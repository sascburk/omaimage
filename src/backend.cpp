#include "backend.h"

#include "customise.h"
#include "drives.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimeZone>
#include <QUrl>
#include <QVariant>

namespace {

const auto kOfficialRepo = QStringLiteral("https://downloads.raspberrypi.com/os_list_imagingutility_v4.json");

QString repoTitle(const QString &url) {
    if (url == kOfficialRepo)
        return QStringLiteral("Raspberry Pi");
    const QUrl parsed(url);
    const QString host = parsed.host();
    const QString file = QFileInfo(parsed.path()).fileName();
    if (host.isEmpty())
        return file.isEmpty() ? url : file;
    return file.isEmpty() ? host : host + QStringLiteral(" · ") + file;
}

QStringList jsonStrings(const QJsonValue &value) {
    QStringList result;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &entry : array)
        result << entry.toString();
    return result;
}

qint64 jsonInt(const QJsonValue &value) {
    if (value.isString())
        return value.toString().toLongLong();
    return value.toInteger();
}

} // namespace

Backend::Backend(QObject *parent) : QObject(parent) {
    m_fontFamily = QFontDatabase::families().contains(QStringLiteral("iA Writer Mono S"))
        ? QStringLiteral("iA Writer Mono S")
        : QStringLiteral("monospace");
    loadTheme();
    watchTheme();
    connect(&m_themeWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &) {
        loadTheme();
        watchTheme();
    });
    connect(&m_themeWatcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &) {
        loadTheme();
        watchTheme();
    });

    QSettings settings;
    m_repoUrls = settings.value(QStringLiteral("repositories")).toStringList();
    if (m_repoUrls.isEmpty())
        m_repoUrls << kOfficialRepo;
    const QVariantList direct = settings.value(QStringLiteral("directImages")).toList();
    for (const QVariant &entry : direct) {
        const QVariantMap map = entry.toMap();
        if (!map.value(QStringLiteral("url")).toString().isEmpty())
            m_direct.append(map);
    }
    m_deviceFilterIndex = settings.value(QStringLiteral("deviceFilter"), 0).toInt();
    loadCache();
    rebuildFilters();
    rebuildEntries();
    refreshCatalog();
}

void Backend::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;
    m_darkMode = darkMode;
    emit darkModeChanged();
    loadTheme();
}

void Backend::setTextScale(qreal textScale) {
    if (qFuzzyCompare(m_textScale, textScale))
        return;
    m_textScale = textScale;
    emit textScaleChanged();
}

void Backend::loadTheme() {
    m_themeBackground = m_darkMode ? QStringLiteral("#101010") : QStringLiteral("#ffffff");
    m_themeForeground = m_darkMode ? QStringLiteral("#eeeeee") : QStringLiteral("#222324");
    m_themeAccent = m_darkMode ? QStringLiteral("#5584aa") : QStringLiteral("#2077b2");
    m_themeSelection = m_darkMode ? QStringLiteral("#186a9a") : QStringLiteral("#2077b2");
    m_themeMuted = m_darkMode ? QStringLiteral("#a7c9c6") : QStringLiteral("#5c6570");
    m_themeRaised = m_darkMode ? QStringLiteral("#1c1c1c") : QStringLiteral("#f3f3f3");
    m_themeDanger = QStringLiteral("#f85525");

    const QString colorsPath = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
    QString themeMode;
    QFile file(colorsPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            const int equals = line.indexOf(QLatin1Char('='));
            if (equals < 0)
                continue;
            const QString key = line.left(equals).trimmed();
            QString value = line.mid(equals + 1).trimmed();
            if (value.size() >= 2
                && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
                    || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))))
                value = value.mid(1, value.size() - 2);

            if (key == QLatin1String("mode"))
                themeMode = value;
            else if (key == QLatin1String("background"))
                m_themeBackground = value;
            else if (key == QLatin1String("foreground"))
                m_themeForeground = value;
            else if (key == QLatin1String("accent"))
                m_themeAccent = value;
            else if (key == QLatin1String("selection"))
                m_themeSelection = value;
            else if (key == QLatin1String("light_foreground"))
                m_themeMuted = value;
            else if (key == QLatin1String("lighter_background"))
                m_themeRaised = value;
            else if (key == QLatin1String("red"))
                m_themeDanger = value;
        }
    }

    bool themeIsDark = m_darkMode;
    bool known = false;
    if (themeMode == QLatin1String("dark")) {
        themeIsDark = true;
        known = true;
    } else if (themeMode == QLatin1String("light")) {
        themeIsDark = false;
        known = true;
    }
    if (known && themeIsDark != m_darkMode) {
        m_darkMode = themeIsDark;
        emit darkModeChanged();
    }
    emit themeColorsChanged();
}

void Backend::watchTheme() {
    const QStringList watched = m_themeWatcher.files() + m_themeWatcher.directories();
    if (!watched.isEmpty())
        m_themeWatcher.removePaths(watched);
    const QString currentDir = QDir::homePath() + QStringLiteral("/.local/state/omarchy/current");
    const QString themeDir = currentDir + QStringLiteral("/theme");
    const QString colorsPath = themeDir + QStringLiteral("/colors.toml");
    if (QDir(currentDir).exists())
        m_themeWatcher.addPath(currentDir);
    if (QDir(themeDir).exists())
        m_themeWatcher.addPath(themeDir);
    if (QFile::exists(colorsPath))
        m_themeWatcher.addPath(colorsPath);
}

QString Backend::defaultLocale() const {
    const QString name = QLocale::system().name();
    if (name.contains(QLatin1Char('_')))
        return name + QStringLiteral(".UTF-8");
    return QStringLiteral("en_US.UTF-8");
}

QString Backend::defaultTimezone() const {
    const QByteArray id = QTimeZone::systemTimeZoneId();
    return id.isEmpty() ? QStringLiteral("UTC") : QString::fromUtf8(id);
}

QString Backend::defaultKeyboard() const {
    const QString territory = QLocale::territoryToCode(QLocale::system().territory()).toLower();
    if (territory == QLatin1String("gb"))
        return QStringLiteral("gb");
    if (territory == QLatin1String("us"))
        return QStringLiteral("us");
    if (territory.size() == 2)
        return territory;
    return QStringLiteral("us");
}

QString Backend::defaultCountry() const {
    const QString territory = QLocale::territoryToCode(QLocale::system().territory()).toUpper();
    return territory.size() == 2 ? territory : QStringLiteral("US");
}

QString Backend::crumb() const {
    if (!m_search.isEmpty())
        return Backend::tr("Search");
    if (m_path.isEmpty())
        return Backend::tr("Images");
    return m_path.join(QStringLiteral("  /  "));
}

QVariantList Backend::repositories() const {
    QVariantList list;
    for (const QString &url : m_repoUrls) {
        QVariantMap map;
        map.insert(QStringLiteral("url"), url);
        map.insert(QStringLiteral("title"), repoTitle(url));
        list.append(map);
    }
    return list;
}

QVariantList Backend::directImages() const {
    QVariantList list;
    for (const QVariantMap &image : m_direct)
        list.append(image);
    return list;
}

void Backend::setStatus(const QString &status) {
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void Backend::loadCache() {
    QFile file(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
               + QStringLiteral("/catalog.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray repos = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("repos")).toArray();
    for (const QJsonValue &value : repos) {
        const QJsonObject object = value.toObject();
        Repo repo;
        repo.url = object.value(QStringLiteral("url")).toString();
        repo.title = repoTitle(repo.url);
        repo.root = object.value(QStringLiteral("json")).toObject();
        if (!repo.url.isEmpty() && repo.root.contains(QStringLiteral("os_list")))
            m_repos.append(repo);
    }
}

void Backend::saveCache() const {
    QJsonArray repos;
    for (const Repo &repo : m_repos) {
        if (repo.root.isEmpty())
            continue;
        QJsonObject object;
        object.insert(QStringLiteral("url"), repo.url);
        object.insert(QStringLiteral("json"), repo.root);
        repos.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("repos"), repos);
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(directory);
    QFile file(directory + QStringLiteral("/catalog.json"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void Backend::saveSources() const {
    QSettings settings;
    settings.setValue(QStringLiteral("repositories"), m_repoUrls);
    QVariantList direct;
    for (const QVariantMap &image : m_direct)
        direct.append(image);
    settings.setValue(QStringLiteral("directImages"), direct);
    settings.setValue(QStringLiteral("deviceFilter"), m_deviceFilterIndex);
}

void Backend::refreshCatalog() {
    ++m_fetchGeneration;
    const int generation = m_fetchGeneration;
    m_fetchesLeft = m_repoUrls.size();
    if (m_fetchesLeft == 0) {
        setStatus(Backend::tr("No catalog URL configured."));
        rebuildEntries();
        return;
    }
    setStatus(Backend::tr("Loading image list…"));
    for (const QString &url : m_repoUrls) {
        const QUrl parsed(url);
        if (parsed.isLocalFile() || QFileInfo(url).isAbsolute()) {
            const QString path = parsed.isLocalFile() ? parsed.toLocalFile() : url;
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                ingestRepo(url, {}, Backend::tr("File is not readable."));
            else
                ingestRepo(url, file.readAll(), {});
            continue;
        }

        QNetworkRequest request(parsed);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Omaimage/1.0"));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *reply = m_nam.get(request);
        reply->setProperty("generation", generation);
        reply->setProperty("repoUrl", url);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            const int generation = reply->property("generation").toInt();
            const QString url = reply->property("repoUrl").toString();
            if (generation == m_fetchGeneration) {
                if (reply->error() != QNetworkReply::NoError)
                    ingestRepo(url, {}, reply->errorString());
                else
                    ingestRepo(url, reply->readAll(), {});
            }
            reply->deleteLater();
        });
    }
}

void Backend::ingestRepo(const QString &url, const QByteArray &body, const QString &error) {
    Repo repo;
    repo.url = url;
    repo.title = repoTitle(url);
    repo.error = error;
    if (error.isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()
            || !document.object().contains(QStringLiteral("os_list"))) {
            repo.error = Backend::tr("Not an image catalog.");
        } else {
            repo.root = document.object();
        }
    }

    bool replaced = false;
    for (Repo &existing : m_repos) {
        if (existing.url == url) {
            if (repo.root.isEmpty() && !existing.root.isEmpty())
                existing.error = repo.error;
            else
                existing = repo;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        m_repos.append(repo);

    --m_fetchesLeft;
    if (m_fetchesLeft > 0)
        return;

    saveCache();
    rebuildFilters();
    rebuildEntries();
    int images = 0;
    for (const Repo &stored : m_repos)
        images += stored.root.value(QStringLiteral("os_list")).toArray().size();
    QStringList problems;
    for (const Repo &stored : m_repos) {
        if (!stored.error.isEmpty())
            problems << stored.title + QStringLiteral(": ") + stored.error;
    }
    if (!problems.isEmpty() && images == 0)
        setStatus(problems.join(QStringLiteral(" ")));
    else if (!problems.isEmpty())
        setStatus(Backend::tr("List loaded, one source failed."));
    else
        setStatus(Backend::tr("Current images loaded."));
    emit catalogChanged();
}

void Backend::rebuildFilters() {
    m_deviceFilters.clear();
    QVariantMap all;
    all.insert(QStringLiteral("name"), Backend::tr("All devices"));
    all.insert(QStringLiteral("tags"), QStringList{});
    all.insert(QStringLiteral("exclusive"), false);
    m_deviceFilters.append(all);

    for (const Repo &repo : m_repos) {
        if (repo.url != kOfficialRepo)
            continue;
        const QJsonArray found = repo.root.value(QStringLiteral("imager")).toObject()
                                     .value(QStringLiteral("devices")).toArray();
        for (const QJsonValue &value : found) {
            const QJsonObject device = value.toObject();
            QVariantMap map;
            map.insert(QStringLiteral("name"), device.value(QStringLiteral("name")).toString());
            map.insert(QStringLiteral("tags"), jsonStrings(device.value(QStringLiteral("tags"))));
            map.insert(QStringLiteral("exclusive"),
                       device.value(QStringLiteral("matching_type")).toString() == QLatin1String("exclusive"));
            m_deviceFilters.append(map);
        }
        break;
    }
    if (m_deviceFilterIndex < 0 || m_deviceFilterIndex >= m_deviceFilters.size())
        m_deviceFilterIndex = 0;
}

bool Backend::osVisible(const QJsonObject &os) const {
    if (os.value(QStringLiteral("unfiltered")).toBool())
        return true;
    if (m_deviceFilterIndex <= 0 || m_deviceFilterIndex >= m_deviceFilters.size())
        return true;
    const QVariantMap filter = m_deviceFilters.at(m_deviceFilterIndex).toMap();
    const QStringList tags = filter.value(QStringLiteral("tags")).toStringList();
    if (tags.isEmpty())
        return true;
    const QStringList devices = jsonStrings(os.value(QStringLiteral("devices")));
    if (devices.isEmpty())
        return !filter.value(QStringLiteral("exclusive")).toBool();
    for (const QString &tag : devices) {
        if (tags.contains(tag))
            return true;
    }
    return false;
}

QJsonArray Backend::filterTree(const QJsonArray &list) const {
    QJsonArray result;
    for (const QJsonValue &value : list) {
        QJsonObject object = value.toObject();
        if (object.contains(QStringLiteral("subitems"))) {
            const QJsonArray devices = object.value(QStringLiteral("devices")).toArray();
            if (!devices.isEmpty() && !osVisible(object))
                continue;
            const QJsonArray children = filterTree(object.value(QStringLiteral("subitems")).toArray());
            if (children.isEmpty())
                continue;
            object.insert(QStringLiteral("subitems"), children);
            result.append(object);
        } else if (object.contains(QStringLiteral("url"))) {
            if (osVisible(object))
                result.append(object);
        }
    }
    return result;
}

QJsonArray Backend::virtualRoot() const {
    QJsonArray root;
    if (!m_direct.isEmpty()) {
        QJsonObject category;
        category.insert(QStringLiteral("name"), Backend::tr("Custom images"));
        category.insert(QStringLiteral("description"), Backend::tr("Directly linked ISO and image files"));
        category.insert(QStringLiteral("unfiltered"), true);
        QJsonArray children;
        for (const QVariantMap &image : m_direct) {
            QJsonObject object;
            object.insert(QStringLiteral("name"), image.value(QStringLiteral("name")).toString());
            object.insert(QStringLiteral("description"), image.value(QStringLiteral("url")).toString());
            object.insert(QStringLiteral("url"), image.value(QStringLiteral("url")).toString());
            object.insert(QStringLiteral("init_format"), image.value(QStringLiteral("initFormat")).toString());
            object.insert(QStringLiteral("unfiltered"), true);
            object.insert(QStringLiteral("editable"), true);
            children.append(object);
        }
        category.insert(QStringLiteral("subitems"), children);
        root.append(category);
    }

    if (m_repos.size() <= 1) {
        const QJsonArray list = m_repos.isEmpty()
            ? QJsonArray()
            : m_repos.first().root.value(QStringLiteral("os_list")).toArray();
        for (const QJsonValue &value : list)
            root.append(value);
    } else {
        for (const Repo &repo : m_repos) {
            QJsonObject category;
            category.insert(QStringLiteral("name"), repo.title);
            category.insert(QStringLiteral("description"), repo.error.isEmpty() ? repo.url : repo.error);
            category.insert(QStringLiteral("subitems"), repo.root.value(QStringLiteral("os_list")).toArray());
            category.insert(QStringLiteral("unfiltered"), true);
            root.append(category);
        }
    }
    return root;
}

QVariantMap Backend::entryFromJson(const QJsonObject &os, bool category) const {
    QVariantMap map;
    map.insert(QStringLiteral("name"), os.value(QStringLiteral("name")).toString());
    map.insert(QStringLiteral("description"), os.value(QStringLiteral("description")).toString());
    map.insert(QStringLiteral("icon"), os.value(QStringLiteral("icon")).toString());
    map.insert(QStringLiteral("category"), category);
    const QString format = os.value(QStringLiteral("init_format")).toString();
    map.insert(QStringLiteral("initFormat"), format);
    map.insert(QStringLiteral("url"), os.value(QStringLiteral("url")).toString());
    map.insert(QStringLiteral("extractSize"), jsonInt(os.value(QStringLiteral("extract_size"))));
    map.insert(QStringLiteral("downloadSize"), jsonInt(os.value(QStringLiteral("image_download_size"))));
    map.insert(QStringLiteral("extractSha256"), os.value(QStringLiteral("extract_sha256")).toString());
    map.insert(QStringLiteral("downloadSha256"), os.value(QStringLiteral("image_download_sha256")).toString());
    map.insert(QStringLiteral("releaseDate"), os.value(QStringLiteral("release_date")).toString());
    map.insert(QStringLiteral("capabilities"), jsonStrings(os.value(QStringLiteral("capabilities"))));
    map.insert(QStringLiteral("editable"), os.value(QStringLiteral("editable")).toBool());

    QStringList meta;
    const qint64 size = jsonInt(os.value(QStringLiteral("extract_size")));
    if (!category && size > 0)
        meta << formatBytes(size);
    const QString date = os.value(QStringLiteral("release_date")).toString();
    if (!date.isEmpty())
        meta << date;
    if (!category && initFormatSupportsCustomisation(format))
        meta << Backend::tr("Customisation available");
    else if (!category && !format.isEmpty())
        meta << Backend::tr("Write only");
    map.insert(QStringLiteral("meta"), meta.join(QStringLiteral("  ·  ")));
    return map;
}

void Backend::rebuildEntries() {
    QJsonArray level = filterTree(virtualRoot());
    if (m_search.isEmpty()) {
        for (const QString &part : m_path) {
            bool found = false;
            for (const QJsonValue &value : level) {
                const QJsonObject object = value.toObject();
                if (object.value(QStringLiteral("name")).toString() == part) {
                    level = object.value(QStringLiteral("subitems")).toArray();
                    found = true;
                    break;
                }
            }
            if (!found) {
                m_path.clear();
                level = filterTree(virtualRoot());
                break;
            }
        }
    }

    m_entries.clear();
    if (!m_search.isEmpty()) {
        const QString needle = m_search.trimmed().toLower();
        QVector<QJsonObject> stack;
        const QJsonArray root = filterTree(virtualRoot());
        for (const QJsonValue &value : root)
            stack.append(value.toObject());
        while (!stack.isEmpty()) {
            const QJsonObject object = stack.takeLast();
            const QJsonArray children = object.value(QStringLiteral("subitems")).toArray();
            for (const QJsonValue &child : children)
                stack.append(child.toObject());
            if (!object.contains(QStringLiteral("url")))
                continue;
            const QString name = object.value(QStringLiteral("name")).toString();
            const QString description = object.value(QStringLiteral("description")).toString();
            if (name.toLower().contains(needle) || description.toLower().contains(needle))
                m_entries.append(entryFromJson(object, false));
        }
    } else {
        for (const QJsonValue &value : level) {
            const QJsonObject object = value.toObject();
            const bool category = object.contains(QStringLiteral("subitems"));
            m_entries.append(entryFromJson(object, category));
        }
    }
    emit entriesChanged();
}

void Backend::setSearch(const QString &text) {
    if (m_search == text)
        return;
    m_search = text;
    rebuildEntries();
}

void Backend::setDeviceFilterIndex(int index) {
    if (index < 0 || index >= m_deviceFilters.size() || index == m_deviceFilterIndex)
        return;
    m_deviceFilterIndex = index;
    saveSources();
    rebuildEntries();
    emit catalogChanged();
}

void Backend::openEntry(int index) {
    if (index < 0 || index >= m_entries.size())
        return;
    const QVariantMap entry = m_entries.at(index).toMap();
    if (entry.value(QStringLiteral("category")).toBool()) {
        m_search.clear();
        m_path.append(entry.value(QStringLiteral("name")).toString());
        rebuildEntries();
        return;
    }
    selectImage(entry);
}

void Backend::goBack() {
    if (!m_search.isEmpty()) {
        m_search.clear();
        rebuildEntries();
        return;
    }
    if (!m_path.isEmpty()) {
        m_path.removeLast();
        rebuildEntries();
    }
}

void Backend::selectImage(const QVariantMap &image) {
    QVariantMap selection = image;
    selection.remove(QStringLiteral("category"));
    const QString format = selection.value(QStringLiteral("initFormat")).toString();
    const bool editable = selection.value(QStringLiteral("editable")).toBool();
    selection.insert(QStringLiteral("formatEditable"), editable);
    selection.insert(QStringLiteral("customisation"), initFormatSupportsCustomisation(format));
    selection.insert(QStringLiteral("interfaces"), initFormatSupportsInterfaces(format));
    selection.insert(QStringLiteral("formatLabel"), describeInitFormat(format));
    selection.insert(QStringLiteral("localPath"), QString());
    const QString url = selection.value(QStringLiteral("url")).toString();
    const QUrl parsed(url);
    if (parsed.isLocalFile())
        selection.insert(QStringLiteral("localPath"), parsed.toLocalFile());
    else if (QFileInfo(url).isAbsolute() && QFileInfo(url).exists())
        selection.insert(QStringLiteral("localPath"), url);
    m_selection = selection;
    emit selectionChanged();
}

void Backend::chooseLocalImage() {
    const QString path = QFileDialog::getOpenFileName(
        nullptr, Backend::tr("Choose image"), QDir::homePath(),
        Backend::tr("Images (*.img *.iso *.xz *.gz *.zip *.zst *.img.xz *.iso.xz);;All files (*)"));
    if (path.isEmpty())
        return;
    QVariantMap image;
    image.insert(QStringLiteral("name"), QFileInfo(path).fileName());
    image.insert(QStringLiteral("description"), path);
    image.insert(QStringLiteral("url"), QUrl::fromLocalFile(path).toString());
    image.insert(QStringLiteral("localPath"), path);
    image.insert(QStringLiteral("initFormat"), QStringLiteral("none"));
    image.insert(QStringLiteral("editable"), true);
    image.insert(QStringLiteral("formatEditable"), true);
    image.insert(QStringLiteral("customisation"), false);
    image.insert(QStringLiteral("interfaces"), false);
    image.insert(QStringLiteral("formatLabel"), describeInitFormat(QStringLiteral("none")));
    image.insert(QStringLiteral("extractSize"), 0);
    m_selection = image;
    emit selectionChanged();
}

void Backend::setInitFormat(const QString &initFormat) {
    if (!m_selection.value(QStringLiteral("formatEditable")).toBool())
        return;
    m_selection.insert(QStringLiteral("initFormat"), initFormat);
    m_selection.insert(QStringLiteral("customisation"), initFormatSupportsCustomisation(initFormat));
    m_selection.insert(QStringLiteral("interfaces"), initFormatSupportsInterfaces(initFormat));
    m_selection.insert(QStringLiteral("formatLabel"), describeInitFormat(initFormat));
    emit selectionChanged();
}

void Backend::addRepository(const QString &url) {
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty() || m_repoUrls.contains(trimmed))
        return;
    const QUrl parsed(trimmed);
    const bool local = parsed.isLocalFile() || QFileInfo(trimmed).isAbsolute();
    if (!local && parsed.scheme() != QLatin1String("https") && parsed.scheme() != QLatin1String("http")) {
        setStatus(Backend::tr("The catalog URL needs http or https."));
        return;
    }
    m_repoUrls.append(trimmed);
    saveSources();
    emit catalogChanged();
    refreshCatalog();
}

void Backend::removeRepository(const QString &url) {
    m_repoUrls.removeAll(url);
    QVector<Repo> kept;
    for (const Repo &repo : m_repos) {
        if (repo.url != url)
            kept.append(repo);
    }
    m_repos = kept;
    saveSources();
    saveCache();
    rebuildFilters();
    rebuildEntries();
    emit catalogChanged();
}

void Backend::addDirectImage(const QString &name, const QString &url, const QString &initFormat) {
    const QString trimmedUrl = url.trimmed();
    if (trimmedUrl.isEmpty())
        return;
    const QUrl parsed(trimmedUrl);
    const bool local = parsed.isLocalFile() || QFileInfo(trimmedUrl).isAbsolute();
    if (!local && parsed.scheme() != QLatin1String("https") && parsed.scheme() != QLatin1String("http")) {
        setStatus(Backend::tr("The image URL needs http or https."));
        return;
    }
    for (const QVariantMap &image : m_direct) {
        if (image.value(QStringLiteral("url")).toString() == trimmedUrl)
            return;
    }
    QVariantMap image;
    image.insert(QStringLiteral("name"), name.trimmed().isEmpty() ? QFileInfo(parsed.path()).fileName() : name.trimmed());
    if (image.value(QStringLiteral("name")).toString().isEmpty())
        image.insert(QStringLiteral("name"), trimmedUrl);
    image.insert(QStringLiteral("url"), trimmedUrl);
    image.insert(QStringLiteral("initFormat"), initFormat.isEmpty() ? QStringLiteral("none") : initFormat);
    m_direct.append(image);
    saveSources();
    rebuildEntries();
    emit catalogChanged();
}

void Backend::removeDirectImage(const QString &url) {
    QVector<QVariantMap> kept;
    for (const QVariantMap &image : m_direct) {
        if (image.value(QStringLiteral("url")).toString() != url)
            kept.append(image);
    }
    m_direct = kept;
    saveSources();
    rebuildEntries();
    emit catalogChanged();
}

void Backend::refreshDrives(bool includeInternal) {
    m_includeInternal = includeInternal;
    QString error;
    const QVector<BlockDevice> found = writableDrives(includeInternal, &error);
    const QString previous = m_drivePath;
    m_drives.clear();
    m_driveIndex = -1;
    m_drivePath.clear();
    m_driveSize = 0;
    int index = 0;
    for (const BlockDevice &drive : found) {
        QVariantMap map;
        map.insert(QStringLiteral("path"), drive.path);
        const QString model = drive.model.trimmed().isEmpty() ? drive.name : drive.model.trimmed();
        map.insert(QStringLiteral("title"), model);
        QStringList detail;
        detail << formatBytes(drive.size) << drive.path;
        if (drive.tran == QLatin1String("usb") || drive.removable)
            detail << QStringLiteral("USB");
        else
            detail << Backend::tr("internal");
        map.insert(QStringLiteral("detail"), detail.join(QStringLiteral("  ·  ")));
        map.insert(QStringLiteral("size"), drive.size);
        m_drives.append(map);
        if (drive.path == previous) {
            m_driveIndex = index;
            m_drivePath = drive.path;
            m_driveSize = drive.size;
        }
        ++index;
    }
    if (!error.isEmpty() && m_drives.isEmpty())
        setStatus(error);
    emit drivesChanged();
}

void Backend::selectDrive(int index) {
    if (index < 0 || index >= m_drives.size())
        return;
    m_driveIndex = index;
    const QVariantMap drive = m_drives.at(index).toMap();
    m_drivePath = drive.value(QStringLiteral("path")).toString();
    m_driveSize = drive.value(QStringLiteral("size")).toLongLong();
    emit drivesChanged();
}

QVariantMap Backend::savedSetup() const {
    return QSettings().value(QStringLiteral("setup")).toMap();
}

void Backend::rememberSetup(QVariantMap settings) const {
    settings.remove(QStringLiteral("password"));
    settings.remove(QStringLiteral("password2"));
    settings.remove(QStringLiteral("wifiPassword"));
    settings.remove(QStringLiteral("connectToken"));
    QSettings store;
    store.setValue(QStringLiteral("setup"), settings);
}

QString Backend::pickSshKeys() {
    const QString path = QFileDialog::getOpenFileName(
        nullptr, Backend::tr("Choose a public SSH key"), QDir::homePath(),
        Backend::tr("Keys (*.pub);;Text files (*.txt);;All files (*)"));
    if (path.isEmpty())
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.read(64 * 1024));
}

void Backend::appendLog(const QString &line) {
    if (!m_log.isEmpty())
        m_log += QLatin1Char('\n');
    m_log += line;
    emit logChanged();
}

void Backend::setBusy(bool busy) {
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void Backend::setProgress(qreal progress, const QString &label) {
    m_progress = progress;
    m_progressLabel = label;
    emit progressChanged();
}

void Backend::fail(const QString &message) {
    if (m_settled)
        return;
    m_settled = true;
    appendLog(message);
    setBusy(false);
    setProgress(0, QString());
    emit failed(message);
}

void Backend::succeed(const QString &message) {
    if (m_settled)
        return;
    m_settled = true;
    appendLog(message);
    setBusy(false);
    setProgress(1, message);
    emit finished(message);
}

void Backend::startWrite(const QVariantMap &settings) {
    if (m_busy)
        return;
    if (!hasSelection()) {
        emit failed(Backend::tr("Choose an image first."));
        return;
    }
    if (m_drivePath.isEmpty()) {
        emit failed(Backend::tr("Choose a USB drive first."));
        return;
    }
    if (settings.value(QStringLiteral("password")).toString()
        != settings.value(QStringLiteral("password2")).toString()) {
        emit failed(Backend::tr("The passwords do not match."));
        return;
    }

    CustomRequest request;
    request.initFormat = m_selection.value(QStringLiteral("initFormat")).toString();
    request.releaseDate = m_selection.value(QStringLiteral("releaseDate")).toString();
    request.hostname = settings.value(QStringLiteral("hostname")).toString();
    request.username = settings.value(QStringLiteral("username")).toString();
    request.password = settings.value(QStringLiteral("password")).toString();
    request.locale = settings.value(QStringLiteral("locale")).toString();
    request.timezone = settings.value(QStringLiteral("timezone")).toString();
    request.keyboard = settings.value(QStringLiteral("keyboard")).toString();
    if (settings.value(QStringLiteral("wifiEnabled")).toBool()) {
        request.wifiSsid = settings.value(QStringLiteral("wifiSsid")).toString();
        request.wifiPassword = settings.value(QStringLiteral("wifiPassword")).toString();
        request.wifiCountry = settings.value(QStringLiteral("wifiCountry")).toString();
        request.wifiHidden = settings.value(QStringLiteral("wifiHidden")).toBool();
        request.wifiOpen = settings.value(QStringLiteral("wifiOpen")).toBool();
    }
    request.sshEnabled = settings.value(QStringLiteral("sshEnabled")).toBool();
    request.sshPasswordAuth = settings.value(QStringLiteral("sshPasswordAuth")).toBool();
    request.sshKeys = settings.value(QStringLiteral("sshKeys")).toString();
    request.passwordlessSudo = settings.value(QStringLiteral("passwordlessSudo")).toBool();
    request.enableI2C = settings.value(QStringLiteral("enableI2C")).toBool();
    request.enableSPI = settings.value(QStringLiteral("enableSPI")).toBool();
    request.enable1Wire = settings.value(QStringLiteral("enable1Wire")).toBool();
    request.enableUsbGadget = settings.value(QStringLiteral("enableUsbGadget")).toBool();
    request.serial = settings.value(QStringLiteral("serial")).toString();
    request.connectEnabled = settings.value(QStringLiteral("connectEnabled")).toBool();
    request.connectToken = settings.value(QStringLiteral("connectToken")).toString();

    const CustomResult custom = buildCustomisation(request);
    if (!custom.error.isEmpty()) {
        emit failed(custom.error);
        return;
    }

    m_pendingExtractSize = m_selection.value(QStringLiteral("extractSize")).toLongLong();
    m_pendingExtractHash = m_selection.value(QStringLiteral("extractSha256")).toString();
    if (m_pendingExtractSize > 0 && m_driveSize > 0 && m_pendingExtractSize > m_driveSize) {
        emit failed(Backend::tr("The image is larger than the drive."));
        return;
    }

    m_pendingFiles = custom.files;
    m_pendingCmdline = custom.cmdlineAppend;
    rememberSetup(settings);
    m_settled = false;
    m_log.clear();
    emit logChanged();
    setBusy(true);
    appendLog(Backend::tr("Target: %1").arg(m_drivePath));
    appendLog(Backend::tr("Image: %1").arg(m_selection.value(QStringLiteral("name")).toString()));

    const QString local = m_selection.value(QStringLiteral("localPath")).toString();
    if (!local.isEmpty())
        launchWriter(local);
    else
        beginDownload(m_selection.value(QStringLiteral("url")).toString());
}

QString Backend::cachePathFor(const QString &url) const {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/images");
    QDir().mkpath(directory);
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex().left(12));
    QString name = QFileInfo(QUrl(url).path()).fileName();
    if (name.isEmpty())
        name = QStringLiteral("image.bin");
    return directory + QLatin1Char('/') + key + QLatin1Char('-') + name;
}

void Backend::beginDownload(const QString &url) {
    const QString target = cachePathFor(url);
    const qint64 expected = m_selection.value(QStringLiteral("downloadSize")).toLongLong();
    if (QFileInfo::exists(target) && (expected <= 0 || QFileInfo(target).size() == expected)) {
        appendLog(Backend::tr("Already downloaded, writing from cache."));
        launchWriter(target);
        return;
    }

    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Omaimage/1.0"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_downloadTarget = target + QStringLiteral(".part");
    m_downloadFile = new QFile(m_downloadTarget, this);
    if (!m_downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(Backend::tr("Cache is not writable."));
        delete m_downloadFile;
        m_downloadFile = nullptr;
        return;
    }
    setProgress(0, Backend::tr("Downloading…"));
    appendLog(Backend::tr("Loading %1").arg(url));
    m_reply = m_nam.get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_downloadFile && m_reply)
            m_downloadFile->write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        const qint64 expected = total > 0 ? total : m_selection.value(QStringLiteral("downloadSize")).toLongLong();
        const qreal value = expected > 0 ? qreal(received) / qreal(expected) : -1;
        setProgress(value, Backend::tr("Downloading  %1 / %2")
                               .arg(formatBytes(received), expected > 0 ? formatBytes(expected) : QStringLiteral("?")));
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, target]() {
        if (!m_reply)
            return;
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        const bool failedDownload = reply->error() != QNetworkReply::NoError;
        const QString error = reply->errorString();
        if (m_downloadFile) {
            m_downloadFile->write(reply->readAll());
            m_downloadFile->close();
            delete m_downloadFile;
            m_downloadFile = nullptr;
        }
        reply->deleteLater();
        if (m_settled)
            return;
        if (failedDownload) {
            QFile::remove(m_downloadTarget);
            fail(Backend::tr("Download failed: %1").arg(error));
            return;
        }
        QFile::remove(target);
        if (!QFile::rename(m_downloadTarget, target)) {
            fail(Backend::tr("The downloaded file could not be saved."));
            return;
        }
        launchWriter(target);
    });
}

void Backend::launchWriter(const QString &imagePath) {
    if (!QFileInfo::exists(imagePath)) {
        fail(Backend::tr("The image file is missing."));
        return;
    }
    m_jobFile = new QTemporaryFile(this);
    m_jobFile->setAutoRemove(true);
    m_jobFile->setFileTemplate(QDir::tempPath() + QStringLiteral("/omaimage-job-XXXXXX.json"));
    if (!m_jobFile->open()) {
        fail(Backend::tr("The job file could not be created."));
        return;
    }
    QJsonObject files;
    for (auto it = m_pendingFiles.cbegin(); it != m_pendingFiles.cend(); ++it)
        files.insert(it.key(), QString::fromUtf8(it.value()));
    QJsonObject job;
    job.insert(QStringLiteral("source"), imagePath);
    job.insert(QStringLiteral("device"), m_drivePath);
    job.insert(QStringLiteral("extractSize"), QJsonValue(m_pendingExtractSize));
    job.insert(QStringLiteral("extractSha256"), m_pendingExtractHash);
    job.insert(QStringLiteral("files"), files);
    job.insert(QStringLiteral("cmdline"), QString::fromUtf8(m_pendingCmdline));
    m_jobFile->write(QJsonDocument(job).toJson(QJsonDocument::Compact));
    m_jobFile->flush();
    m_jobFile->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    setProgress(0, Backend::tr("Waiting for authorisation…"));
    appendLog(Backend::tr("Requesting administrator rights…"));
    m_writer = new QProcess(this);
    m_writer->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_writer, &QProcess::readyReadStandardOutput, this, [this]() {
        m_writerBuffer += m_writer->readAllStandardOutput();
        while (true) {
            const int newline = m_writerBuffer.indexOf('\n');
            if (newline < 0)
                break;
            const QString line = QString::fromUtf8(m_writerBuffer.left(newline)).trimmed();
            m_writerBuffer.remove(0, newline + 1);
            handleWriterLine(line);
        }
    });
    connect(m_writer, &QProcess::readyReadStandardError, this, [this]() {
        const QString text = QString::fromUtf8(m_writer->readAllStandardError()).trimmed();
        if (!text.isEmpty())
            appendLog(text);
    });
    connect(m_writer, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        if (!m_writerBuffer.trimmed().isEmpty())
            handleWriterLine(QString::fromUtf8(m_writerBuffer).trimmed());
        m_writerBuffer.clear();
        finishWriter(exitCode);
    });
    connect(m_writer, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!m_settled && m_writer && m_writer->state() == QProcess::NotRunning)
            fail(Backend::tr("pkexec could not be started."));
    });
    m_writer->start(QStringLiteral("pkexec"),
                    {QCoreApplication::applicationFilePath(), QStringLiteral("--write-job"),
                     m_jobFile->fileName()});
}

void Backend::handleWriterLine(const QString &line) {
    if (line.startsWith(QLatin1String("STATUS "))) {
        const QString text = line.mid(7);
        appendLog(text);
        setProgress(m_progress, text);
    } else if (line.startsWith(QLatin1String("PROGRESS "))) {
        const QStringList parts = line.mid(9).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            const qint64 done = parts.at(0).toLongLong();
            const qint64 total = parts.at(1).toLongLong();
            const qreal value = total > 0 ? qreal(done) / qreal(total) : -1;
            setProgress(value, Backend::tr("Writing  %1 / %2")
                                   .arg(formatBytes(done), total > 0 ? formatBytes(total) : QStringLiteral("?")));
        }
    } else if (line.startsWith(QLatin1String("DONE "))) {
        succeed(line.mid(5));
    } else if (line.startsWith(QLatin1String("ERROR "))) {
        fail(line.mid(6));
    }
}

void Backend::finishWriter(int exitCode) {
    m_writer->deleteLater();
    m_writer = nullptr;
    delete m_jobFile;
    m_jobFile = nullptr;
    if (m_settled)
        return;
    if (exitCode == 0)
        succeed(Backend::tr("Done. The drive can be removed."));
    else if (exitCode == 126 || exitCode == 127)
        fail(Backend::tr("Administrator authorisation was denied."));
    else
        fail(Backend::tr("Writing failed."));
}

void Backend::cancelWrite() {
    if (m_reply) {
        m_reply->abort();
    }
    if (m_writer && m_writer->state() != QProcess::NotRunning) {
        m_writer->terminate();
        appendLog(Backend::tr("Cancellation requested…"));
    } else if (!m_settled && m_busy) {
        fail(Backend::tr("Cancelled."));
    }
}

QVariantMap Backend::windowGeometry() const {
    const QSettings settings;
    const QRect geometry = settings.value(QStringLiteral("window/geometry")).toRect();
    QVariantMap map;
    map.insert(QStringLiteral("valid"), geometry.isValid());
    map.insert(QStringLiteral("x"), geometry.x());
    map.insert(QStringLiteral("y"), geometry.y());
    map.insert(QStringLiteral("width"), geometry.width());
    map.insert(QStringLiteral("height"), geometry.height());
    map.insert(QStringLiteral("maximized"), settings.value(QStringLiteral("window/maximized"), false).toBool());
    return map;
}

void Backend::saveWindowGeometry(int x, int y, int width, int height, bool maximized) {
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), QRect(x, y, width, height));
    settings.setValue(QStringLiteral("window/maximized"), maximized);
}
