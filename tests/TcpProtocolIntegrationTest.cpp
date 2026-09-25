#include "DeviceSimulatorServer.h"

#include <network/TcpDeviceDataSource.h>
#include <network/TcpDeviceDataSourceAdapter.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtTest>

class TcpProtocolIntegrationTest : public QObject
{
    Q_OBJECT

private slots:
    void receivesLiveProtocolFrames();
    void adapterDiscoversFourSimulatorDevices();
    void isolatesBadCrcFrameAndKeepsReceiving();
    void recoversAfterSimulatorDisconnectsClient();
};

void TcpProtocolIntegrationTest::receivesLiveProtocolFrames()
{
    DeviceSimulatorServer server;
    server.setSendIntervalMs(30);
    QString errorMessage;
    QVERIFY2(server.start(QHostAddress::LocalHost, 0, &errorMessage),
             qPrintable(errorMessage));

    TcpDeviceDataSource source;
    source.setReadTimeoutMs(1000);
    QSignalSpy frameSpy(&source, &TcpDeviceDataSource::frameReceived);
    source.connectToDevice(QStringLiteral("127.0.0.1"), server.serverPort());

    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() >= 8, 2500);

    QSet<QString> deviceIds;
    for (const QList<QVariant> &arguments : frameSpy) {
        const Frame frame = qvariant_cast<Frame>(arguments.at(0));
        QCOMPARE(frame.version, quint8(Protocol::Version1));
        QCOMPARE(frame.messageType, Protocol::MessageType::Telemetry);
        QVERIFY(frame.timestampUtcMs > 0);
        deviceIds.insert(frame.deviceId);

        QJsonParseError parseError;
        const QJsonDocument payload = QJsonDocument::fromJson(frame.payload, &parseError);
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        QVERIFY(payload.isObject());
        QVERIFY(payload.object().contains(QStringLiteral("temperature")));
    }
    QCOMPARE(deviceIds.size(), 4);

    source.disconnectFromDevice();
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), TcpConnectionState::Disconnected, 1000);
}

void TcpProtocolIntegrationTest::adapterDiscoversFourSimulatorDevices()
{
    DeviceSimulatorServer server;
    server.setSendIntervalMs(30);
    QString errorMessage;
    QVERIFY2(server.start(QHostAddress::LocalHost, 0, &errorMessage),
             qPrintable(errorMessage));

    TcpDeviceDataSourceAdapter adapter;
    QVERIFY(adapter.discoversDevicesDynamically());
    adapter.setEndpoint(QStringLiteral("127.0.0.1"), server.serverPort());
    QSignalSpy discoverySpy(&adapter, &IDeviceDataSource::deviceDiscovered);
    QVERIFY(adapter.start());
    QTRY_VERIFY_WITH_TIMEOUT(discoverySpy.count() >= 4, 2500);

    QSet<QString> deviceIds;
    for (const DeviceInfo &device : adapter.devices()) {
        deviceIds.insert(device.deviceId);
    }
    QCOMPARE(deviceIds.size(), 4);
    QVERIFY(deviceIds.contains(QStringLiteral("DEV-001")));
    QVERIFY(deviceIds.contains(QStringLiteral("DEV-004")));
    QVERIFY(!deviceIds.contains(QStringLiteral("DEV-005")));
    adapter.stop();
}

void TcpProtocolIntegrationTest::isolatesBadCrcFrameAndKeepsReceiving()
{
    DeviceSimulatorServer server;
    server.setSendIntervalMs(30);
    QVERIFY(server.setScenario(QStringLiteral("DEV-001"),
                               DeviceSimulatorServer::Scenario::BadCrc));
    QString errorMessage;
    QVERIFY2(server.start(QHostAddress::LocalHost, 0, &errorMessage),
             qPrintable(errorMessage));

    TcpDeviceDataSource source;
    source.setReadTimeoutMs(1000);
    QSignalSpy frameSpy(&source, &TcpDeviceDataSource::frameReceived);
    QSignalSpy protocolErrorSpy(&source, &TcpDeviceDataSource::protocolErrorOccurred);
    source.connectToDevice(QStringLiteral("127.0.0.1"), server.serverPort());

    QTRY_VERIFY_WITH_TIMEOUT(!protocolErrorSpy.isEmpty(), 1500);
    QVERIFY(protocolErrorSpy.first().at(0).toString().contains(QStringLiteral("CRC")));
    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() >= 3, 1500);

    QSet<QString> validDeviceIds;
    for (const QList<QVariant> &arguments : frameSpy) {
        validDeviceIds.insert(qvariant_cast<Frame>(arguments.at(0)).deviceId);
    }
    QVERIFY(!validDeviceIds.contains(QStringLiteral("DEV-001")));
    QVERIFY(validDeviceIds.size() >= 3);

    QVERIFY(server.setScenario(QStringLiteral("DEV-001"),
                               DeviceSimulatorServer::Scenario::Normal));
    frameSpy.clear();
    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() >= 4, 1500);
    bool recovered = false;
    for (const QList<QVariant> &arguments : frameSpy) {
        if (qvariant_cast<Frame>(arguments.at(0)).deviceId == QStringLiteral("DEV-001")) {
            recovered = true;
            break;
        }
    }
    QVERIFY(recovered);

    source.disconnectFromDevice();
}

void TcpProtocolIntegrationTest::recoversAfterSimulatorDisconnectsClient()
{
    DeviceSimulatorServer server;
    server.setSendIntervalMs(30);
    QString errorMessage;
    QVERIFY2(server.start(QHostAddress::LocalHost, 0, &errorMessage),
             qPrintable(errorMessage));

    TcpDeviceDataSource source;
    source.setReconnectDelayMs(80);
    source.setReadTimeoutMs(1000);
    QSignalSpy frameSpy(&source, &TcpDeviceDataSource::frameReceived);
    source.connectToDevice(QStringLiteral("127.0.0.1"), server.serverPort());

    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() >= 4, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), TcpConnectionState::Connected, 1000);

    QVERIFY(server.setScenario(QStringLiteral("DEV-002"),
                               DeviceSimulatorServer::Scenario::ActiveDisconnect));
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), TcpConnectionState::Reconnecting, 1500);

    QVERIFY(server.setScenario(QStringLiteral("DEV-002"),
                               DeviceSimulatorServer::Scenario::Normal));
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), TcpConnectionState::Connected, 1500);
    const int framesBeforeReconnect = frameSpy.count();
    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() > framesBeforeReconnect, 1500);

    source.disconnectFromDevice();
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), TcpConnectionState::Disconnected, 1000);
}

QTEST_MAIN(TcpProtocolIntegrationTest)

#include "TcpProtocolIntegrationTest.moc"
