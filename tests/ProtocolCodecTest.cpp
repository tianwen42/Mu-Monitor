#include <network/FrameDecoder.h>
#include <protocol/CRC16.h>
#include <protocol/FrameCodec.h>
#include <protocol/Protocol.h>

#include <QtTest>

namespace {

Frame makeFrame(quint32 sequence = 42)
{
    Frame frame;
    frame.version = Protocol::Version1;
    frame.messageType = Protocol::MessageType::Telemetry;
    frame.sequence = sequence;
    frame.deviceId = QStringLiteral("DEV-001");
    frame.timestampUtcMs = 1727000000123LL;
    frame.payload = QByteArrayLiteral("{\"temperature\":61.5,\"pressure\":1.25}");
    return frame;
}

QByteArray oversizeHeader()
{
    QByteArray header(Protocol::kHeaderSize, '\0');
    header[0] = static_cast<char>((Protocol::kMagic >> 8) & 0xff);
    header[1] = static_cast<char>(Protocol::kMagic & 0xff);
    header[2] = static_cast<char>(Protocol::Version1);
    header[3] = static_cast<char>(Protocol::MessageType::Telemetry);
    const quint16 invalidSize = static_cast<quint16>(Protocol::kMaxFrameSize + 1);
    header[4] = static_cast<char>((invalidSize >> 8) & 0xff);
    header[5] = static_cast<char>(invalidSize & 0xff);
    return header;
}

} // namespace

class ProtocolCodecTest : public QObject
{
    Q_OBJECT

private slots:
    void crc16MatchesStandardVector();
    void fixedVectorMatchesProtocolV1();
    void encodeDecodeRoundTrip();
    void encodeRejectsInvalidFrameValues();
    void decoderReassemblesFragmentedFrame();
    void decoderHandlesCoalescedFrames();
    void decoderReportsTruncatedFrameOnFinish();
    void decoderResynchronizesAfterTruncatedFrame();
    void rejectsBadCrc();
    void rejectsUnknownVersion();
    void rejectsUnknownMessageType();
    void rejectsInvalidLengthAndResynchronizes();
    void rejectsOversizeFrameAndResynchronizes();
};

void ProtocolCodecTest::crc16MatchesStandardVector()
{
    QCOMPARE(CRC16::compute(QByteArrayLiteral("123456789")), quint16(0x29b1));
}

void ProtocolCodecTest::fixedVectorMatchesProtocolV1()
{
    Frame frame;
    frame.version = Protocol::Version1;
    frame.messageType = Protocol::MessageType::Telemetry;
    frame.sequence = 0x01020304U;
    frame.deviceId = QStringLiteral("DEV-001");
    frame.timestampUtcMs = 0x0102030405060708LL;

    QString errorMessage;
    const QByteArray encoded = FrameCodec::encode(frame, &errorMessage);
    QVERIFY2(!encoded.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(encoded.toHex(),
             QByteArrayLiteral("4d5501010034010203044445562d303031"
                               "00000000000000000000000000000000000000000000000000"
                               "010203040506070803f4"));

    const DecodeResult decoded = FrameCodec::decode(encoded);
    QCOMPARE(decoded.status, DecodeStatus::Ok);
    QCOMPARE(decoded.consumedBytes, 52);
    QCOMPARE(decoded.frame.sequence, 0x01020304U);
    QCOMPARE(decoded.frame.timestampUtcMs, 0x0102030405060708LL);
}

void ProtocolCodecTest::encodeDecodeRoundTrip()
{
    const Frame source = makeFrame(0x11223344U);
    QString errorMessage;
    const QByteArray encoded = FrameCodec::encode(source, &errorMessage);
    QVERIFY2(!encoded.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(static_cast<quint8>(encoded.at(0)), quint8(0x4d));
    QCOMPARE(static_cast<quint8>(encoded.at(1)), quint8(0x55));
    QCOMPARE(static_cast<quint8>(encoded.at(2)), quint8(Protocol::Version1));
    QCOMPARE(static_cast<quint8>(encoded.at(3)),
             static_cast<quint8>(Protocol::MessageType::Telemetry));
    QCOMPARE(encoded.size(), Protocol::kHeaderSize + source.payload.size() + Protocol::kCrcSize);

    const DecodeResult result = FrameCodec::decode(encoded);
    QCOMPARE(result.status, DecodeStatus::Ok);
    QCOMPARE(result.consumedBytes, encoded.size());
    QCOMPARE(result.frame.version, source.version);
    QCOMPARE(result.frame.messageType, source.messageType);
    QCOMPARE(result.frame.sequence, source.sequence);
    QCOMPARE(result.frame.deviceId, source.deviceId);
    QCOMPARE(result.frame.timestampUtcMs, source.timestampUtcMs);
    QCOMPARE(result.frame.payload, source.payload);
}

void ProtocolCodecTest::encodeRejectsInvalidFrameValues()
{
    QString errorMessage;

    Frame frame = makeFrame();
    frame.version = 2;
    QVERIFY(FrameCodec::encode(frame, &errorMessage).isEmpty());
    QVERIFY(errorMessage.contains(QStringLiteral("版本")));

    frame = makeFrame();
    frame.messageType = static_cast<Protocol::MessageType>(0x55);
    QVERIFY(FrameCodec::encode(frame, &errorMessage).isEmpty());
    QVERIFY(errorMessage.contains(QStringLiteral("消息类型")));

    frame = makeFrame();
    frame.deviceId = QString(33, QLatin1Char('x'));
    QVERIFY(FrameCodec::encode(frame, &errorMessage).isEmpty());
    QVERIFY(errorMessage.contains(QStringLiteral("设备 ID")));

    frame = makeFrame();
    frame.payload = QByteArray(Protocol::kMaxPayloadSize + 1, 'x');
    QVERIFY(FrameCodec::encode(frame, &errorMessage).isEmpty());
    QVERIFY(errorMessage.contains(QStringLiteral("超过上限")));
}

void ProtocolCodecTest::decoderReassemblesFragmentedFrame()
{
    const QByteArray encoded = FrameCodec::encode(makeFrame());
    QVERIFY(!encoded.isEmpty());

    FrameDecoder decoder;
    decoder.appendData(encoded.left(7));
    QVERIFY(!decoder.hasEvents());

    decoder.appendData(encoded.mid(7, 19));
    QVERIFY(!decoder.hasEvents());

    decoder.appendData(encoded.mid(26));
    QTRY_VERIFY(decoder.hasEvents());
    const FrameDecoder::Event event = decoder.takeNextEvent();
    QCOMPARE(event.type, FrameDecoder::EventType::DecodedFrame);
    QCOMPARE(event.frame.sequence, 42U);
}

void ProtocolCodecTest::decoderHandlesCoalescedFrames()
{
    const QByteArray first = FrameCodec::encode(makeFrame(1));
    const QByteArray second = FrameCodec::encode(makeFrame(2));
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());

    FrameDecoder decoder;
    decoder.appendData(first + second);
    QCOMPARE(decoder.eventCount(), 2);

    const FrameDecoder::Event firstEvent = decoder.takeNextEvent();
    const FrameDecoder::Event secondEvent = decoder.takeNextEvent();
    QCOMPARE(firstEvent.type, FrameDecoder::EventType::DecodedFrame);
    QCOMPARE(secondEvent.type, FrameDecoder::EventType::DecodedFrame);
    QCOMPARE(firstEvent.frame.sequence, 1U);
    QCOMPARE(secondEvent.frame.sequence, 2U);
}

void ProtocolCodecTest::decoderReportsTruncatedFrameOnFinish()
{
    const QByteArray encoded = FrameCodec::encode(makeFrame());
    QVERIFY(!encoded.isEmpty());

    FrameDecoder decoder;
    decoder.appendData(encoded.left(encoded.size() - 3));
    QVERIFY(!decoder.hasEvents());

    decoder.finish();
    QCOMPARE(decoder.eventCount(), 1);
    const FrameDecoder::Event event = decoder.takeNextEvent();
    QCOMPARE(event.type, FrameDecoder::EventType::ProtocolError);
    QVERIFY(event.message.contains(QStringLiteral("截断")));
    QCOMPARE(decoder.bufferedBytes(), 0);
}

void ProtocolCodecTest::decoderResynchronizesAfterTruncatedFrame()
{
    const QByteArray truncated = FrameCodec::encode(makeFrame(1)).left(23);
    const QByteArray valid = FrameCodec::encode(makeFrame(2));
    QVERIFY(!valid.isEmpty());

    FrameDecoder decoder;
    decoder.appendData(truncated + valid);

    bool sawError = false;
    bool sawFrame = false;
    while (decoder.hasEvents()) {
        const FrameDecoder::Event event = decoder.takeNextEvent();
        if (event.type == FrameDecoder::EventType::ProtocolError) {
            sawError = true;
            QVERIFY(event.message.contains(QStringLiteral("重新同步")));
        } else {
            sawFrame = true;
            QCOMPARE(event.frame.sequence, 2U);
        }
    }
    QVERIFY(sawError);
    QVERIFY(sawFrame);
}

void ProtocolCodecTest::rejectsBadCrc()
{
    QByteArray encoded = FrameCodec::encode(makeFrame());
    QVERIFY(!encoded.isEmpty());
    encoded[encoded.size() - 1] = static_cast<char>(
        static_cast<quint8>(encoded.at(encoded.size() - 1)) ^ 0x01);

    const DecodeResult result = FrameCodec::decode(encoded);
    QCOMPARE(result.status, DecodeStatus::CrcMismatch);

    FrameDecoder decoder;
    decoder.appendData(encoded);
    QVERIFY(decoder.hasEvents());
    QCOMPARE(decoder.takeNextEvent().type, FrameDecoder::EventType::ProtocolError);
}

void ProtocolCodecTest::rejectsUnknownVersion()
{
    QByteArray encoded = FrameCodec::encode(makeFrame());
    QVERIFY(!encoded.isEmpty());
    encoded[2] = static_cast<char>(Protocol::Version1 + 1);

    const DecodeResult result = FrameCodec::decode(encoded);
    QCOMPARE(result.status, DecodeStatus::UnsupportedVersion);

    FrameDecoder decoder;
    const QByteArray valid = FrameCodec::encode(makeFrame(99));
    decoder.appendData(encoded + valid);
    QVERIFY(decoder.hasEvents());
    const FrameDecoder::Event event = decoder.takeNextEvent();
    QCOMPARE(event.type, FrameDecoder::EventType::ProtocolError);
    QVERIFY(event.message.contains(QStringLiteral("版本")));
    QVERIFY(decoder.hasEvents());
    const FrameDecoder::Event nextEvent = decoder.takeNextEvent();
    QCOMPARE(nextEvent.type, FrameDecoder::EventType::DecodedFrame);
    QCOMPARE(nextEvent.frame.sequence, 99U);
}

void ProtocolCodecTest::rejectsUnknownMessageType()
{
    QByteArray encoded = FrameCodec::encode(makeFrame());
    QVERIFY(!encoded.isEmpty());
    encoded[3] = static_cast<char>(0x55);

    const DecodeResult result = FrameCodec::decode(encoded);
    QCOMPARE(result.status, DecodeStatus::UnknownMessageType);

    FrameDecoder decoder;
    decoder.appendData(encoded);
    QVERIFY(decoder.hasEvents());
    const FrameDecoder::Event event = decoder.takeNextEvent();
    QCOMPARE(event.type, FrameDecoder::EventType::ProtocolError);
    QVERIFY(event.message.contains(QStringLiteral("消息类型")));
}

void ProtocolCodecTest::rejectsInvalidLengthAndResynchronizes()
{
    QByteArray invalidLength(Protocol::kHeaderSize, '\0');
    invalidLength[0] = static_cast<char>((Protocol::kMagic >> 8) & 0xff);
    invalidLength[1] = static_cast<char>(Protocol::kMagic & 0xff);
    invalidLength[2] = static_cast<char>(Protocol::Version1);
    invalidLength[3] = static_cast<char>(Protocol::MessageType::Telemetry);
    invalidLength[4] = 0;
    invalidLength[5] = static_cast<char>(Protocol::kMinFrameSize - 1);

    const QByteArray valid = FrameCodec::encode(makeFrame(77));
    QVERIFY(!valid.isEmpty());

    FrameDecoder decoder;
    decoder.appendData(invalidLength + valid);
    QCOMPARE(decoder.eventCount(), 2);
    QCOMPARE(decoder.takeNextEvent().type, FrameDecoder::EventType::ProtocolError);
    const FrameDecoder::Event frameEvent = decoder.takeNextEvent();
    QCOMPARE(frameEvent.type, FrameDecoder::EventType::DecodedFrame);
    QCOMPARE(frameEvent.frame.sequence, 77U);
}

void ProtocolCodecTest::rejectsOversizeFrameAndResynchronizes()
{
    QString errorMessage;
    Frame largeFrame = makeFrame();
    largeFrame.payload = QByteArray(Protocol::kMaxPayloadSize + 1, 'x');
    QVERIFY(FrameCodec::encode(largeFrame, &errorMessage).isEmpty());
    QVERIFY(!errorMessage.isEmpty());

    const QByteArray valid = FrameCodec::encode(makeFrame());
    QVERIFY(!valid.isEmpty());

    FrameDecoder decoder;
    decoder.appendData(oversizeHeader() + valid);
    QVERIFY(decoder.hasEvents());
    const FrameDecoder::Event errorEvent = decoder.takeNextEvent();
    QCOMPARE(errorEvent.type, FrameDecoder::EventType::ProtocolError);
    QVERIFY(errorEvent.message.contains(QStringLiteral("超过上限")));

    bool decoded = false;
    while (decoder.hasEvents()) {
        const FrameDecoder::Event event = decoder.takeNextEvent();
        if (event.type == FrameDecoder::EventType::DecodedFrame) {
            QCOMPARE(event.frame.deviceId, QStringLiteral("DEV-001"));
            decoded = true;
            break;
        }
    }
    QVERIFY(decoded);
}

QTEST_MAIN(ProtocolCodecTest)

#include "ProtocolCodecTest.moc"
