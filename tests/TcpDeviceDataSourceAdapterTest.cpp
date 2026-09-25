#include "network/TcpDeviceDataSourceAdapter.h"

#include "network/TcpDeviceDataSource.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include <algorithm>

namespace {

DeviceInfo makeDevice(const QString &id)
{
    DeviceInfo device;
    device.deviceId = id;
    device.name = QStringLiteral("设备 %1").arg(id);
    device.ipAddress = QStringLiteral("127.0.0.1");
    device.protocol = QStringLiteral("TCP");
    return device;
}

Frame makeTelemetryFrame(const QString &deviceId, quint32 sequence,
                         bool online = true, bool collecting = true,
                         double temperature = 61.5)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("online"), online);
    payload.insert(QStringLiteral("collecting"), collecting);
    payload.insert(QStringLiteral("temperature"), temperature);
    payload.insert(QStringLiteral("pressure"), 1.12);
    payload.insert(QStringLiteral("speed"), 1480.0);
    payload.insert(QStringLiteral("voltage"), 220.5);

    Frame frame;
    frame.version = Protocol::Version1;
    frame.messageType = Protocol::MessageType::Telemetry;
    frame.sequence = sequence;
    frame.deviceId = deviceId;
    frame.timestampUtcMs = 1727000000000LL + sequence;
    frame.payload = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    return frame;
}

Frame makeHeartbeatFrame(const QString &deviceId, quint32 sequence,
                         bool online = true, bool collecting = true,
                         int latencyMs = 37)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("online"), online);
    payload.insert(QStringLiteral("collecting"), collecting);
    payload.insert(QStringLiteral("latencyMs"), latencyMs);

    Frame frame;
    frame.version = Protocol::Version1;
    frame.messageType = Protocol::MessageType::Heartbeat;
    frame.sequence = sequence;
    frame.deviceId = deviceId;
    frame.timestampUtcMs = 1727000000000LL + sequence;
    frame.payload = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    return frame;
}

void startAdapter(TcpDeviceDataSourceAdapter &adapter, QTcpServer &server)
{
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    adapter.setEndpoint(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(adapter.start());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer);
    QTRY_COMPARE_WITH_TIMEOUT(adapter.connectionState(), ConnectionState::Connected,
                             2000);
}

int stateCount(const QList<ConnectionState> &states, ConnectionState expected)
{
    return static_cast<int>(std::count(states.cbegin(), states.cend(), expected));
}

} // namespace

class TcpDeviceDataSourceAdapterTest : public QObject
{
    Q_OBJECT

private slots:
    void mapsTelemetryFrame();
    void mapsHeartbeatFrame();
    void mapsTcpConnectionStates();
    void filtersOfflineUnknownAndDisabledFrames();
    void stopsAndIgnoresRepeatedStart();
    void aggregatesErrors();
};

void TcpDeviceDataSourceAdapterTest::mapsTelemetryFrame()
{
    TcpDeviceDataSourceAdapter adapter;
    adapter.setDevices({makeDevice(QStringLiteral("DEV-001"))});

    QTcpServer server;
    startAdapter(adapter, server);

    QList<TelemetrySample> samples;
    connect(&adapter, &IDeviceDataSource::telemetryGenerated,
            this, [&samples](const QList<TelemetrySample> &batch) {
                samples = batch;
            });

    emit adapter.tcpDeviceDataSource()->frameReceived(
        makeTelemetryFrame(QStringLiteral("DEV-001"), 7));

    QCOMPARE(samples.size(), 1);
    const TelemetrySample &sample = samples.first();
    QCOMPARE(sample.deviceId, QStringLiteral("DEV-001"));
    QCOMPARE(sample.collectedAt.toUTC().toMSecsSinceEpoch(), 1727000000007LL);
    QVERIFY(sample.measurement(MeasurementType::Temperature));
    QCOMPARE(sample.measurement(MeasurementType::Temperature)->value, 61.5);
    QCOMPARE(sample.measurement(MeasurementType::Temperature)->quality,
             QualityCode::Good);
    adapter.stop();
}

void TcpDeviceDataSourceAdapterTest::mapsHeartbeatFrame()
{
    TcpDeviceDataSourceAdapter adapter;
    adapter.setDevices({makeDevice(QStringLiteral("DEV-001"))});

    QTcpServer server;
    startAdapter(adapter, server);

    QList<HeartbeatRecord> heartbeats;
    connect(&adapter, &IDeviceDataSource::heartbeatGenerated,
            this, [&heartbeats](const QList<HeartbeatRecord> &batch) {
                heartbeats = batch;
            });

    emit adapter.tcpDeviceDataSource()->frameReceived(
        makeHeartbeatFrame(QStringLiteral("DEV-001"), 9, true, false, 42));

    QCOMPARE(heartbeats.size(), 1);
    const HeartbeatRecord &heartbeat = heartbeats.first();
    QCOMPARE(heartbeat.deviceId, QStringLiteral("DEV-001"));
    QCOMPARE(heartbeat.heartbeatAt.toUTC().toMSecsSinceEpoch(), 1727000000009LL);
    QVERIFY(heartbeat.online);
    QVERIFY(!heartbeat.collecting);
    QCOMPARE(heartbeat.latencyMs, 42);
    adapter.stop();
}

void TcpDeviceDataSourceAdapterTest::mapsTcpConnectionStates()
{
    TcpDeviceDataSourceAdapter adapter;
    adapter.setDevices({makeDevice(QStringLiteral("DEV-001"))});

    QTcpServer server;
    startAdapter(adapter, server);

    QList<ConnectionState> states;
    connect(&adapter, &IDeviceDataSource::connectionStateChanged,
            this, [&states](ConnectionState state) {
                states.append(state);
            });

    emit adapter.tcpDeviceDataSource()->stateChanged(TcpConnectionState::Connecting);
    emit adapter.tcpDeviceDataSource()->stateChanged(TcpConnectionState::Connected);
    emit adapter.tcpDeviceDataSource()->stateChanged(TcpConnectionState::Reconnecting);
    emit adapter.tcpDeviceDataSource()->stateChanged(TcpConnectionState::Disconnected);

    QCOMPARE(states,
             (QList<ConnectionState>{ConnectionState::Connecting,
                                     ConnectionState::Connected,
                                     ConnectionState::Reconnecting,
                                     ConnectionState::Disconnected}));
    adapter.stop();
}

void TcpDeviceDataSourceAdapterTest::filtersOfflineUnknownAndDisabledFrames()
{
    TcpDeviceDataSourceAdapter adapter;
    adapter.setDevices({
        makeDevice(QStringLiteral("DEV-001")),
        makeDevice(QStringLiteral("DEV-002")),
    });

    QTcpServer server;
    startAdapter(adapter, server);

    QList<TelemetrySample> samples;
    connect(&adapter, &IDeviceDataSource::telemetryGenerated,
            this, [&samples](const QList<TelemetrySample> &batch) {
                samples = batch;
            });

    emit adapter.tcpDeviceDataSource()->frameReceived(
        makeTelemetryFrame(QStringLiteral("DEV-001"), 1, false, true));
    QVERIFY(samples.isEmpty());

    emit adapter.tcpDeviceDataSource()->frameReceived(
        makeTelemetryFrame(QStringLiteral("UNKNOWN"), 2, true, true));
    QVERIFY(samples.isEmpty());

    QVERIFY(adapter.setDeviceCollectionEnabled(QStringLiteral("DEV-002"), false));
    emit adapter.tcpDeviceDataSource()->frameReceived(
        makeTelemetryFrame(QStringLiteral("DEV-002"), 3, true, true));
    QVERIFY(samples.isEmpty());

    emit adapter.tcpDeviceDataSource()->frameReceived(
        makeTelemetryFrame(QStringLiteral("DEV-001"), 4, true, true));
    QCOMPARE(samples.size(), 1);
    QCOMPARE(samples.first().deviceId, QStringLiteral("DEV-001"));

    samples.clear();
    QVERIFY(adapter.setCollectionEnabled(false));
    emit adapter.tcpDeviceDataSource()->frameReceived(
        makeTelemetryFrame(QStringLiteral("DEV-001"), 5, true, true));
    QVERIFY(samples.isEmpty());

    adapter.stop();
}

void TcpDeviceDataSourceAdapterTest::stopsAndIgnoresRepeatedStart()
{
    TcpDeviceDataSourceAdapter adapter;
    adapter.setDevices({makeDevice(QStringLiteral("DEV-001"))});

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    adapter.setEndpoint(QStringLiteral("127.0.0.1"), server.serverPort());

    QList<ConnectionState> states;
    connect(&adapter, &IDeviceDataSource::connectionStateChanged,
            this, [&states](ConnectionState state) {
                states.append(state);
            });

    QVERIFY(adapter.start());
    QVERIFY(adapter.start());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer);
    QTRY_COMPARE_WITH_TIMEOUT(adapter.connectionState(), ConnectionState::Connected,
                             2000);
    QCOMPARE(stateCount(states, ConnectionState::Connecting), 1);

    adapter.stop();
    QVERIFY(!adapter.isRunning());
    QCOMPARE(adapter.connectionState(), ConnectionState::Disconnected);
    QVERIFY(states.contains(ConnectionState::Stopping));
    QCOMPARE(states.last(), ConnectionState::Disconnected);

    const int stoppedStateCount = states.size();
    adapter.stop();
    QCOMPARE(states.size(), stoppedStateCount);
    QTRY_COMPARE_WITH_TIMEOUT(adapter.tcpDeviceDataSource()->state(),
                              TcpConnectionState::Disconnected, 2000);

    QVERIFY(adapter.start());
    QVERIFY(adapter.start());
    QTRY_COMPARE_WITH_TIMEOUT(adapter.connectionState(), ConnectionState::Connected,
                             2000);
    QCOMPARE(stateCount(states, ConnectionState::Connecting), 2);
    adapter.stop();
}

void TcpDeviceDataSourceAdapterTest::aggregatesErrors()
{
    TcpDeviceDataSourceAdapter adapter;
    QStringList errors;
    connect(&adapter, &IDeviceDataSource::errorOccurred,
            this, [&errors](const QString &message) {
                errors.append(message);
            });

    emit adapter.tcpDeviceDataSource()->errorOccurred(QStringLiteral("transport"));
    emit adapter.tcpDeviceDataSource()->protocolErrorOccurred(QStringLiteral("protocol"));

    QCOMPARE(errors, (QStringList{QStringLiteral("transport"),
                                  QStringLiteral("protocol")}));
}

QTEST_MAIN(TcpDeviceDataSourceAdapterTest)

#include "TcpDeviceDataSourceAdapterTest.moc"