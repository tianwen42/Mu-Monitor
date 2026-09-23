#pragma once

#include <QByteArray>
#include <QString>

class FrameEncoder final
{
public:
    static QByteArray encodeTelemetry(const QString &deviceId,
                                      quint32 sequence,
                                      const QByteArray &payload,
                                      QString *errorMessage = nullptr);
};
