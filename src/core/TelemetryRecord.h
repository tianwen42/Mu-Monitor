#pragma once

#include <QDateTime>
#include <QString>

struct TelemetryRecord
{
    QString deviceId;
    QString name;
    QString status;
    double temperature = 0.0;
    double pressure = 0.0;
    double speed = 0.0;
    double voltage = 0.0;
    QDateTime updatedAt;
};
