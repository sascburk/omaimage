#include "drives.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>

namespace {

bool protectedMount(const QString &mountpoint) {
    static const QStringList kProtected = {
        QStringLiteral("/"),
        QStringLiteral("/boot"),
        QStringLiteral("/boot/efi"),
        QStringLiteral("/boot/firmware"),
        QStringLiteral("/efi"),
        QStringLiteral("/home"),
        QStringLiteral("/usr"),
        QStringLiteral("/var"),
    };
    return kProtected.contains(mountpoint);
}

QStringList readMountpoints(const QJsonValue &value) {
    QStringList mounts;
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &entry : array) {
            const QString mount = entry.toString();
            if (!mount.isEmpty() && mount != QLatin1String("[SWAP]"))
                mounts << mount;
        }
    } else if (value.isString()) {
        const QString mount = value.toString();
        if (!mount.isEmpty() && mount != QLatin1String("[SWAP]"))
            mounts << mount;
    }
    return mounts;
}

BlockDevice deviceFromJson(const QJsonObject &object) {
    BlockDevice device;
    device.name = object.value(QStringLiteral("name")).toString();
    device.path = object.value(QStringLiteral("path")).toString();
    if (device.path.isEmpty() && !device.name.isEmpty())
        device.path = QStringLiteral("/dev/") + device.name;
    device.model = object.value(QStringLiteral("model")).toString();
    device.tran = object.value(QStringLiteral("tran")).toString();
    device.fstype = object.value(QStringLiteral("fstype")).toString();
    device.type = object.value(QStringLiteral("type")).toString();
    const QJsonValue size = object.value(QStringLiteral("size"));
    device.size = size.isString() ? size.toString().toLongLong() : size.toInteger();
    device.removable = object.value(QStringLiteral("rm")).toBool();
    device.readOnly = object.value(QStringLiteral("ro")).toBool();
    device.hotplug = object.value(QStringLiteral("hotplug")).toBool();
    device.mountpoints = readMountpoints(object.value(QStringLiteral("mountpoints")));

    const QJsonArray children = object.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &child : children)
        device.children.append(deviceFromJson(child.toObject()));
    return device;
}

void markSystemDisks(BlockDevice &device, const QString &diskPath, QSet<QString> *systemDisks) {
    const QString disk = device.type == QLatin1String("disk") ? device.path : diskPath;
    for (const QString &mount : device.mountpoints) {
        if (protectedMount(mount))
            systemDisks->insert(disk);
    }
    for (BlockDevice &child : device.children)
        markSystemDisks(child, disk, systemDisks);
}

void applySystemFlag(BlockDevice &device, const QSet<QString> &systemDisks) {
    device.system = systemDisks.contains(device.path);
    for (BlockDevice &child : device.children)
        applySystemFlag(child, systemDisks);
}

bool ignoredName(const QString &name) {
    return name.startsWith(QLatin1String("zram"))
        || name.startsWith(QLatin1String("loop"))
        || name.startsWith(QLatin1String("ram"))
        || name.startsWith(QLatin1String("sr"))
        || name.startsWith(QLatin1String("fd"))
        || name.startsWith(QLatin1String("dm-"));
}

} // namespace

QString formatBytes(qint64 bytes) {
    if (bytes <= 0)
        return QStringLiteral("0 B");
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = bytes;
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    const int precision = (unit == 0 || value >= 100.0) ? 0 : 1;
    return QString::number(value, 'f', precision) + QLatin1Char(' ') + QLatin1String(units[unit]);
}

QVector<BlockDevice> listBlockDevices(QString *error) {
    QProcess process;
    process.start(QStringLiteral("lsblk"),
                  {QStringLiteral("-J"), QStringLiteral("-b"),
                   QStringLiteral("-o"),
                   QStringLiteral("NAME,PATH,SIZE,TYPE,TRAN,RM,RO,MODEL,MOUNTPOINTS,FSTYPE,HOTPLUG")});
    if (!process.waitForFinished(8000)) {
        if (error)
            *error = QCoreApplication::translate("Drives", "lsblk did not respond.");
        return {};
    }
    if (process.exitCode() != 0) {
        if (error)
            *error = QCoreApplication::translate("Drives", "Drives could not be read.");
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(process.readAllStandardOutput(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QCoreApplication::translate("Drives", "The drive list is unreadable.");
        return {};
    }

    QVector<BlockDevice> devices;
    const QJsonArray list = document.object().value(QStringLiteral("blockdevices")).toArray();
    for (const QJsonValue &value : list)
        devices.append(deviceFromJson(value.toObject()));

    QSet<QString> systemDisks;
    for (BlockDevice &device : devices)
        markSystemDisks(device, device.path, &systemDisks);
    for (BlockDevice &device : devices)
        applySystemFlag(device, systemDisks);
    return devices;
}

QVector<BlockDevice> writableDrives(bool includeInternal, QString *error) {
    QVector<BlockDevice> result;
    const QVector<BlockDevice> devices = listBlockDevices(error);
    for (const BlockDevice &device : devices) {
        if (device.type != QLatin1String("disk"))
            continue;
        if (device.system || device.readOnly || device.size < 32LL * 1024 * 1024)
            continue;
        if (ignoredName(device.name))
            continue;
        const bool external = device.removable || device.hotplug
            || device.tran == QLatin1String("usb");
        if (!includeInternal && !external)
            continue;
        result.append(device);
    }
    return result;
}

bool drivesSelfTest(QString *error) {
    QString listError;
    const QVector<BlockDevice> devices = listBlockDevices(&listError);
    if (devices.isEmpty()) {
        if (error)
            *error = listError.isEmpty() ? QStringLiteral("No drives were seen.") : listError;
        return false;
    }
    bool sawSystem = false;
    for (const BlockDevice &device : devices) {
        if (device.system)
            sawSystem = true;
    }
    if (!sawSystem) {
        if (error)
            *error = QStringLiteral("The system disk was not detected.");
        return false;
    }
    const QVector<BlockDevice> offered = writableDrives(true, &listError);
    for (const BlockDevice &drive : offered) {
        if (drive.system) {
            if (error)
                *error = QStringLiteral("The system disk would be offered for writing.");
            return false;
        }
    }
    if (formatBytes(1024LL * 1024 * 1024) != QLatin1String("1.0 GB")) {
        if (error)
            *error = QStringLiteral("formatBytes is wrong.");
        return false;
    }
    return true;
}
