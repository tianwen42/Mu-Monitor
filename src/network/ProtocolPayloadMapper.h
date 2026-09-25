#pragma once

#include "core/HeartbeatRecord.h"
#include "core/TelemetrySample.h"

#include <QByteArray>
#include <QString>

class ProtocolPayloadMapper final
{
public:
    static bool toTelemetrySample(const QString &deviceId, qint64 timestampUtcMs,
                                  const QByteArray &payload,
                                  TelemetrySample *sample,
                                  QString *errorMessage = nullptr);
    static bool toHeartbeatRecord(const QString &deviceId, qint64 timestampUtcMs,
                                  const QByteArray &payload,
                                  HeartbeatRecord *record,
                                  QString *errorMessage = nullptr);
};