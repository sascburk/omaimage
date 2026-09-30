#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

struct BlockDevice {
    QString path;
    QString name;
    QString model;
    QString tran;
    QString fstype;
    QString type;
    qint64 size = 0;
    bool removable = false;
    bool readOnly = false;
    bool hotplug = false;
    bool system = false;
    QStringList mountpoints;
    QVector<BlockDevice> children;
};

QVector<BlockDevice> listBlockDevices(QString *error);
QVector<BlockDevice> writableDrives(bool includeInternal, QString *error);
QString formatBytes(qint64 bytes);
bool drivesSelfTest(QString *error);
