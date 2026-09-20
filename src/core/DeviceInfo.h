#pragma once

#include <QDateTime>
#include <QString>

struct DeviceInfo
{
    QString deviceId;
    QString name;
    QString model;
    QString location;
    QString ipAddress;
    QString protocol;
    QString notes;
};

struct AlarmRecord
{
    QString deviceId;
    QString level;
    QString message;
    QDateTime occurredAt;
};
