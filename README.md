# Omaimage

Schlanke Qt-Quick-App im Stil von Omawrite und Omacalc. Sie schreibt ISO- und Image-Dateien auf einen USB-Stick und richtet Raspberry-Pi-Abbilder so ein, dass der Pi beim ersten Start direkt hochfährt.

Die Liste kommt von `https://downloads.raspberrypi.com/os_list_imagingutility_v4.json`, also dieselben aktuellen Abbilder wie im Raspberry Pi Imager. Weitere Katalog-URLs im selben JSON-Format und einzelne Download-Links lassen sich in der App hinzufügen.

Für Raspberry-Pi-OS stehen Benutzer, Passwort, WLAN, Land, Tastatur, Zeitzone, Locale, SSH, Hostname, Schnittstellen und Raspberry Pi Connect zur Verfügung. Aktuelle Abbilder bekommen cloud-init (`user-data`, `network-config`), ältere ein `firstrun.sh`.

## Bauen

Abhängigkeiten auf Omarchy: `qt6-base`, `qt6-declarative`, `qt6-quickcontrols2`, `qt6-svg`, `xz`, `gzip`, `zstd`, `libarchive` (`bsdtar`), `util-linux`, `polkit`.

```bash
qmake6 omaimage.pro
make -j"$(nproc)"
./omaimage --self-test
./bin/install.sh
```

`install.sh` legt die App unter `~/.local/bin/omaimage` ab und trägt einen Starter ein.

Schreiben auf den Stick startet `pkexec` und fragt nach dem Administratorpasswort. Die Systemplatte wird nicht angeboten.

## Quellen

Eigene Kataloge müssen ein `os_list` enthalten, wie es der Raspberry Pi Imager erwartet. Eine einzelne URL zeigt auf eine `.img`, `.iso`, `.xz`, `.gz`, `.zip` oder `.zst` Datei. Bei lokalen Dateien und eigenen URLs lässt sich das Einrichtungsformat wählen; für aktuelle Raspberry-Pi-OS-Abbilder ist das „Raspberry Pi OS (cloud-init)“.
