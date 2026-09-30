#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "customise.h"

#include <crypt.h>

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QPasswordDigestor>
#include <QRegularExpression>

#include <cstring>
#include <memory>

namespace {

QString shellQuote(const QString &value) {
    QString quoted = value;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

QString yamlQuote(const QString &value) {
    QString quoted = value;
    quoted.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    quoted.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    quoted.remove(QLatin1Char('\r'));
    quoted.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

QString tomlQuote(const QString &value) {
    QString quoted = value;
    quoted.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    quoted.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    quoted.remove(QLatin1Char('\r'));
    quoted.remove(QLatin1Char('\n'));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

QString stripNewlines(QString value) {
    value.remove(QLatin1Char('\r'));
    value.remove(QLatin1Char('\n'));
    return value;
}

QString countryCode(const QString &value) {
    const QString trimmed = value.trimmed();
    if (trimmed.size() != 2)
        return {};
    for (const QChar character : trimmed) {
        if (!character.isLetter())
            return {};
    }
    return trimmed.toUpper();
}

bool usesYescrypt(const QString &releaseDate) {
    const QDate date = QDate::fromString(releaseDate, Qt::ISODate);
    return date.isValid() && date >= QDate(2023, 1, 1);
}

QString cryptHash(const QString &password, const QString &releaseDate) {
    const QByteArray cleaned = stripNewlines(password).toUtf8();
    if (cleaned.isEmpty())
        return {};

    char salt[256];
    const char *prefix = usesYescrypt(releaseDate) ? "$y$" : "$5$";
    if (!crypt_gensalt_rn(prefix, 0, nullptr, 0, salt, sizeof salt))
        return {};

    auto data = std::make_unique<crypt_data>();
    std::memset(data.get(), 0, sizeof(crypt_data));
    char *hash = crypt_r(cleaned.constData(), salt, data.get());
    return hash ? QString::fromUtf8(hash) : QString();
}

QString wifiPsk(const QString &ssid, const QString &password) {
    const QString cleaned = stripNewlines(password);
    if (cleaned.isEmpty())
        return {};
    const bool passphrase = cleaned.size() >= 8 && cleaned.size() < 64;
    if (!passphrase)
        return cleaned.toLower();
    const QByteArray derived = QPasswordDigestor::deriveKeyPbkdf2(
        QCryptographicHash::Sha1, cleaned.toUtf8(), ssid.toUtf8(), 4096, 32);
    return QString::fromLatin1(derived.toHex());
}

QStringList sshKeyLines(const QString &raw) {
    QStringList keys;
    const QStringList lines = raw.split(QRegularExpression(QStringLiteral("\r?\n")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;
        keys << trimmed;
    }
    return keys;
}

QString normaliseLocale(QString locale) {
    locale = locale.trimmed();
    if (locale.isEmpty())
        return {};
    static const QRegularExpression pattern(QStringLiteral("^[a-z]{2}_[A-Z]{2}(\\.[A-Za-z0-9-]+)?$"));
    if (!pattern.match(locale).hasMatch())
        return {};
    if (!locale.contains(QLatin1Char('.')))
        locale += QStringLiteral(".UTF-8");
    return locale;
}

void appendLine(QByteArray *out, const QString &line) {
    *out += line.toUtf8();
    *out += '\n';
}

QString validate(const CustomRequest &request, bool *any) {
    *any = false;
    const QString user = request.username.trimmed();
    if (!user.isEmpty()) {
        static const QRegularExpression namePattern(QStringLiteral("^[a-z_][a-z0-9_-]{0,31}$"));
        if (!namePattern.match(user).hasMatch())
            return QCoreApplication::translate("Customise", "The username may only contain lowercase letters, digits, _ and -.");
        if (request.password.isEmpty() && sshKeyLines(request.sshKeys).isEmpty())
            return QCoreApplication::translate("Customise", "The user is missing a password.");
        *any = true;
    } else if (!request.password.isEmpty()) {
        return QCoreApplication::translate("Customise", "The password is missing a username.");
    }

    if (!request.hostname.trimmed().isEmpty()) {
        static const QRegularExpression hostPattern(
            QStringLiteral("^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?$"));
        if (!hostPattern.match(request.hostname.trimmed()).hasMatch())
            return QCoreApplication::translate("Customise", "The hostname is invalid.");
        *any = true;
    }

    if (!request.timezone.trimmed().isEmpty()) {
        static const QRegularExpression zonePattern(QStringLiteral("^[A-Za-z0-9_+\\-/]{1,64}$"));
        if (!zonePattern.match(request.timezone.trimmed()).hasMatch())
            return QCoreApplication::translate("Customise", "The time zone is invalid.");
    }
    if (!request.keyboard.trimmed().isEmpty()) {
        static const QRegularExpression keyPattern(QStringLiteral("^[A-Za-z0-9_-]{1,32}$"));
        if (!keyPattern.match(request.keyboard.trimmed()).hasMatch())
            return QCoreApplication::translate("Customise", "The keyboard layout is invalid.");
    }
    if (request.wifiSsid.contains(QLatin1Char('\n')) || request.wifiSsid.contains(QLatin1Char('\r')))
        return QCoreApplication::translate("Customise", "The Wi-Fi name contains a line break.");

    if (!request.wifiSsid.trimmed().isEmpty()) {
        const QString password = stripNewlines(request.wifiPassword);
        if (!request.wifiOpen && (password.size() < 8 || password.size() > 63)
            && password.size() != 64)
            return QCoreApplication::translate("Customise", "The Wi-Fi password must be 8 to 63 characters.");
        if (countryCode(request.wifiCountry).isEmpty())
            return QCoreApplication::translate("Customise", "Wi-Fi is missing a country, for example US.");
        *any = true;
    } else if (!request.wifiPassword.isEmpty()) {
        return QCoreApplication::translate("Customise", "The Wi-Fi password is missing a network name.");
    }

    if (request.sshEnabled)
        *any = true;
    if (!request.timezone.trimmed().isEmpty() || !request.keyboard.trimmed().isEmpty()
        || !normaliseLocale(request.locale).isEmpty())
        *any = true;
    if (request.passwordlessSudo || request.enableI2C || request.enableSPI || request.enable1Wire
        || request.enableUsbGadget || request.connectEnabled)
        *any = true;
    if (!request.serial.isEmpty() && request.serial != QLatin1String("Disabled"))
        *any = true;
    return {};
}

QByteArray systemdScript(const CustomRequest &request, const QString &passwordHash, const QString &psk) {
    QByteArray script;
    const QString hostname = stripNewlines(request.hostname.trimmed());
    const QString user = request.username.trimmed();
    const QString country = countryCode(request.wifiCountry);
    const QString ssid = request.wifiSsid.trimmed();
    const QStringList keys = sshKeyLines(request.sshKeys);
    const QString timezone = request.timezone.trimmed();
    const QString keyboard = request.keyboard.trimmed();
    const QString locale = normaliseLocale(request.locale);

    appendLine(&script, QStringLiteral("#!/bin/sh"));
    appendLine(&script, QString());
    appendLine(&script, QStringLiteral("set +e"));
    appendLine(&script, QString());

    if (!hostname.isEmpty()) {
        appendLine(&script, QStringLiteral("IMAGER_HOSTNAME=") + shellQuote(hostname));
        appendLine(&script, QStringLiteral("CURRENT_HOSTNAME=$(cat /etc/hostname | tr -d \" \\t\\n\\r\")"));
        appendLine(&script, QStringLiteral("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then"));
        appendLine(&script, QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom set_hostname \"$IMAGER_HOSTNAME\""));
        appendLine(&script, QStringLiteral("else"));
        appendLine(&script, QStringLiteral("   echo \"$IMAGER_HOSTNAME\" >/etc/hostname"));
        appendLine(&script, QStringLiteral("   IMAGER_HOSTNAME_SED=$(printf '%s' \"$IMAGER_HOSTNAME\" | sed -e 's/[\\\\/&]/\\\\&/g')"));
        appendLine(&script, QStringLiteral("   sed -i \"s/127.0.1.1.*$CURRENT_HOSTNAME/127.0.1.1\\t$IMAGER_HOSTNAME_SED/g\" /etc/hosts"));
        appendLine(&script, QStringLiteral("fi"));
    }

    appendLine(&script, QStringLiteral("FIRSTUSER=$(getent passwd 1000 | cut -d: -f1)"));
    appendLine(&script, QStringLiteral("FIRSTUSERHOME=$(getent passwd 1000 | cut -d: -f6)"));

    if (!keys.isEmpty()) {
        QString keyArgs;
        for (const QString &key : keys)
            keyArgs += QLatin1Char(' ') + shellQuote(key);
        appendLine(&script, QStringLiteral("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then"));
        appendLine(&script, QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom enable_ssh -k") + keyArgs);
        appendLine(&script, QStringLiteral("else"));
        appendLine(&script, QStringLiteral("   install -o \"$FIRSTUSER\" -m 700 -d \"$FIRSTUSERHOME/.ssh\""));
        appendLine(&script, QStringLiteral("   cat > \"$FIRSTUSERHOME/.ssh/authorized_keys\" <<'EOF'"));
        for (const QString &key : keys)
            appendLine(&script, key);
        appendLine(&script, QStringLiteral("EOF"));
        appendLine(&script, QStringLiteral("   chown \"$FIRSTUSER:$FIRSTUSER\" \"$FIRSTUSERHOME/.ssh/authorized_keys\""));
        appendLine(&script, QStringLiteral("   chmod 600 \"$FIRSTUSERHOME/.ssh/authorized_keys\""));
        appendLine(&script, QStringLiteral("   systemctl enable ssh"));
        appendLine(&script, QStringLiteral("fi"));
        if (request.sshPasswordAuth) {
            appendLine(&script, QStringLiteral("sed -i 's/^#\\?PasswordAuthentication.*/PasswordAuthentication yes/' /etc/ssh/sshd_config"));
        }
    } else if (request.sshEnabled) {
        appendLine(&script, QStringLiteral("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then"));
        appendLine(&script, QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom enable_ssh"));
        appendLine(&script, QStringLiteral("else"));
        appendLine(&script, QStringLiteral("   systemctl enable ssh"));
        appendLine(&script, QStringLiteral("fi"));
    }

    if (!user.isEmpty()) {
        appendLine(&script, QStringLiteral("IMAGER_USER=") + shellQuote(user));
        if (!passwordHash.isEmpty())
            appendLine(&script, QStringLiteral("IMAGER_PASS=") + shellQuote(passwordHash));
        appendLine(&script, QStringLiteral("if [ -f /usr/lib/userconf-pi/userconf ]; then"));
        appendLine(&script, QStringLiteral("   /usr/lib/userconf-pi/userconf \"$IMAGER_USER\" \"$IMAGER_PASS\""));
        appendLine(&script, QStringLiteral("else"));
        if (!passwordHash.isEmpty())
            appendLine(&script, QStringLiteral("   echo \"$FIRSTUSER:$IMAGER_PASS\" | chpasswd -e"));
        appendLine(&script, QStringLiteral("   if [ \"$FIRSTUSER\" != \"$IMAGER_USER\" ]; then"));
        appendLine(&script, QStringLiteral("      usermod -l \"$IMAGER_USER\" \"$FIRSTUSER\""));
        appendLine(&script, QStringLiteral("      usermod -m -d \"/home/$IMAGER_USER\" \"$IMAGER_USER\""));
        appendLine(&script, QStringLiteral("      groupmod -n \"$IMAGER_USER\" \"$FIRSTUSER\""));
        appendLine(&script, QStringLiteral("      if grep -q \"^autologin-user=\" /etc/lightdm/lightdm.conf ; then"));
        appendLine(&script, QStringLiteral("         sed /etc/lightdm/lightdm.conf -i -e \"s/^autologin-user=.*/autologin-user=$IMAGER_USER/\""));
        appendLine(&script, QStringLiteral("      fi"));
        appendLine(&script, QStringLiteral("   fi"));
        appendLine(&script, QStringLiteral("fi"));
        if (request.passwordlessSudo) {
            const QString sudoers = QStringLiteral("/etc/sudoers.d/010_") + user + QStringLiteral("-nopasswd");
            appendLine(&script, QStringLiteral("echo ")
                       + shellQuote(user + QStringLiteral(" ALL=(ALL) NOPASSWD:ALL"))
                       + QStringLiteral(" >") + shellQuote(sudoers));
            appendLine(&script, QStringLiteral("chmod 0440 ") + shellQuote(sudoers));
        }
    }

    if (!ssid.isEmpty()) {
        appendLine(&script, QStringLiteral("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then"));
        QString command = QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom set_wlan ");
        if (request.wifiHidden)
            command += QStringLiteral("-h ");
        command += shellQuote(ssid) + QLatin1Char(' ') + shellQuote(psk) + QLatin1Char(' ') + shellQuote(country);
        appendLine(&script, command);
        appendLine(&script, QStringLiteral("else"));
        appendLine(&script, QStringLiteral("cat >/etc/wpa_supplicant/wpa_supplicant.conf <<'WPAEOF'"));
        if (!country.isEmpty())
            appendLine(&script, QStringLiteral("country=") + country);
        appendLine(&script, QStringLiteral("ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev"));
        appendLine(&script, QStringLiteral("update_config=1"));
        appendLine(&script, QStringLiteral("network={"));
        if (request.wifiHidden)
            appendLine(&script, QStringLiteral("\tscan_ssid=1"));
        appendLine(&script, QStringLiteral("\tssid=") + shellQuote(ssid));
        if (request.wifiOpen || psk.isEmpty()) {
            appendLine(&script, QStringLiteral("\tkey_mgmt=NONE"));
        } else {
            appendLine(&script, QStringLiteral("\tkey_mgmt=WPA-PSK SAE"));
            appendLine(&script, QStringLiteral("\tpsk=") + psk);
        }
        appendLine(&script, QStringLiteral("}"));
        appendLine(&script, QStringLiteral("WPAEOF"));
        appendLine(&script, QStringLiteral("   chmod 600 /etc/wpa_supplicant/wpa_supplicant.conf"));
        appendLine(&script, QStringLiteral("   rfkill unblock wifi"));
        appendLine(&script, QStringLiteral("fi"));
    }

    if (!keyboard.isEmpty() || !timezone.isEmpty()) {
        appendLine(&script, QStringLiteral("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then"));
        if (!keyboard.isEmpty())
            appendLine(&script, QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom set_keymap ") + shellQuote(keyboard));
        if (!timezone.isEmpty())
            appendLine(&script, QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom set_timezone ") + shellQuote(timezone));
        appendLine(&script, QStringLiteral("else"));
        if (!timezone.isEmpty()) {
            appendLine(&script, QStringLiteral("   rm -f /etc/localtime"));
            appendLine(&script, QStringLiteral("   echo ") + shellQuote(timezone) + QStringLiteral(" >/etc/timezone"));
            appendLine(&script, QStringLiteral("   dpkg-reconfigure -f noninteractive tzdata"));
        }
        if (!keyboard.isEmpty()) {
            appendLine(&script, QStringLiteral("cat >/etc/default/keyboard <<'KBEOF'"));
            appendLine(&script, QStringLiteral("XKBMODEL=\"pc105\""));
            appendLine(&script, QStringLiteral("XKBLAYOUT=") + shellQuote(keyboard));
            appendLine(&script, QStringLiteral("XKBVARIANT=\"\""));
            appendLine(&script, QStringLiteral("XKBOPTIONS=\"\""));
            appendLine(&script, QStringLiteral("KBEOF"));
            appendLine(&script, QStringLiteral("   dpkg-reconfigure -f noninteractive keyboard-configuration"));
        }
        appendLine(&script, QStringLiteral("fi"));
    }

    if (!locale.isEmpty()) {
        const QString genLine = locale + QStringLiteral(" UTF-8");
        appendLine(&script, QStringLiteral("sed -i ") + shellQuote(QStringLiteral("s/^# *") + genLine + QLatin1Char('/') + genLine + QLatin1Char('/'))
                   + QStringLiteral(" /etc/locale.gen"));
        appendLine(&script, QStringLiteral("grep -q ") + shellQuote(genLine) + QStringLiteral(" /etc/locale.gen || echo ")
                   + shellQuote(genLine) + QStringLiteral(" >> /etc/locale.gen"));
        appendLine(&script, QStringLiteral("locale-gen"));
        appendLine(&script, QStringLiteral("update-locale LANG=") + shellQuote(locale));
    }

    appendLine(&script, QStringLiteral("rm -f /boot/firstrun.sh"));
    appendLine(&script, QStringLiteral("sed -i 's| systemd.run.*||g' /boot/cmdline.txt"));
    appendLine(&script, QStringLiteral("exit 0"));
    return script;
}

void runcmd(QByteArray *cloud, const QString &command) {
    appendLine(cloud, QStringLiteral("  - [ sh, -c, ") + yamlQuote(command) + QStringLiteral(" ]"));
}

QByteArray cloudInitUserData(const CustomRequest &request, const QString &passwordHash, bool withInterfaces) {
    QByteArray cloud;
    const QString hostname = stripNewlines(request.hostname.trimmed());
    const QString user = request.username.trimmed();
    const QString timezone = request.timezone.trimmed();
    const QString keyboard = request.keyboard.trimmed();
    const QString locale = normaliseLocale(request.locale);
    const QStringList keys = sshKeyLines(request.sshKeys);
    const bool hasUser = !user.isEmpty() && (!passwordHash.isEmpty() || !keys.isEmpty());

    if (!hostname.isEmpty()) {
        appendLine(&cloud, QStringLiteral("hostname: ") + yamlQuote(hostname));
        appendLine(&cloud, QStringLiteral("manage_etc_hosts: true"));
        appendLine(&cloud, QStringLiteral("packages:"));
        appendLine(&cloud, QStringLiteral("- avahi-daemon"));
        appendLine(&cloud, QStringLiteral("apt:"));
        appendLine(&cloud, QStringLiteral("  preserve_sources_list: true"));
        appendLine(&cloud, QStringLiteral("  conf: |"));
        appendLine(&cloud, QStringLiteral("    Acquire {"));
        appendLine(&cloud, QStringLiteral("      Check-Date \"false\";"));
        appendLine(&cloud, QStringLiteral("    };"));
    }
    if (!timezone.isEmpty())
        appendLine(&cloud, QStringLiteral("timezone: ") + yamlQuote(timezone));
    if (!keyboard.isEmpty()) {
        appendLine(&cloud, QStringLiteral("keyboard:"));
        appendLine(&cloud, QStringLiteral("  model: pc105"));
        appendLine(&cloud, QStringLiteral("  layout: ") + yamlQuote(keyboard));
    }
    if (!locale.isEmpty())
        appendLine(&cloud, QStringLiteral("locale: ") + yamlQuote(locale));

    if (hasUser) {
        appendLine(&cloud, QStringLiteral("user:"));
        appendLine(&cloud, QStringLiteral("  name: ") + yamlQuote(user));
        appendLine(&cloud, QStringLiteral("  shell: /bin/bash"));
        if (!passwordHash.isEmpty()) {
            appendLine(&cloud, QStringLiteral("  lock_passwd: false"));
            appendLine(&cloud, QStringLiteral("  passwd: ") + yamlQuote(passwordHash));
        } else {
            appendLine(&cloud, QStringLiteral("  lock_passwd: true"));
        }
        if (request.sshEnabled && !keys.isEmpty()) {
            appendLine(&cloud, QStringLiteral("  ssh_authorized_keys:"));
            for (const QString &key : keys)
                appendLine(&cloud, QStringLiteral("    - ") + yamlQuote(key));
        }
        if (request.passwordlessSudo)
            appendLine(&cloud, QStringLiteral("  sudo: ALL=(ALL) NOPASSWD:ALL"));
        else
            appendLine(&cloud, QStringLiteral("  sudo: null"));
    }

    if (request.sshEnabled) {
        if (request.sshPasswordAuth || keys.isEmpty())
            appendLine(&cloud, QStringLiteral("ssh_pwauth: true"));
        else
            appendLine(&cloud, QStringLiteral("ssh_pwauth: false"));
    }

    const bool serialOn = !request.serial.isEmpty() && request.serial != QLatin1String("Disabled");
    if (withInterfaces && (request.enableI2C || request.enableSPI || request.enable1Wire
                           || request.enableUsbGadget || serialOn)) {
        appendLine(&cloud, QStringLiteral("rpi:"));
        if (request.enableUsbGadget)
            appendLine(&cloud, QStringLiteral("  enable_usb_gadget: true"));
        if (request.enableI2C || request.enableSPI || request.enable1Wire || serialOn) {
            appendLine(&cloud, QStringLiteral("  interfaces:"));
            if (request.enableI2C)
                appendLine(&cloud, QStringLiteral("    i2c: true"));
            if (request.enableSPI)
                appendLine(&cloud, QStringLiteral("    spi: true"));
            if (request.enable1Wire)
                appendLine(&cloud, QStringLiteral("    onewire: true"));
            if (serialOn) {
                if (request.serial == QLatin1String("Default")) {
                    appendLine(&cloud, QStringLiteral("    serial: true"));
                } else {
                    const bool console = request.serial.contains(QLatin1String("Console"));
                    const bool hardware = request.serial.contains(QLatin1String("Hardware"));
                    appendLine(&cloud, QStringLiteral("    serial:"));
                    appendLine(&cloud, QStringLiteral("      console: ") + (console ? QStringLiteral("true") : QStringLiteral("false")));
                    appendLine(&cloud, QStringLiteral("      hardware: ") + (hardware ? QStringLiteral("true") : QStringLiteral("false")));
                }
            }
        }
    }

    const bool needSsh = request.sshEnabled;
    const bool needSudo = request.passwordlessSudo && hasUser;
    const bool needConnect = request.connectEnabled && !request.connectToken.trimmed().isEmpty() && hasUser;
    if (needSsh || needSudo || needConnect) {
        appendLine(&cloud, QStringLiteral("runcmd:"));
        if (needSsh)
            appendLine(&cloud, QStringLiteral("  - [ systemctl, enable, --now, ssh ]"));
        if (needSudo) {
            const QString sudoers = QStringLiteral("/etc/sudoers.d/010_") + user + QStringLiteral("-nopasswd");
            runcmd(&cloud, QStringLiteral("echo ")
                   + shellQuote(user + QStringLiteral(" ALL=(ALL) NOPASSWD:ALL"))
                   + QStringLiteral(" >") + shellQuote(sudoers));
            runcmd(&cloud, QStringLiteral("chmod 0440 ") + shellQuote(sudoers));
        }
        if (needConnect) {
            const QString token = stripNewlines(request.connectToken.trimmed());
            const QString dir = QStringLiteral("/home/") + user + QStringLiteral("/.config/com.raspberrypi.connect");
            const QString keyPath = dir + QStringLiteral("/auth.key");
            const QString quotedUser = shellQuote(user);
            runcmd(&cloud, QStringLiteral("install -o ") + quotedUser + QStringLiteral(" -m 700 -d ") + shellQuote(dir));
            runcmd(&cloud, QStringLiteral("printf '%s\n' ") + shellQuote(token) + QStringLiteral(" > ")
                   + shellQuote(keyPath) + QStringLiteral(" && chown ") + shellQuote(user + QStringLiteral(":") + user)
                   + QStringLiteral(" ") + shellQuote(keyPath) + QStringLiteral(" && chmod 600 ") + shellQuote(keyPath));
            runcmd(&cloud, QStringLiteral("loginctl enable-linger ") + quotedUser + QStringLiteral(" 2>/dev/null || true"));
        }
    }

    if (cloud.isEmpty())
        return {};
    cloud.prepend("manage_resolv_conf: false\n\n");
    cloud.prepend("#cloud-config\n");
    return cloud;
}

QByteArray cloudInitNetwork(const CustomRequest &request, const QString &psk) {
    const QString ssid = request.wifiSsid.trimmed();
    if (ssid.isEmpty())
        return {};
    const QString country = countryCode(request.wifiCountry);
    QByteArray net;
    appendLine(&net, QStringLiteral("network:"));
    appendLine(&net, QStringLiteral("  version: 2"));
    appendLine(&net, QStringLiteral("  ethernets:"));
    appendLine(&net, QStringLiteral("    eth0:"));
    appendLine(&net, QStringLiteral("      dhcp4: true"));
    appendLine(&net, QStringLiteral("      dhcp6: true"));
    appendLine(&net, QStringLiteral("      optional: true"));
    appendLine(&net, QStringLiteral("  wifis:"));
    appendLine(&net, QStringLiteral("    wlan0:"));
    appendLine(&net, QStringLiteral("      dhcp4: true"));
    if (!country.isEmpty())
        appendLine(&net, QStringLiteral("      regulatory-domain: ") + yamlQuote(country));
    appendLine(&net, QStringLiteral("      access-points:"));
    appendLine(&net, QStringLiteral("        ") + yamlQuote(ssid) + QStringLiteral(":"));
    if (request.wifiHidden)
        appendLine(&net, QStringLiteral("          hidden: true"));
    if (request.wifiOpen || psk.isEmpty()) {
        appendLine(&net, QStringLiteral("          auth:"));
        appendLine(&net, QStringLiteral("            key-management: none"));
    } else {
        appendLine(&net, QStringLiteral("          password: ") + yamlQuote(psk));
    }
    appendLine(&net, QStringLiteral("      optional: true"));
    return net;
}

QByteArray preseedToml(const CustomRequest &request, const QString &passwordHash, const QString &psk) {
    QByteArray body;
    const QString hostname = stripNewlines(request.hostname.trimmed());
    const QString user = request.username.trimmed();
    const QString country = countryCode(request.wifiCountry);
    const QString ssid = request.wifiSsid.trimmed();
    const QStringList keys = sshKeyLines(request.sshKeys);
    const QString timezone = request.timezone.trimmed();
    const QString keyboard = request.keyboard.trimmed();

    if (!hostname.isEmpty()) {
        appendLine(&body, QStringLiteral("[system]"));
        appendLine(&body, QStringLiteral("hostname = ") + tomlQuote(hostname));
        appendLine(&body, QString());
    }
    if (!user.isEmpty()) {
        appendLine(&body, QStringLiteral("[user]"));
        appendLine(&body, QStringLiteral("name = ") + tomlQuote(user));
        if (!passwordHash.isEmpty()) {
            appendLine(&body, QStringLiteral("password = ") + tomlQuote(passwordHash));
            appendLine(&body, QStringLiteral("password_encrypted = true"));
        }
        appendLine(&body, QStringLiteral("groups = [\"sudo\"]"));
        if (request.passwordlessSudo)
            appendLine(&body, QStringLiteral("passwordless_sudo = true"));
        appendLine(&body, QString());
    }
    if (request.sshEnabled) {
        appendLine(&body, QStringLiteral("[ssh]"));
        appendLine(&body, QStringLiteral("enabled = true"));
        appendLine(&body, QStringLiteral("password_authentication = ")
                   + ((request.sshPasswordAuth || keys.isEmpty()) ? QStringLiteral("true") : QStringLiteral("false")));
        if (!keys.isEmpty()) {
            appendLine(&body, QStringLiteral("authorized_keys = ["));
            for (const QString &key : keys)
                appendLine(&body, QStringLiteral("  ") + tomlQuote(key) + QStringLiteral(","));
            appendLine(&body, QStringLiteral("]"));
        }
        appendLine(&body, QString());
    }
    if (!ssid.isEmpty()) {
        appendLine(&body, QStringLiteral("[wlan]"));
        appendLine(&body, QStringLiteral("ssid = ") + tomlQuote(ssid));
        if (!request.wifiOpen && !psk.isEmpty()) {
            appendLine(&body, QStringLiteral("password = ") + tomlQuote(psk));
            appendLine(&body, QStringLiteral("password_encrypted = true"));
        }
        appendLine(&body, QStringLiteral("hidden = ") + (request.wifiHidden ? QStringLiteral("true") : QStringLiteral("false")));
        if (!country.isEmpty())
            appendLine(&body, QStringLiteral("country = ") + tomlQuote(country));
        appendLine(&body, QString());
    }
    if (!timezone.isEmpty() || !keyboard.isEmpty()) {
        appendLine(&body, QStringLiteral("[locale]"));
        if (!keyboard.isEmpty())
            appendLine(&body, QStringLiteral("keymap = ") + tomlQuote(keyboard));
        if (!timezone.isEmpty())
            appendLine(&body, QStringLiteral("timezone = ") + tomlQuote(timezone));
        appendLine(&body, QString());
    }
    if (request.connectEnabled) {
        const QString token = stripNewlines(request.connectToken.trimmed());
        appendLine(&body, QStringLiteral("[connect]"));
        appendLine(&body, QStringLiteral("enabled = true"));
        if (token.isEmpty()) {
            appendLine(&body, QStringLiteral("mode = \"device-identity\""));
        } else {
            appendLine(&body, QStringLiteral("mode = \"token\""));
            appendLine(&body, QStringLiteral("token = ") + tomlQuote(token));
        }
        appendLine(&body, QString());
    }

    QString serialValue;
    if (request.serial == QLatin1String("Default"))
        serialValue = QStringLiteral("default");
    else if (request.serial == QLatin1String("Console"))
        serialValue = QStringLiteral("console");
    else if (request.serial == QLatin1String("Hardware"))
        serialValue = QStringLiteral("hardware");
    else if (request.serial == QLatin1String("Console & Hardware"))
        serialValue = QStringLiteral("console_hardware");
    if (request.enableI2C || request.enableSPI || request.enable1Wire || request.enableUsbGadget
        || !serialValue.isEmpty()) {
        appendLine(&body, QStringLiteral("[interfaces]"));
        if (request.enableI2C)
            appendLine(&body, QStringLiteral("i2c = true"));
        if (request.enableSPI)
            appendLine(&body, QStringLiteral("spi = true"));
        if (request.enable1Wire)
            appendLine(&body, QStringLiteral("onewire = true"));
        if (request.enableUsbGadget)
            appendLine(&body, QStringLiteral("usb_gadget = true"));
        if (!serialValue.isEmpty())
            appendLine(&body, QStringLiteral("serial = ") + tomlQuote(serialValue));
        appendLine(&body, QString());
    }

    if (body.isEmpty())
        return {};
    QByteArray out = "config_version = \"1.0\"\n\n";
    out += body;
    while (out.endsWith("\n\n"))
        out.chop(1);
    return out;
}

QByteArray regdomAppend(const QString &country) {
    const QString code = countryCode(country);
    if (code.isEmpty())
        return {};
    return " cfg80211.ieee80211_regdom=" + code.toUtf8();
}

} // namespace

bool initFormatSupportsCustomisation(const QString &initFormat) {
    return initFormat == QLatin1String("systemd")
        || initFormat == QLatin1String("cloudinit")
        || initFormat == QLatin1String("cloudinit-rpi")
        || initFormat == QLatin1String("rpi-preseed");
}

bool initFormatSupportsInterfaces(const QString &initFormat) {
    return initFormat == QLatin1String("cloudinit-rpi")
        || initFormat == QLatin1String("rpi-preseed");
}

QString describeInitFormat(const QString &initFormat) {
    if (initFormat == QLatin1String("cloudinit-rpi"))
        return QCoreApplication::translate("Customise", "Raspberry Pi OS (cloud-init)");
    if (initFormat == QLatin1String("cloudinit"))
        return QCoreApplication::translate("Customise", "cloud-init");
    if (initFormat == QLatin1String("systemd"))
        return QCoreApplication::translate("Customise", "Raspberry Pi OS Legacy (firstrun)");
    if (initFormat == QLatin1String("rpi-preseed"))
        return QCoreApplication::translate("Customise", "rpi-preseed");
    return QCoreApplication::translate("Customise", "No first-boot setup");
}

CustomResult buildCustomisation(const CustomRequest &request) {
    CustomResult result;
    if (!initFormatSupportsCustomisation(request.initFormat))
        return result;

    bool any = false;
    result.error = validate(request, &any);
    if (!result.error.isEmpty() || !any)
        return result;

    const QString passwordHash = request.password.isEmpty()
        ? QString()
        : cryptHash(request.password, request.releaseDate);
    if (!request.password.isEmpty() && passwordHash.isEmpty()) {
        result.error = QCoreApplication::translate("Customise", "The password could not be hashed.");
        return result;
    }
    const QString psk = request.wifiSsid.trimmed().isEmpty() || request.wifiOpen
        ? QString()
        : wifiPsk(request.wifiSsid.trimmed(), request.wifiPassword);

    const QString instance = QStringLiteral("omaimage-")
        + QString::number(QDateTime::currentMSecsSinceEpoch());

    if (request.initFormat == QLatin1String("systemd")) {
        result.files.insert(QStringLiteral("firstrun.sh"), systemdScript(request, passwordHash, psk));
        result.cmdlineAppend = " systemd.run=/boot/firstrun.sh"
                                " systemd.run_success_action=reboot"
                                " systemd.unit=kernel-command-line.target";
        result.cmdlineAppend += regdomAppend(request.wifiCountry);
    } else if (request.initFormat == QLatin1String("rpi-preseed")) {
        const QByteArray toml = preseedToml(request, passwordHash, psk);
        if (!toml.isEmpty())
            result.files.insert(QStringLiteral("rpi-preseed.toml"), toml);
        result.cmdlineAppend = regdomAppend(request.wifiCountry);
    } else {
        const bool interfaces = request.initFormat == QLatin1String("cloudinit-rpi");
        const QByteArray userData = cloudInitUserData(request, passwordHash, interfaces);
        const QByteArray network = cloudInitNetwork(request, psk);
        if (!userData.isEmpty())
            result.files.insert(QStringLiteral("user-data"), userData);
        if (!network.isEmpty())
            result.files.insert(QStringLiteral("network-config"), network);
        if (!userData.isEmpty() || !network.isEmpty()) {
            result.files.insert(QStringLiteral("meta-data"),
                                "instance-id: " + instance.toUtf8() + "\n");
            result.cmdlineAppend = " ds=nocloud;i=" + instance.toUtf8();
        }
        result.cmdlineAppend += regdomAppend(request.wifiCountry);
    }
    return result;
}

bool customiseSelfTest(QString *error) {
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    CustomRequest request;
    request.initFormat = QStringLiteral("cloudinit-rpi");
    request.releaseDate = QStringLiteral("2026-09-15");
    request.hostname = QStringLiteral("pi-box");
    request.username = QStringLiteral("ada");
    request.password = QStringLiteral("S3cret-Passw0rd");
    request.locale = QStringLiteral("de_DE.UTF-8");
    request.timezone = QStringLiteral("Europe/Berlin");
    request.keyboard = QStringLiteral("de");
    request.wifiSsid = QStringLiteral("Heimnetz");
    request.wifiPassword = QStringLiteral("wlan-secret-99");
    request.wifiCountry = QStringLiteral("de");
    request.wifiHidden = true;
    request.sshEnabled = true;
    request.sshPasswordAuth = false;
    request.sshKeys = QStringLiteral("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITestKey ada@host");
    request.passwordlessSudo = false;
    request.enableI2C = true;
    request.enableSPI = true;
    request.serial = QStringLiteral("Hardware");
    request.connectEnabled = true;

    const CustomResult cloud = buildCustomisation(request);
    if (!cloud.error.isEmpty())
        return fail(cloud.error);
    const QByteArray userData = cloud.files.value(QStringLiteral("user-data"));
    const QByteArray network = cloud.files.value(QStringLiteral("network-config"));
    const QByteArray meta = cloud.files.value(QStringLiteral("meta-data"));
    if (!userData.startsWith("#cloud-config\n"))
        return fail(QStringLiteral("user-data is missing the cloud-config header."));
    if (!userData.contains("hostname: \"pi-box\"") || !userData.contains("name: \"ada\"")
        || !userData.contains("timezone: \"Europe/Berlin\"") || !userData.contains("layout: \"de\"")
        || !userData.contains("locale: \"de_DE.UTF-8\"") || !userData.contains("i2c: true")
        || !userData.contains("ssh_pwauth: false") || !userData.contains("sudo: null"))
        return fail(QStringLiteral("user-data is incomplete."));
    if (userData.contains("S3cret-Passw0rd") || network.contains("wlan-secret-99"))
        return fail(QStringLiteral("Plaintext secret in cloud-init."));
    if (!userData.contains("passwd: \"$y$") && !userData.contains("passwd: \"$5$"))
        return fail(QStringLiteral("Password hash is missing."));
    if (!network.contains("regulatory-domain: \"DE\"") || !network.contains("hidden: true")
        || !network.contains("Heimnetz") || !network.contains("eth0:"))
        return fail(QStringLiteral("network-config is incomplete."));
    if (!cloud.cmdlineAppend.contains("ds=nocloud;i=omaimage-")
        || !cloud.cmdlineAppend.contains("cfg80211.ieee80211_regdom=DE"))
        return fail(QStringLiteral("cloud-init cmdline is incomplete."));
    const int marker = meta.indexOf("instance-id: ");
    if (marker < 0 || !cloud.cmdlineAppend.contains(meta.mid(marker + 13).trimmed()))
        return fail(QStringLiteral("instance-id does not match."));

    request.initFormat = QStringLiteral("systemd");
    request.releaseDate = QStringLiteral("2022-01-01");
    request.hostname = QStringLiteral("pi-box");
    request.sshKeys = QStringLiteral("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITestKey $(reboot)");
    const CustomResult legacy = buildCustomisation(request);
    const QByteArray script = legacy.files.value(QStringLiteral("firstrun.sh"));
    if (!script.contains("set_hostname") || !script.contains("userconf") || !script.contains("set_wlan")
        || !script.contains("set_keymap") || !script.contains("set_timezone")
        || !script.contains("rm -f /boot/firstrun.sh"))
        return fail(QStringLiteral("firstrun.sh is incomplete."));
    if (!script.contains("enable_ssh -k 'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITestKey $(reboot)'"))
        return fail(QStringLiteral("The SSH key is not safely quoted in the script."));
    if (script.contains("S3cret-Passw0rd") || !script.contains("IMAGER_PASS='$5$"))
        return fail(QStringLiteral("Legacy password hash does not match."));
    if (!legacy.cmdlineAppend.contains("systemd.run=/boot/firstrun.sh"))
        return fail(QStringLiteral("systemd.run is missing."));

    request.initFormat = QStringLiteral("rpi-preseed");
    request.releaseDate = QStringLiteral("2026-09-15");
    request.hostname = QStringLiteral("pi-box");
    request.enableUsbGadget = true;
    const CustomResult preseed = buildCustomisation(request);
    const QByteArray toml = preseed.files.value(QStringLiteral("rpi-preseed.toml"));
    if (!toml.contains("config_version") || !toml.contains("[system]") || !toml.contains("[user]")
        || !toml.contains("password_encrypted = true") || !toml.contains("[wlan]")
        || !toml.contains("[locale]") || !toml.contains("[ssh]") || !toml.contains("[interfaces]")
        || !toml.contains("[connect]") || !toml.contains("usb_gadget = true"))
        return fail(QStringLiteral("rpi-preseed.toml is incomplete."));
    if (toml.contains("S3cret-Passw0rd") || toml.contains("wlan-secret-99"))
        return fail(QStringLiteral("Plaintext in the preseed."));

    CustomRequest invalid;
    invalid.initFormat = QStringLiteral("cloudinit-rpi");
    invalid.username = QStringLiteral("Ada");
    invalid.password = QStringLiteral("secret");
    if (buildCustomisation(invalid).error.isEmpty())
        return fail(QStringLiteral("An invalid username was accepted."));

    CustomRequest empty;
    empty.initFormat = QStringLiteral("none");
    empty.username = QStringLiteral("ada");
    if (!buildCustomisation(empty).files.isEmpty())
        return fail(QStringLiteral("Format none produced files."));
    return true;
}
