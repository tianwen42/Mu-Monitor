#pragma once

#include <QString>
#include <QtGlobal>

class Protocol final
{
public:
    enum Version : quint8 {
        Version1 = 1,
    };

    enum class MessageType : quint8 {
        Telemetry = 0x01,
        Heartbeat = 0x02,
        Command = 0x03,
        Response = 0x04,
        Error = 0x7f,
    };

    static constexpr quint16 kMagic = 0x4d55;
    static constexpr int kMagicSize = 2;
    static constexpr int kVersionSize = 1;
    static constexpr int kMessageTypeSize = 1;
    static constexpr int kLengthSize = 2;
    static constexpr int kSequenceSize = 4;
    static constexpr int kDeviceIdSize = 32;
    static constexpr int kTimestampSize = 8;
    static constexpr int kCrcSize = 2;
    static constexpr int kHeaderSize = kMagicSize + kVersionSize + kMessageTypeSize
                                        + kLengthSize + kSequenceSize + kDeviceIdSize
                                        + kTimestampSize;
    static constexpr int kMaxPayloadSize = 4096;
    static constexpr int kMinFrameSize = kHeaderSize + kCrcSize;
    static constexpr int kMaxFrameSize = kHeaderSize + kMaxPayloadSize + kCrcSize;

    static bool isSupportedVersion(quint8 version);
    static bool isKnownMessageType(quint8 messageType);
    static QString messageTypeName(MessageType messageType);
};

QString protocolVersionName(quint8 version);
QString protocolMessageTypeName(quint8 messageType);
