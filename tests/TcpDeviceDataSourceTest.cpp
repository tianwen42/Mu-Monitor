#include <network/TcpDeviceDataSource.h>
#include <protocol/FrameCodec.h>

#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

namespace {

Frame makeFrame(quint32 sequence)
{
    Frame frame;
    frame.version = Protocol::Version1;
    frame.messageType = Protocol::MessageType::Telemetry;
    frame.sequence = sequence;
    frame.deviceId = QStringLiteral("DEV-001");
    frame.timestampUtcMs = 1727000000000LL + sequence;
    frame.payload = QByteArrayLiteral("{\"temperature\":61.5}");
    return frame;
}

} // namespace

class TcpDeviceDataSourceTest : public QObject
{
    Q_OBJECT

private slots:
    void receivesFramedData();
    void receivesCoalescedFrames();
    void rejectsBadCrcAndContinues();
    void sendsFrameOverTcp();
    void reconnectsAfterPeerDisconnect();
    void readTimeoutDisconnectsAndSignalsError();
    void reconnectBackoffIsExponentialAndCapped();
    void manualDisconnectSuppressesReconnect();
    void timeoutSettingsAreBounded();
};

void TcpDeviceDataSourceTest::receivesFramedData()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TcpDeviceDataSource source;
    source.setReconnectEnabled(false);
    QSignalSpy frameSpy(&source, &TcpDeviceDataSource::frameReceived);

    source.connectToDevice(QStringLiteral("127.0.0.1"), server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer);
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), TcpConnectionState::Connected, 2000);

    const QByteArray encoded = FrameCodec::encode(makeFrame(7));
    QVERIFY(!encoded.isEmpty());
    peer->write(encoded.left(11));
    peer->flush();
    QTest::qWait(20);
    peer->write(encoded.mid(11));
    peer->flush();

    QTRY_COMPARE_WITH_TIMEOUT(frameSpy.count(), 1, 2000);
    const Frame received = qvariant_cast<Frame>(frameSpy.takeFirst().at(0));
    QCOMPARE(received.sequence, 7U);
    QCOMPARE(received.deviceId, QStringLiteral("DEV-001"));

    source.disconnectFromDevice();
}

void TcpDeviceDataSourceTest::receivesCoalescedFrames()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TcpDeviceDataSource source;
    source.setReconnectEnabled(false);
    QSignalSpy frameSpy(&source, &TcpDeviceDataSource::frameReceived);

    source.connectToDevice(QStringLiteral("127.0.0.1"), server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer);
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), TcpConnectionState::Connected, 2000);

    const QByteArray first = FrameCodec::encode(makeFrame(10));
    const QByteArray second = FrameCodec::encode(makeFrame(11));
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());
    peer->write(first + second);
    peer->flush();

    QTRY_COMPARE_WITH_TIMEOUT(frameSpy.count(), 2, 2000);
    QCOMPARE(qvariant_cast<Frame>(frameSpy.at(0).at(0)).sequence, 10U);
    QCOMPARE(qvariant_cast<Frame>(frameSpy.at(1).at(0)).sequence, 11U);

    source.disconnectFromDevice();
}

void TcpDeviceDataSourceTest::rejectsBadCrcAndContinues()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TcpDeviceDataSource source;
    source.setReconnectEnabled(false);
    QSignalSpy frameSpy(&source, &TcpDeviceDataSource::frameReceived);
    QSignalSpy protocolErrorSpy(&source, &TcpDeviceDataSource::protocolErrorOccurred);

    source.connectToDevice(QStringLiteral("127.0.0.1"), server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer);
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), TcpConnectionState::Connected, 2000);

    QByteArray bad = FrameCodec::encode(makeFrame(20));
    const QByteArray good = FrameCodec::encode(makeFrame(21));
    QVERIFY(!bad.isEmpty());
    QVERIFY(!good.isEmpty());
    bad[bad.size() - 1] = static_cast<char>(
        static_cast<quint8>(bad.at(bad.size() - 1)) ^ 0x01);
    peer->write(bad + good);
    peer->flush();

    QTRY_COMPARE_WITH_TIMEOUT(frameSpy.count(), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(!protocolErrorSpy.isEmpty(), 2000);
    QCOMPARE(qvariant_cast<Frame>(frameSpy.takeFirst().at(0)).sequence, 21U);

    source.disconnectFromDevice();
}

void TcpDeviceDataSourceTest::sendsFrameOverTcp()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TcpDeviceDataSource source;
    source.setReconnectEnabled(false);
    source.connectToDevice(QStringLiteral("127.0.0.1"), server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer);
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), TcpConnectionState::Connected, 2000);

    const Frame outgoing = makeFrame(30);
    const QByteArray encoded = FrameCodec::encode(outgoing);
    QVERIFY(!encoded.isEmpty());
    source.sendFrame(outgoing);

    QTRY_VERIFY_WITH_TIMEOUT(peer->bytesAvailable() >= encoded.size(), 2000);
    const QByteArray receivedBytes = peer->read(encoded.size());
    const DecodeResult decoded = FrameCodec::decode(receivedBytes);
    QCOMPARE(decoded.status, DecodeStatus::Ok);
    QCOMPARE(decoded.frame.sequence, outgoing.sequence);
    QCOMPARE(decoded.frame.payload, outgoing.payload);

    source.disconnectFromDevice();
}

void TcpDeviceDataSourceTest::reconnectsAfterPeerDisconnect()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TcpDeviceDataSource source;
    source.setReconnectDelayMs(300);
    source.connectToDevice(QStringLiteral("127.0.0.1"), server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
    QTcpSocket *firstPeer = server.nextPendingConnection();
    QVERIFY(firstPeer);
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), TcpConnectionState::Connected, 2000);

    firstPeer->disconnectFromHost();
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), TcpConnectionState::Reconnecting, 2000);

    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2500);
    QTcpSocket *secondPeer = server.nextPendingConnection();
    QVERIFY(secondPeer);
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), TcpConnectionState::Connected, 2000);

    source.disconnectFromDevice();
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), TcpConnectionState::Disconnected, 2000);
}

void TcpDeviceDataSourceTest::readTimeoutDisconnectsAndSignalsError()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TcpDeviceDataSource source;
    source.setReconnectEnabled(false);
    source.setReadTimeoutMs(150);
    QSignalSpy errorSpy(&source, &TcpDeviceDataSource::errorOccurred);

    source.connectToDevice(QStringLiteral("127.0.0.1"), server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer);
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), TcpConnectionState::Connected, 2000);

    QTRY_VERIFY_WITH_TIMEOUT(!errorSpy.isEmpty(), 1500);
    QVERIFY(errorSpy.takeFirst().at(0).toString().contains(QStringLiteral("接收超时")));
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), TcpConnectionState::Disconnected, 1000);
}

void TcpDeviceDataSourceTest::reconnectBackoffIsExponentialAndCapped()
{
    QTcpServer portReservation;
    QVERIFY(portReservation.listen(QHostAddress::LocalHost, 0));
    const quint16 closedPort = portReservation.serverPort();
    portReservation.close();

    TcpDeviceDataSource source;
    source.setReconnectDelayMs(50);
    source.setReconnectMaxDelayMs(200);
    source.setConnectTimeoutMs(100);
    QSignalSpy reconnectSpy(&source, &TcpDeviceDataSource::reconnectScheduled);

    source.connectToDevice(QStringLiteral("127.0.0.1"), closedPort);
    QTRY_VERIFY_WITH_TIMEOUT(reconnectSpy.count() >= 4, 3000);

    QCOMPARE(reconnectSpy.at(0).at(0).toInt(), 50);
    QCOMPARE(reconnectSpy.at(1).at(0).toInt(), 100);
    QCOMPARE(reconnectSpy.at(2).at(0).toInt(), 200);
    QCOMPARE(reconnectSpy.at(3).at(0).toInt(), 200);
    QCOMPARE(reconnectSpy.at(3).at(1).toInt(), 4);

    source.disconnectFromDevice();
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), TcpConnectionState::Disconnected, 1000);
}

void TcpDeviceDataSourceTest::manualDisconnectSuppressesReconnect()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TcpDeviceDataSource source;
    source.setReconnectDelayMs(80);
    source.connectToDevice(QStringLiteral("127.0.0.1"), server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer);
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), TcpConnectionState::Connected, 2000);

    source.disconnectFromDevice();
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), TcpConnectionState::Disconnected, 1000);
    QTest::qWait(250);
    QVERIFY(!server.hasPendingConnections());
}

void TcpDeviceDataSourceTest::timeoutSettingsAreBounded()
{
    TcpDeviceDataSource source;
    QCOMPARE(source.reconnectDelayMs(), TcpConnectionWorker::kDefaultReconnectDelayMs);
    QCOMPARE(source.reconnectMaxDelayMs(), TcpConnectionWorker::kDefaultReconnectMaxDelayMs);
    QCOMPARE(source.connectTimeoutMs(), TcpConnectionWorker::kDefaultConnectTimeoutMs);
    QCOMPARE(source.readTimeoutMs(), TcpConnectionWorker::kDefaultReadTimeoutMs);

    source.setReconnectDelayMs(10);
    source.setReconnectMaxDelayMs(-1);
    source.setConnectTimeoutMs(-1);
    source.setReadTimeoutMs(0);
    QCOMPARE(source.reconnectDelayMs(), 50);
    QCOMPARE(source.reconnectMaxDelayMs(), 50);
    QCOMPARE(source.connectTimeoutMs(), 0);
    QCOMPARE(source.readTimeoutMs(), 0);
}

QTEST_MAIN(TcpDeviceDataSourceTest)

#include "TcpDeviceDataSourceTest.moc"
