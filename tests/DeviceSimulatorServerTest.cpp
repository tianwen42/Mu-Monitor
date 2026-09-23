#include "DeviceSimulatorServer.h"

#include <network/FrameDecoder.h>

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QtTest>

class DeviceSimulatorServerTest : public QObject
{
    Q_OBJECT

private slots:
    void sendsNewlineDelimitedJson();
    void sendsProtocolV1FramesWhenEnabled();
    void highTemperatureScenarioIsReported();
    void highPressureScenarioIsReported();
    void offlineScenarioIsReported();
    void tracksClientCount();
    void rejectsOccupiedPort();
    void rejectsUnknownDeviceScenario();

private:
    QJsonObject readJsonLine(QTcpSocket &client, int timeoutMs = 3000);
};

QJsonObject DeviceSimulatorServerTest::readJsonLine(QTcpSocket &client, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        if (!client.canReadLine()) {
            QTest::qWait(50);
            continue;
        }

        const QByteArray line = client.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        if (error.error == QJsonParseError::NoError && document.isObject()) {
            return document.object();
        }
    }

    return {};
}

void DeviceSimulatorServerTest::sendsNewlineDelimitedJson()
{
    DeviceSimulatorServer server;
    QString errorMessage;
    QVERIFY2(server.start(QHostAddress::LocalHost, 0, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY(server.serverPort() > 0);

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(client.waitForConnected(1000));

    const QJsonObject object = readJsonLine(client);
    QCOMPARE(object.value(QStringLiteral("type")).toString(), QStringLiteral("telemetry"));
    QCOMPARE(object.value(QStringLiteral("version")).toInt(), 1);
    QVERIFY(object.value(QStringLiteral("deviceId")).toString().startsWith(QStringLiteral("DEV-")));
    QVERIFY(object.contains(QStringLiteral("temperature")));
    QVERIFY(object.contains(QStringLiteral("pressure")));
}

void DeviceSimulatorServerTest::sendsProtocolV1FramesWhenEnabled()
{
    DeviceSimulatorServer server;
    QCOMPARE(server.wireFormat(), DeviceSimulatorServer::WireFormat::JsonLines);
    server.setWireFormat(DeviceSimulatorServer::WireFormat::ProtocolV1);

    QString errorMessage;
    QVERIFY2(server.start(QHostAddress::LocalHost, 0, &errorMessage),
             qPrintable(errorMessage));

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(client.waitForConnected(1000));

    FrameDecoder decoder;
    Frame received;
    bool decoded = false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3500 && !decoded) {
        if (client.bytesAvailable() > 0) {
            decoder.appendData(client.readAll());
        }
        while (decoder.hasEvents()) {
            const FrameDecoder::Event event = decoder.takeNextEvent();
            if (event.type == FrameDecoder::EventType::DecodedFrame) {
                received = event.frame;
                decoded = true;
                break;
            }
            QFAIL(qPrintable(event.message));
        }
        if (!decoded) {
            QTest::qWait(50);
        }
    }

    QVERIFY2(decoded, "未在超时时间内收到 Protocol v1 帧");
    QCOMPARE(received.version, quint8(Protocol::Version1));
    QCOMPARE(received.messageType, Protocol::MessageType::Telemetry);
    QCOMPARE(received.sequence, 0U);
    QVERIFY(received.deviceId.startsWith(QStringLiteral("DEV-")));
    QVERIFY(qAbs(received.timestampUtcMs - QDateTime::currentMSecsSinceEpoch()) < 10000);
    QVERIFY(!received.payload.endsWith('\n'));

    QJsonParseError parseError;
    const QJsonDocument payload = QJsonDocument::fromJson(received.payload, &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(payload.isObject());
    QVERIFY(payload.object().contains(QStringLiteral("temperature")));
}

void DeviceSimulatorServerTest::highTemperatureScenarioIsReported()
{
    DeviceSimulatorServer server;
    QString errorMessage;
    QVERIFY2(server.start(QHostAddress::LocalHost, 0, &errorMessage),
             qPrintable(errorMessage));

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(client.waitForConnected(1000));
    QVERIFY(server.setScenario(QStringLiteral("DEV-001"),
                               DeviceSimulatorServer::Scenario::HighTemperature));

    QJsonObject object;
    for (int i = 0; i < 5; ++i) {
        object = readJsonLine(client, 2000);
        if (object.value(QStringLiteral("deviceId")).toString() == QStringLiteral("DEV-001")) {
            break;
        }
    }

    QCOMPARE(object.value(QStringLiteral("deviceId")).toString(), QStringLiteral("DEV-001"));
    QCOMPARE(object.value(QStringLiteral("status")).toString(), QStringLiteral("alarm"));
    QCOMPARE(object.value(QStringLiteral("alarm")).toString(), QStringLiteral("temperature_high"));
    QVERIFY(object.value(QStringLiteral("temperature")).toDouble() > 80.0);
}

void DeviceSimulatorServerTest::highPressureScenarioIsReported()
{
    DeviceSimulatorServer server;
    QString errorMessage;
    QVERIFY2(server.start(QHostAddress::LocalHost, 0, &errorMessage),
             qPrintable(errorMessage));

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(client.waitForConnected(1000));
    QVERIFY(server.setScenario(QStringLiteral("DEV-002"),
                               DeviceSimulatorServer::Scenario::HighPressure));

    QJsonObject object;
    for (int i = 0; i < 5; ++i) {
        object = readJsonLine(client, 2000);
        if (object.value(QStringLiteral("deviceId")).toString() == QStringLiteral("DEV-002")) {
            break;
        }
    }

    QCOMPARE(object.value(QStringLiteral("status")).toString(), QStringLiteral("alarm"));
    QCOMPARE(object.value(QStringLiteral("alarm")).toString(), QStringLiteral("pressure_high"));
    QVERIFY(object.value(QStringLiteral("pressure")).toDouble() > 1.8);
}

void DeviceSimulatorServerTest::offlineScenarioIsReported()
{
    DeviceSimulatorServer server;
    QString errorMessage;
    QVERIFY2(server.start(QHostAddress::LocalHost, 0, &errorMessage),
             qPrintable(errorMessage));

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(client.waitForConnected(1000));
    QVERIFY(server.setScenario(QStringLiteral("DEV-003"),
                               DeviceSimulatorServer::Scenario::Offline));

    QJsonObject object;
    for (int i = 0; i < 5; ++i) {
        object = readJsonLine(client, 2000);
        if (object.value(QStringLiteral("deviceId")).toString() == QStringLiteral("DEV-003")) {
            break;
        }
    }

    QCOMPARE(object.value(QStringLiteral("online")).toBool(), false);
    QCOMPARE(object.value(QStringLiteral("collecting")).toBool(), false);
    QCOMPARE(object.value(QStringLiteral("status")).toString(), QStringLiteral("offline"));
    QCOMPARE(object.value(QStringLiteral("temperature")).toDouble(), 0.0);
    QCOMPARE(object.value(QStringLiteral("pressure")).toDouble(), 0.0);
}

void DeviceSimulatorServerTest::tracksClientCount()
{
    DeviceSimulatorServer server;
    QString errorMessage;
    QVERIFY2(server.start(QHostAddress::LocalHost, 0, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(server.clientCount(), 0);

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(client.waitForConnected(1000));
    QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 1, 1000);

    client.disconnectFromHost();
    QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 0, 1000);
}

void DeviceSimulatorServerTest::rejectsOccupiedPort()
{
    DeviceSimulatorServer first;
    QString firstError;
    QVERIFY2(first.start(QHostAddress::LocalHost, 0, &firstError),
             qPrintable(firstError));

    DeviceSimulatorServer second;
    QString secondError;
    QVERIFY(!second.start(QHostAddress::LocalHost, first.serverPort(), &secondError));
    QVERIFY(!second.isRunning());
    QVERIFY(!secondError.isEmpty());
}

void DeviceSimulatorServerTest::rejectsUnknownDeviceScenario()
{
    DeviceSimulatorServer server;
    QCOMPARE(server.devices().size(), 4);
    QVERIFY(!server.setScenario(QStringLiteral("DEV-999"),
                                DeviceSimulatorServer::Scenario::HighTemperature));
    QVERIFY(server.scenarioText(QStringLiteral("DEV-999")).isEmpty());
}

QTEST_MAIN(DeviceSimulatorServerTest)

#include "DeviceSimulatorServerTest.moc"
