#pragma once

#include "Protocol.h"

#include <QByteArray>
#include <QMetaType>
#include <QString>

struct Frame
{
    quint8 version = Protocol::Version1;
    Protocol::MessageType messageType = Protocol::MessageType::Telemetry;
    quint32 sequence = 0;
    QString deviceId;
    qint64 timestampUtcMs = 0;
    QByteArray payload;
};

Q_DECLARE_METATYPE(Frame)
