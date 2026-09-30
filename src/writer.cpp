#include "writer.h"

#include "drives.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cerrno>
#include <csignal>
#include <cstring>

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

volatile sig_atomic_t g_stop = 0;

void onStop(int) {
    g_stop = 1;
}

void emitLine(const char *kind, const QString &text) {
    QString single = text;
    single.replace(QLatin1Char('\n'), QLatin1Char(' '));
    fprintf(stdout, "%s %s\n", kind, single.toUtf8().constData());
    fflush(stdout);
}

bool writeAll(int fd, const char *data, qint64 length) {
    qint64 offset = 0;
    while (offset < length) {
        if (g_stop)
            return false;
        const ssize_t written = ::write(fd, data + offset, static_cast<size_t>(length - offset));
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (written == 0)
            return false;
        offset += written;
    }
    return true;
}

enum class Compression { None, Xz, Gzip, Zstd, Zip, Reject };

Compression sniff(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QCoreApplication::translate("Writer", "The image cannot be opened.");
        return Compression::Reject;
    }
    const QByteArray magic = file.read(8);
    if (magic.isEmpty()) {
        *error = QCoreApplication::translate("Writer", "The image file is empty.");
        return Compression::Reject;
    }
    if (magic.startsWith("<!") || magic.startsWith("<ht") || magic.startsWith("<HT")
        || magic.startsWith("{") || magic.startsWith("<htm")) {
        *error = QCoreApplication::translate("Writer", "The file is not a disk image.");
        return Compression::Reject;
    }
    if (magic.startsWith(QByteArray::fromHex("FD377A585A00")))
        return Compression::Xz;
    if (magic.size() >= 2 && magic[0] == '\x1f' && magic[1] == '\x8b')
        return Compression::Gzip;
    if (magic.size() >= 4 && memcmp(magic.constData(), "\x28\xb5\x2f\xfd", 4) == 0)
        return Compression::Zstd;
    if (magic.startsWith("PK"))
        return Compression::Zip;

    const QString name = QFileInfo(path).fileName().toLower();
    if (name.contains(QLatin1String(".xz")) || name.endsWith(QLatin1String(".gz"))
        || name.endsWith(QLatin1String(".zip")) || name.endsWith(QLatin1String(".zst"))) {
        *error = QCoreApplication::translate("Writer", "The file extension does not match the contents.");
        return Compression::Reject;
    }
    return Compression::None;
}

bool streamDecompressed(const QString &source, Compression compression, int fd,
                        QCryptographicHash *hash, qint64 expected, qint64 *written, QString *error) {
    qint64 lastReport = 0;
    auto report = [&]() {
        if (*written - lastReport >= 8LL * 1024 * 1024 || (expected > 0 && *written == expected)) {
            emitLine("PROGRESS", QString::number(*written) + QLatin1Char(' ') + QString::number(expected));
            lastReport = *written;
        }
    };

    auto consume = [&](const QByteArray &chunk) {
        if (chunk.isEmpty())
            return true;
        if (!writeAll(fd, chunk.constData(), chunk.size())) {
            *error = g_stop ? QCoreApplication::translate("Writer", "Cancelled.")
                            : QCoreApplication::translate("Writer", "Writing to the drive failed.");
            return false;
        }
        hash->addData(chunk);
        *written += chunk.size();
        report();
        return true;
    };

    if (compression == Compression::None) {
        QFile file(source);
        if (!file.open(QIODevice::ReadOnly)) {
            *error = QCoreApplication::translate("Writer", "The image cannot be read.");
            return false;
        }
        while (!g_stop) {
            const QByteArray chunk = file.read(1024 * 1024);
            if (chunk.isEmpty())
                return file.atEnd();
            if (!consume(chunk))
                return false;
        }
        *error = QCoreApplication::translate("Writer", "Cancelled.");
        return false;
    }

    QString program;
    QStringList arguments;
    if (compression == Compression::Xz) {
        program = QStringLiteral("xz");
        arguments = {QStringLiteral("-dc"), QStringLiteral("--threads=0"), source};
    } else if (compression == Compression::Gzip) {
        program = QStringLiteral("gzip");
        arguments = {QStringLiteral("-dc"), source};
    } else if (compression == Compression::Zstd) {
        program = QStringLiteral("zstd");
        arguments = {QStringLiteral("-dc"), source};
    } else {
        program = QStringLiteral("bsdtar");
        arguments = {QStringLiteral("-xOf"), source};
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(5000)) {
        *error = QCoreApplication::translate("Writer", "%1 could not be started.").arg(program);
        return false;
    }

    while (process.state() != QProcess::NotRunning || process.bytesAvailable() > 0) {
        if (g_stop) {
            process.kill();
            process.waitForFinished(2000);
            *error = QCoreApplication::translate("Writer", "Cancelled.");
            return false;
        }
        if (process.bytesAvailable() == 0)
            process.waitForReadyRead(200);
        process.readAllStandardError();
        const QByteArray chunk = process.readAllStandardOutput();
        if (!chunk.isEmpty() && !consume(chunk)) {
            process.kill();
            process.waitForFinished(2000);
            return false;
        }
    }
    process.waitForFinished(10000);
    process.readAllStandardError();
    const QByteArray rest = process.readAllStandardOutput();
    if (!rest.isEmpty() && !consume(rest))
        return false;
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *error = QCoreApplication::translate("Writer", "Decompression failed.");
        return false;
    }
    return true;
}

void unmountNode(const BlockDevice &node) {
    for (const BlockDevice &child : node.children)
        unmountNode(child);
    if (node.fstype == QLatin1String("swap"))
        QProcess::execute(QStringLiteral("swapoff"), {node.path});
    for (const QString &mount : node.mountpoints)
        QProcess::execute(QStringLiteral("umount"), {mount});
    if (node.type != QLatin1String("disk"))
        QProcess::execute(QStringLiteral("umount"), {node.path});
}

void unmountDisk(const QString &diskPath) {
    const QVector<BlockDevice> devices = listBlockDevices(nullptr);
    for (const BlockDevice &device : devices) {
        if (device.path == diskPath)
            unmountNode(device);
    }
}

QString bootPartition(const QString &diskPath) {
    const QVector<BlockDevice> devices = listBlockDevices(nullptr);
    QString fallback;
    qint64 fallbackSize = 0;
    for (const BlockDevice &device : devices) {
        if (device.path != diskPath)
            continue;
        for (const BlockDevice &child : device.children) {
            const bool fat = child.fstype == QLatin1String("vfat")
                || child.fstype == QLatin1String("fat")
                || child.fstype == QLatin1String("exfat");
            if (!fat)
                continue;
            if (fallback.isEmpty() || child.size < fallbackSize) {
                fallback = child.path;
                fallbackSize = child.size;
            }
        }
    }
    if (!fallback.isEmpty())
        return fallback;

    const QString name = QFileInfo(diskPath).fileName();
    const bool needsP = !name.isEmpty() && name.back().isDigit();
    return diskPath + (needsP ? QStringLiteral("p1") : QStringLiteral("1"));
}

bool applyCustomisation(const QString &diskPath, const QJsonObject &files, const QByteArray &cmdline,
                        QString *error) {
    if (files.isEmpty() && cmdline.isEmpty())
        return true;

    emitLine("STATUS", QCoreApplication::translate("Writer", "Reading partitions…"));
    QProcess::execute(QStringLiteral("partprobe"), {diskPath});
    QProcess::execute(QStringLiteral("udevadm"), {QStringLiteral("settle"), QStringLiteral("--timeout=8")});

    const QString partition = bootPartition(diskPath);
    const QString mountPoint = QStringLiteral("/run/omaimage-boot");
    QDir().mkpath(mountPoint);
    QProcess::execute(QStringLiteral("umount"), {mountPoint});

    emitLine("STATUS", QCoreApplication::translate("Writer", "Writing setup to the boot partition…"));
    const int mounted = QProcess::execute(QStringLiteral("mount"),
                                           {QStringLiteral("-o"), QStringLiteral("rw"),
                                            partition, mountPoint});
    if (mounted != 0) {
        *error = QCoreApplication::translate("Writer", "The boot partition could not be mounted. The image was written, but the Raspberry Pi setup was not.");
        return false;
    }

    bool ok = true;
    const QStringList names = files.keys();
    for (const QString &name : names) {
        if (name.contains(QLatin1Char('/')) || name.contains(QLatin1String(".."))) {
            *error = QCoreApplication::translate("Writer", "Invalid file name in the setup.");
            ok = false;
            break;
        }
        QFile file(mountPoint + QLatin1Char('/') + name);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            *error = QCoreApplication::translate("Writer", "Could not write “%1”.").arg(name);
            ok = false;
            break;
        }
        const QByteArray payload = files.value(name).toString().toUtf8();
        if (file.write(payload) != payload.size()) {
            *error = QCoreApplication::translate("Writer", "“%1” is incomplete.").arg(name);
            ok = false;
            break;
        }
    }

    if (ok && !cmdline.isEmpty()) {
        QFile file(mountPoint + QStringLiteral("/cmdline.txt"));
        if (!file.open(QIODevice::ReadOnly)) {
            *error = QCoreApplication::translate("Writer", "cmdline.txt is missing on the boot partition. Setup cannot be enabled.");
            ok = false;
        } else {
            QByteArray existing = file.readAll();
            file.close();
            const int nul = existing.indexOf('\0');
            if (nul >= 0)
                existing.truncate(nul);
            existing = existing.trimmed();
            if (!existing.contains(cmdline.trimmed()))
                existing += cmdline;
            existing += '\n';
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
                || file.write(existing) != existing.size()) {
                *error = QCoreApplication::translate("Writer", "cmdline.txt could not be updated.");
                ok = false;
            }
        }
    }

    ::sync();
    const int unmounted = QProcess::execute(QStringLiteral("umount"), {mountPoint});
    if (unmounted != 0 && ok) {
        *error = QCoreApplication::translate("Writer", "The boot partition could not be unmounted cleanly.");
        ok = false;
    }
    return ok;
}

} // namespace

int runWriteJob(const QString &jobPath, bool allowFile) {
    std::signal(SIGTERM, onStop);
    std::signal(SIGINT, onStop);

    QFile jobFile(jobPath);
    if (!jobFile.open(QIODevice::ReadOnly)) {
        emitLine("ERROR", QCoreApplication::translate("Writer", "The job file is missing."));
        return 1;
    }
    const QJsonObject job = QJsonDocument::fromJson(jobFile.readAll()).object();
    const QString source = job.value(QStringLiteral("source")).toString();
    const QString device = job.value(QStringLiteral("device")).toString();
    const qint64 extractSize = job.value(QStringLiteral("extractSize")).toInteger();
    const QString expectedHash = job.value(QStringLiteral("extractSha256")).toString().trimmed().toLower();
    const QJsonObject files = job.value(QStringLiteral("files")).toObject();
    const QByteArray cmdline = job.value(QStringLiteral("cmdline")).toString().toUtf8();

    if (source.isEmpty() || device.isEmpty() || !device.startsWith(QLatin1String("/"))) {
        emitLine("ERROR", QCoreApplication::translate("Writer", "The write job is incomplete."));
        return 1;
    }

    QString sniffError;
    const Compression compression = sniff(source, &sniffError);
    if (compression == Compression::Reject) {
        emitLine("ERROR", sniffError);
        return 1;
    }

    QFileInfo info(device);
    const bool regularFile = info.exists() ? info.isFile() : allowFile;
    if (!allowFile) {
        QString listError;
        const QVector<BlockDevice> devices = listBlockDevices(&listError);
        const BlockDevice *match = nullptr;
        for (const BlockDevice &candidate : devices) {
            if (candidate.path == device) {
                match = &candidate;
                break;
            }
        }
        if (!match || match->type != QLatin1String("disk")) {
            emitLine("ERROR", QCoreApplication::translate("Writer", "The target is not a whole drive."));
            return 1;
        }
        if (match->system) {
            emitLine("ERROR", QCoreApplication::translate("Writer", "The system disk is not written."));
            return 1;
        }
        if (extractSize > 0 && match->size > 0 && extractSize > match->size) {
            emitLine("ERROR", QCoreApplication::translate("Writer", "The image is larger than the drive."));
            return 1;
        }
        emitLine("STATUS", QCoreApplication::translate("Writer", "Unmounting the drive…"));
        unmountDisk(device);
    } else if (!regularFile && !device.startsWith(QLatin1String("/dev/"))) {
        emitLine("ERROR", QCoreApplication::translate("Writer", "Invalid target."));
        return 1;
    }

    const int flags = allowFile ? (O_WRONLY | O_CREAT | O_TRUNC) : (O_WRONLY | O_EXCL);
    int fd = ::open(device.toLocal8Bit().constData(), flags, 0644);
    if (fd < 0 && !allowFile && (errno == EBUSY || errno == EPERM)) {
        unmountDisk(device);
        fd = ::open(device.toLocal8Bit().constData(), flags, 0644);
    }
    if (fd < 0) {
        emitLine("ERROR", QCoreApplication::translate("Writer", "The drive cannot be opened. Is it still mounted?"));
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || !(S_ISBLK(st.st_mode) || (allowFile && S_ISREG(st.st_mode)))) {
        ::close(fd);
        emitLine("ERROR", QCoreApplication::translate("Writer", "The target is neither a drive nor a test file."));
        return 1;
    }
    if (S_ISBLK(st.st_mode) && extractSize > 0) {
        quint64 deviceSize = 0;
        if (ioctl(fd, BLKGETSIZE64, &deviceSize) == 0 && deviceSize > 0
            && static_cast<quint64>(extractSize) > deviceSize) {
            ::close(fd);
            emitLine("ERROR", QCoreApplication::translate("Writer", "The image is larger than the drive."));
            return 1;
        }
    }

    emitLine("STATUS", QCoreApplication::translate("Writer", "Writing the image…"));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 written = 0;
    QString writeError;
    const bool streamed = streamDecompressed(source, compression, fd, &hash, extractSize, &written, &writeError);
    ::fsync(fd);
    if (S_ISBLK(st.st_mode))
        ioctl(fd, BLKFLSBUF);
    ::close(fd);
    ::sync();

    if (!streamed) {
        emitLine("ERROR", writeError.isEmpty() ? QCoreApplication::translate("Writer", "Writing failed.") : writeError);
        return g_stop ? 2 : 1;
    }
    if (extractSize > 0 && written != extractSize) {
        emitLine("ERROR", QCoreApplication::translate("Writer", "The written size does not match the image."));
        return 1;
    }
    const QString actualHash = QString::fromLatin1(hash.result().toHex());
    if (!expectedHash.isEmpty() && actualHash != expectedHash) {
        emitLine("ERROR", QCoreApplication::translate("Writer", "The checksum does not match. Do not use this drive."));
        return 1;
    }

    if (!allowFile) {
        QString customError;
        if (!applyCustomisation(device, files, cmdline, &customError)) {
            emitLine("ERROR", customError);
            return 1;
        }
        QProcess::execute(QStringLiteral("eject"), {device});
    }

    emitLine("PROGRESS", QString::number(written) + QLatin1Char(' ') + QString::number(qMax(extractSize, written)));
    emitLine("DONE", QCoreApplication::translate("Writer", "Done. The drive can be removed."));
    return 0;
}

bool writerSelfTest(QString *error) {
    QTemporaryDir dir;
    if (!dir.isValid()) {
        if (error)
            *error = QStringLiteral("Temporary directory is missing.");
        return false;
    }
    const QString rawPath = dir.filePath(QStringLiteral("raw.bin"));
    const QByteArray payload(64 * 1024, 'A');
    QFile raw(rawPath);
    if (!raw.open(QIODevice::WriteOnly) || raw.write(payload) != payload.size()) {
        if (error)
            *error = QStringLiteral("Test file is missing.");
        return false;
    }
    raw.close();
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());

    QProcess xz;
    xz.start(QStringLiteral("xz"), {QStringLiteral("-kf"), rawPath});
    if (!xz.waitForFinished(10000) || xz.exitCode() != 0) {
        if (error)
            *error = QStringLiteral("xz is missing for the write test.");
        return false;
    }

    const QString outPath = dir.filePath(QStringLiteral("out.bin"));
    QJsonObject job;
    job.insert(QStringLiteral("source"), rawPath + QStringLiteral(".xz"));
    job.insert(QStringLiteral("device"), outPath);
    job.insert(QStringLiteral("extractSize"), payload.size());
    job.insert(QStringLiteral("extractSha256"), hash);
    const QString jobPath = dir.filePath(QStringLiteral("job.json"));
    QFile jobFile(jobPath);
    if (!jobFile.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("The job could not be written.");
        return false;
    }
    jobFile.write(QJsonDocument(job).toJson());
    jobFile.close();

    if (runWriteJob(jobPath, true) != 0) {
        if (error)
            *error = QStringLiteral("The write test failed.");
        return false;
    }
    QFile out(outPath);
    if (!out.open(QIODevice::ReadOnly) || out.readAll() != payload) {
        if (error)
            *error = QStringLiteral("Decompressed contents differ.");
        return false;
    }

    job.insert(QStringLiteral("extractSha256"), QStringLiteral("00"));
    if (!jobFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("The job could not be written.");
        return false;
    }
    jobFile.write(QJsonDocument(job).toJson());
    jobFile.close();
    if (runWriteJob(jobPath, true) == 0) {
        if (error)
            *error = QStringLiteral("A wrong checksum was accepted.");
        return false;
    }
    return true;
}
