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
