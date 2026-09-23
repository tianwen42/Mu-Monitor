#include "FrameCodec.h"

#include "CRC16.h"
#include "Protocol.h"


namespace {

void appendUInt16(QByteArray &output, quint16 value)
{
    output.append(static_cast<char>((value >> 8) & 0xff));
    output.append(static_cast<char>(value & 0xff));
}

void appendUInt32(QByteArray &output, quint32 value)
{
    output.append(static_cast<char>((value >> 24) & 0xff));
    output.append(static_cast<char>((value >> 16) & 0xff));
    output.append(static_cast<char>((value >> 8) & 0xff));
    output.append(static_cast<char>(value & 0xff));
}

void appendInt64(QByteArray &output, qint64 value)
{
    const quint64 unsignedValue = static_cast<quint64>(value);
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.append(static_cast<char>((unsignedValue >> shift) & 0xff));
    }
}

quint16 readUInt16(const QByteArray &data, int offset)
{
    return static_cast<quint16>(
        (static_cast<quint8>(data.at(offset)) << 8)
        | static_cast<quint8>(data.at(offset + 1)));
}

quint32 readUInt32(const QByteArray &data, int offset)
{
    return (static_cast<quint32>(static_cast<quint8>(data.at(offset))) << 24)
           | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 16)
           | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 8)
           | static_cast<quint32>(static_cast<quint8>(data.at(offset + 3)));
}

qint64 readInt64(const QByteArray &data, int offset)
{
    quint64 value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<quint8>(data.at(offset + i));
    }
    return static_cast<qint64>(value);
}

bool decodeDeviceId(const QByteArray &rawDeviceId, QString *deviceId, QString *errorMessage)
{
    if (rawDeviceId.size() != Protocol::kDeviceIdSize) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("设备 ID 字段长度不是 %1 字节")
                                .arg(Protocol::kDeviceIdSize);
        }
        return false;
    }

    const int terminator = rawDeviceId.indexOf('\0');
    const QByteArray encodedId = terminator >= 0 ? rawDeviceId.left(terminator) : rawDeviceId;
    if (encodedId.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("设备 ID 不能为空");
        }
        return false;
    }

    if (terminator >= 0) {
        for (int i = terminator + 1; i < rawDeviceId.size(); ++i) {
            if (rawDeviceId.at(i) != '\0') {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("设备 ID 的零填充区域包含非零字节");
                }
                return false;
            }
        }
    }

    const QString decoded = QString::fromUtf8(encodedId);
    if (decoded.toUtf8() != encodedId) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("设备 ID 不是合法 UTF-8");
        }
        return false;
    }

    if (deviceId) {
        *deviceId = decoded;
    }
    return true;
}

bool encodeDeviceId(const QString &deviceId, QByteArray *encodedDeviceId, QString *errorMessage)
{
    const QByteArray encoded = deviceId.toUtf8();
    if (encoded.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("设备 ID 不能为空");
        }
        return false;
    }
    if (encoded.size() > Protocol::kDeviceIdSize) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("设备 ID 不能超过 %1 字节")
                                .arg(Protocol::kDeviceIdSize);
        }
        return false;
    }
    if (encoded.contains('\0')) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("设备 ID 不能包含 NUL 字节");
        }
        return false;
    }

    QByteArray padded(Protocol::kDeviceIdSize, '\0');
    padded.replace(0, encoded.size(), encoded);
    if (encodedDeviceId) {
        *encodedDeviceId = padded;
    }
    return true;
}

QByteArray magicBytes()
{
    QByteArray magic(Protocol::kMagicSize, Qt::Uninitialized);
    magic[0] = static_cast<char>((Protocol::kMagic >> 8) & 0xff);
    magic[1] = static_cast<char>(Protocol::kMagic & 0xff);
    return magic;
}

} // namespace

QByteArray FrameCodec::encode(const Frame &frame, QString *errorMessage)
{
    if (!Protocol::isSupportedVersion(frame.version)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("不支持的协议版本：%1").arg(frame.version);
        }
        return {};
    }

    if (!Protocol::isKnownMessageType(static_cast<quint8>(frame.messageType))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未知消息类型：0x%1")
                                .arg(static_cast<quint8>(frame.messageType), 2, 16, QLatin1Char('0'));
        }
        return {};
    }

    QByteArray encodedDeviceId;
    if (!encodeDeviceId(frame.deviceId, &encodedDeviceId, errorMessage)) {
        return {};
    }

    if (frame.payload.size() > Protocol::kMaxPayloadSize) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("负载长度 %1 超过上限 %2")
                                .arg(frame.payload.size())
                                .arg(Protocol::kMaxPayloadSize);
        }
        return {};
    }

    const int frameSize = Protocol::kHeaderSize + frame.payload.size() + Protocol::kCrcSize;
    QByteArray encoded;
    encoded.reserve(frameSize);
    encoded.append(magicBytes());
    encoded.append(static_cast<char>(frame.version));
    encoded.append(static_cast<char>(static_cast<quint8>(frame.messageType)));
    appendUInt16(encoded, static_cast<quint16>(frameSize));
    appendUInt32(encoded, frame.sequence);
    encoded.append(encodedDeviceId);
    appendInt64(encoded, frame.timestampUtcMs);
    encoded.append(frame.payload);
    appendUInt16(encoded, CRC16::compute(encoded));

    if (errorMessage) {
        errorMessage->clear();
    }
    return encoded;
}

DecodeResult FrameCodec::decode(const QByteArray &data)
{
    DecodeResult result;

    if (data.size() < Protocol::kMagicSize) {
        result.status = DecodeStatus::NeedMoreData;
        return result;
    }

    if (static_cast<quint8>(data.at(0)) != ((Protocol::kMagic >> 8) & 0xff)
        || static_cast<quint8>(data.at(1)) != (Protocol::kMagic & 0xff)) {
        result.status = DecodeStatus::InvalidMagic;
        return result;
    }

    if (data.size() < Protocol::kMagicSize + Protocol::kVersionSize) {
        result.status = DecodeStatus::NeedMoreData;
        return result;
    }

    const quint8 version = static_cast<quint8>(data.at(2));
    if (!Protocol::isSupportedVersion(version)) {
        result.status = DecodeStatus::UnsupportedVersion;
        return result;
    }

    if (data.size() < Protocol::kMagicSize + Protocol::kVersionSize
                           + Protocol::kMessageTypeSize) {
        result.status = DecodeStatus::NeedMoreData;
        return result;
    }

    const quint8 messageType = static_cast<quint8>(data.at(3));
    if (!Protocol::isKnownMessageType(messageType)) {
        result.status = DecodeStatus::UnknownMessageType;
        return result;
    }

    if (data.size() < Protocol::kMagicSize + Protocol::kVersionSize
                           + Protocol::kMessageTypeSize + Protocol::kLengthSize) {
        result.status = DecodeStatus::NeedMoreData;
        return result;
    }

    const int frameSize = readUInt16(data, 4);
    if (frameSize < Protocol::kMinFrameSize) {
        result.status = DecodeStatus::InvalidLength;
        return result;
    }
    if (frameSize > Protocol::kMaxFrameSize) {
        result.status = DecodeStatus::FrameTooLarge;
        return result;
    }
    if (data.size() < frameSize) {
        result.status = DecodeStatus::NeedMoreData;
        return result;
    }

    const quint16 expectedCrc = readUInt16(data, frameSize - Protocol::kCrcSize);
    const quint16 actualCrc = CRC16::compute(data.constData(), frameSize - Protocol::kCrcSize);
    if (actualCrc != expectedCrc) {
        result.status = DecodeStatus::CrcMismatch;
        return result;
    }

    Frame frame;
    frame.version = version;
    frame.messageType = static_cast<Protocol::MessageType>(messageType);
    frame.sequence = readUInt32(data, 6);
    if (!decodeDeviceId(data.mid(10, Protocol::kDeviceIdSize),
                        &frame.deviceId,
                        nullptr)) {
        result.status = DecodeStatus::InvalidDeviceId;
        return result;
    }
    frame.timestampUtcMs = readInt64(data, 42);
    frame.payload = data.mid(Protocol::kHeaderSize,
                             frameSize - Protocol::kHeaderSize - Protocol::kCrcSize);

    result.status = DecodeStatus::Ok;
    result.frame = frame;
    result.consumedBytes = frameSize;
    return result;
}

QString FrameCodec::statusText(DecodeStatus status)
{
    switch (status) {
    case DecodeStatus::Ok:
        return QStringLiteral("帧有效");
    case DecodeStatus::NeedMoreData:
        return QStringLiteral("需要更多数据");
    case DecodeStatus::InvalidMagic:
        return QStringLiteral("帧头不匹配");
    case DecodeStatus::UnsupportedVersion:
        return QStringLiteral("不支持的协议版本");
    case DecodeStatus::UnknownMessageType:
        return QStringLiteral("未知消息类型");
    case DecodeStatus::InvalidLength:
        return QStringLiteral("帧长度小于最小值");
    case DecodeStatus::FrameTooLarge:
        return QStringLiteral("帧长度超过上限");
    case DecodeStatus::InvalidDeviceId:
        return QStringLiteral("设备 ID 无效");
    case DecodeStatus::CrcMismatch:
        return QStringLiteral("CRC16 校验失败");
    }
    return QStringLiteral("未知解码状态");
}
