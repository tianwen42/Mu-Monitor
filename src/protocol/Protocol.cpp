#include "Protocol.h"

bool Protocol::isSupportedVersion(quint8 version)
{
    return version == Version1;
}

bool Protocol::isKnownMessageType(quint8 messageType)
{
    switch (static_cast<MessageType>(messageType)) {
    case MessageType::Telemetry:
    case MessageType::Heartbeat:
    case MessageType::Command:
    case MessageType::Response:
    case MessageType::Error:
        return true;
    }
    return false;
}

QString Protocol::messageTypeName(MessageType messageType)
{
    switch (messageType) {
    case MessageType::Telemetry:
        return QStringLiteral("telemetry");
    case MessageType::Heartbeat:
        return QStringLiteral("heartbeat");
    case MessageType::Command:
        return QStringLiteral("command");
    case MessageType::Response:
        return QStringLiteral("response");
    case MessageType::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

QString protocolVersionName(quint8 version)
{
    if (version == Protocol::Version1) {
        return QStringLiteral("v1");
    }
    return QStringLiteral("v%1").arg(version);
}

QString protocolMessageTypeName(quint8 messageType)
{
    if (!Protocol::isKnownMessageType(messageType)) {
        return QStringLiteral("unknown(0x%1)")
            .arg(messageType, 2, 16, QLatin1Char('0'));
    }
    return Protocol::messageTypeName(static_cast<Protocol::MessageType>(messageType));
}
