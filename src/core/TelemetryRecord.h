#pragma once

#include "core/BusinessStates.h"

#include <QDateTime>
#include <QString>

struct TelemetryRecord
{
    QString deviceId;
    QString name;
    TelemetryStatus status = TelemetryStatus::Offline;
    double temperature = 0.0;
    double pressure = 0.0;
    double speed = 0.0;
    double voltage = 0.0;
    QDateTime updatedAt;
};
