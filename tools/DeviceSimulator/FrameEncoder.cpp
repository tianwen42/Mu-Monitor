#include "FrameEncoder.h"

#include <protocol/FrameCodec.h>
#include <protocol/Frame.h>

#include <QDateTime>

QByteArray FrameEncoder::encodeTelemetry(const QString &deviceId,
                                         quint32 sequence,
                                         const QByteArray &payload,
                                         QString *errorMessage)
{
    Frame frame;
    frame.version = Protocol::Version1;
    frame.messageType = Protocol::MessageType::Telemetry;
    frame.sequence = sequence;
    frame.deviceId = deviceId;
    frame.timestampUtcMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    frame.payload = payload;
    return FrameCodec::encode(frame, errorMessage);
}

QByteArray FrameEncoder::encodeTelemetryWithBadCrc(const QString &deviceId,
                                                   quint32 sequence,
                                                   const QByteArray &payload,
                                                   QString *errorMessage)
{
    QByteArray encoded = encodeTelemetry(deviceId, sequence, payload, errorMessage);
    if (!encoded.isEmpty()) {
        encoded[encoded.size() - 1] = static_cast<char>(
            static_cast<quint8>(encoded.at(encoded.size() - 1)) ^ 0x01);
    }
    return encoded;
}
