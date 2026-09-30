#pragma once

#include <QFile>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QVariantList>
#include <QVariantMap>

class QNetworkReply;
class QTemporaryFile;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool darkMode READ darkMode NOTIFY darkModeChanged)
    Q_PROPERTY(qreal textScale READ textScale WRITE setTextScale NOTIFY textScaleChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily CONSTANT)
    Q_PROPERTY(QString themeBackground READ themeBackground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeForeground READ themeForeground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeAccent READ themeAccent NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeMuted READ themeMuted NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeRaised READ themeRaised NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeSelection READ themeSelection NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeDanger READ themeDanger NOTIFY themeColorsChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(QString crumb READ crumb NOTIFY entriesChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY entriesChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList deviceFilters READ deviceFilters NOTIFY catalogChanged)
    Q_PROPERTY(int deviceFilterIndex READ deviceFilterIndex WRITE setDeviceFilterIndex NOTIFY catalogChanged)
    Q_PROPERTY(QVariantList drives READ drives NOTIFY drivesChanged)
    Q_PROPERTY(int driveIndex READ driveIndex NOTIFY drivesChanged)
    Q_PROPERTY(QVariantList repositories READ repositories NOTIFY catalogChanged)
    Q_PROPERTY(QVariantList directImages READ directImages NOTIFY catalogChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressLabel READ progressLabel NOTIFY progressChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logChanged)
    Q_PROPERTY(QString defaultLocale READ defaultLocale CONSTANT)
    Q_PROPERTY(QString defaultTimezone READ defaultTimezone CONSTANT)
    Q_PROPERTY(QString defaultKeyboard READ defaultKeyboard CONSTANT)
    Q_PROPERTY(QString defaultCountry READ defaultCountry CONSTANT)

public:
    explicit Backend(QObject *parent = nullptr);

    bool darkMode() const { return m_darkMode; }
    qreal textScale() const { return m_textScale; }
    void setTextScale(qreal textScale);
    QString fontFamily() const { return m_fontFamily; }
    QString themeBackground() const { return m_themeBackground; }
    QString themeForeground() const { return m_themeForeground; }
    QString themeAccent() const { return m_themeAccent; }
    QString themeMuted() const { return m_themeMuted; }
    QString themeRaised() const { return m_themeRaised; }
    QString themeSelection() const { return m_themeSelection; }
    QString themeDanger() const { return m_themeDanger; }

    QVariantList entries() const { return m_entries; }
    QString crumb() const;
    bool canGoBack() const { return !m_path.isEmpty() || !m_search.isEmpty(); }
    QString status() const { return m_status; }
    QVariantMap selection() const { return m_selection; }
    bool hasSelection() const { return !m_selection.isEmpty(); }
    QVariantList deviceFilters() const { return m_deviceFilters; }
    int deviceFilterIndex() const { return m_deviceFilterIndex; }
    QVariantList drives() const { return m_drives; }
    int driveIndex() const { return m_driveIndex; }
    QVariantList repositories() const;
    QVariantList directImages() const;
    bool busy() const { return m_busy; }
    qreal progress() const { return m_progress; }
    QString progressLabel() const { return m_progressLabel; }
    QString logText() const { return m_log; }

    QString defaultLocale() const;
    QString defaultTimezone() const;
    QString defaultKeyboard() const;
    QString defaultCountry() const;

    void setDarkMode(bool darkMode);

    Q_INVOKABLE void refreshCatalog();
    Q_INVOKABLE void setSearch(const QString &text);
    Q_INVOKABLE void setDeviceFilterIndex(int index);
    Q_INVOKABLE void openEntry(int index);
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void chooseLocalImage();
    Q_INVOKABLE void addRepository(const QString &url);
    Q_INVOKABLE void removeRepository(const QString &url);
    Q_INVOKABLE void addDirectImage(const QString &name, const QString &url, const QString &initFormat);
    Q_INVOKABLE void removeDirectImage(const QString &url);
    Q_INVOKABLE void setInitFormat(const QString &initFormat);
    Q_INVOKABLE void refreshDrives(bool includeInternal);
    Q_INVOKABLE void selectDrive(int index);
    Q_INVOKABLE QVariantMap savedSetup() const;
    Q_INVOKABLE void startWrite(const QVariantMap &settings);
    Q_INVOKABLE void cancelWrite();
    Q_INVOKABLE QString pickSshKeys();
    Q_INVOKABLE QVariantMap windowGeometry() const;
    Q_INVOKABLE void saveWindowGeometry(int x, int y, int width, int height, bool maximized);

signals:
    void darkModeChanged();
    void textScaleChanged();
    void themeColorsChanged();
    void entriesChanged();
    void statusChanged();
    void selectionChanged();
    void catalogChanged();
    void drivesChanged();
    void busyChanged();
    void progressChanged();
    void logChanged();
    void finished(const QString &message);
    void failed(const QString &message);

private:
    struct Repo {
        QString url;
        QString title;
        QJsonObject root;
        QString error;
    };

    void loadTheme();
    void watchTheme();
    void loadCache();
    void saveCache() const;
    void saveSources() const;
    void ingestRepo(const QString &url, const QByteArray &body, const QString &error);
    void rebuildFilters();
    void rebuildEntries();
    QJsonArray virtualRoot() const;
    QJsonArray filterTree(const QJsonArray &list) const;
    bool osVisible(const QJsonObject &os) const;
    QVariantMap entryFromJson(const QJsonObject &os, bool category) const;
    void selectImage(const QVariantMap &image);
    void setStatus(const QString &status);
    void appendLog(const QString &line);
    void setBusy(bool busy);
    void setProgress(qreal progress, const QString &label);
    void fail(const QString &message);
    void succeed(const QString &message);
    void beginDownload(const QString &url);
    void launchWriter(const QString &imagePath);
    void handleWriterLine(const QString &line);
    void finishWriter(int exitCode);
    QString cachePathFor(const QString &url) const;
    void rememberSetup(QVariantMap settings) const;

    bool m_darkMode = true;
    qreal m_textScale = 1.0;
    QString m_fontFamily;
    QString m_themeBackground;
    QString m_themeForeground;
    QString m_themeAccent;
    QString m_themeMuted;
    QString m_themeRaised;
    QString m_themeSelection;
    QString m_themeDanger;
    QFileSystemWatcher m_themeWatcher;

    QStringList m_repoUrls;
    QVector<Repo> m_repos;
    QVector<QVariantMap> m_direct;
    QVariantList m_deviceFilters;
    int m_deviceFilterIndex = 0;
    QStringList m_path;
    QString m_search;
    QVariantList m_entries;
    QString m_status;
    QVariantMap m_selection;

    QVariantList m_drives;
    int m_driveIndex = -1;
    QString m_drivePath;
    qint64 m_driveSize = 0;
    bool m_includeInternal = false;

    bool m_busy = false;
    qreal m_progress = 0;
    QString m_progressLabel;
    QString m_log;
    bool m_settled = false;
    QHash<QString, QByteArray> m_pendingFiles;
    QByteArray m_pendingCmdline;
    qint64 m_pendingExtractSize = 0;
    QString m_pendingExtractHash;

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    QFile *m_downloadFile = nullptr;
    QString m_downloadTarget;
    int m_fetchGeneration = 0;
    int m_fetchesLeft = 0;
    QProcess *m_writer = nullptr;
    QTemporaryFile *m_jobFile = nullptr;
    QByteArray m_writerBuffer;
};
