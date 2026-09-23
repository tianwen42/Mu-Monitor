#pragma once

#include "Frame.h"

#include <QByteArray>
#include <QString>

enum class DecodeStatus {
    Ok,
    NeedMoreData,
    InvalidMagic,
    UnsupportedVersion,
    UnknownMessageType,
    InvalidLength,
    FrameTooLarge,
    InvalidDeviceId,
    CrcMismatch,
};

struct DecodeResult
{
    DecodeStatus status = DecodeStatus::NeedMoreData;
    Frame frame;
    int consumedBytes = 0;

    bool isOk() const { return status == DecodeStatus::Ok; }
};

class FrameCodec final
{
public:
    static QByteArray encode(const Frame &frame, QString *errorMessage = nullptr);
    static DecodeResult decode(const QByteArray &data);
    static QString statusText(DecodeStatus status);
};
