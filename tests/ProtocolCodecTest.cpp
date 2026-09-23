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
    void encodeDecodeRoundTrip();
    void decoderReassemblesFragmentedFrame();
    void decoderHandlesCoalescedFrames();
    void rejectsBadCrc();
    void rejectsUnknownVersion();
    void rejectsOversizeFrameAndResynchronizes();
};

void ProtocolCodecTest::crc16MatchesStandardVector()
{
    QCOMPARE(CRC16::compute(QByteArrayLiteral("123456789")), quint16(0x29b1));
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
    decoder.appendData(encoded);
    QVERIFY(decoder.hasEvents());
    const FrameDecoder::Event event = decoder.takeNextEvent();
    QCOMPARE(event.type, FrameDecoder::EventType::ProtocolError);
    QVERIFY(event.message.contains(QStringLiteral("版本")));
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
