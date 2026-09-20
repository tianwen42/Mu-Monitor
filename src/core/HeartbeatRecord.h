#pragma once

#include <QDateTime>
#include <QString>

struct HeartbeatRecord
{
    QString deviceId;
    QDateTime heartbeatAt;
    bool online = false;
    bool collecting = false;
    int latencyMs = -1;
};