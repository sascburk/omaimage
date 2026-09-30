#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

// First-boot files understood by Raspberry Pi OS.
// cloudinit-rpi: current Raspberry Pi OS (user-data, network-config, meta-data)
// systemd: legacy images (firstrun.sh + systemd.run on cmdline.txt)
// cloudinit: generic cloud-init
// rpi-preseed: rpi-preseed.toml
// none: write the image only

struct CustomRequest {
    QString initFormat;
    QString releaseDate;
    QString hostname;
    QString username;
    QString password;
    QString locale;
    QString timezone;
    QString keyboard;
    QString wifiSsid;
    QString wifiPassword;
    QString wifiCountry;
    bool wifiHidden = false;
    bool wifiOpen = false;
    bool sshEnabled = false;
    bool sshPasswordAuth = true;
    QString sshKeys;
    bool passwordlessSudo = false;
    bool enableI2C = false;
    bool enableSPI = false;
    bool enable1Wire = false;
    bool enableUsbGadget = false;
    QString serial;
    bool connectEnabled = false;
    QString connectToken;
};

struct CustomResult {
    QString error;
    QHash<QString, QByteArray> files;
    QByteArray cmdlineAppend;
};

CustomResult buildCustomisation(const CustomRequest &request);
bool customiseSelfTest(QString *error);
bool initFormatSupportsCustomisation(const QString &initFormat);
bool initFormatSupportsInterfaces(const QString &initFormat);
QString describeInitFormat(const QString &initFormat);
