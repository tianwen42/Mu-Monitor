#include "network/ProtocolPayloadMapper.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace {
QByteArray telemetryPayload()
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("telemetry"));
    object.insert(QStringLiteral("online"), true);
    object.insert(QStringLiteral("collecting"), true);
    object.insert(QStringLiteral("temperature"), 63.5);
    object.insert(QStringLiteral("pressure"), 1.15);
    object.insert(QStringLiteral("speed"), 1370.0);
    object.insert(QStringLiteral("voltage"), 220.0);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
}

class ProtocolPayloadMapperTest : public QObject
{
    Q_OBJECT

private slots:
    void mapsTelemetryPayload();
    void rejectsOfflineTelemetry();
    void mapsHeartbeatPayload();
    void mapsEmptyHeartbeatPayload();
};

void ProtocolPayloadMapperTest::mapsTelemetryPayload()
{
    const qint64 timestampUtcMs = 1727000000123LL;
    TelemetrySample sample;
    QString errorMessage;
    QVERIFY2(ProtocolPayloadMapper::toTelemetrySample(
                 QStringLiteral("DEV-001"), timestampUtcMs,
                 telemetryPayload(), &sample, &errorMessage),
             qPrintable(errorMessage));

    QCOMPARE(sample.deviceId, QStringLiteral("DEV-001"));
    QCOMPARE(sample.collectedAt, QDateTime::fromMSecsSinceEpoch(timestampUtcMs, QTimeZone::UTC));
    QVERIFY(sample.isValid(&errorMessage));
    QCOMPARE(sample.measurement(MeasurementType::Temperature)->value, 63.5);
    QCOMPARE(sample.measurement(MeasurementType::Pressure)->value, 1.15);
    QCOMPARE(sample.measurement(MeasurementType::Speed)->value, 1370.0);
    QCOMPARE(sample.measurement(MeasurementType::Voltage)->value, 220.0);
}

void ProtocolPayloadMapperTest::rejectsOfflineTelemetry()
{
    QJsonObject object = QJsonDocument::fromJson(telemetryPayload()).object();
    object.insert(QStringLiteral("online"), false);

    TelemetrySample sample;
    QString errorMessage;
    QVERIFY(!ProtocolPayloadMapper::toTelemetrySample(
        QStringLiteral("DEV-001"), 1727000000123LL,
        QJsonDocument(object).toJson(QJsonDocument::Compact),
        &sample, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("离线")));
}

void ProtocolPayloadMapperTest::mapsHeartbeatPayload()
{
    QJsonObject object;
    object.insert(QStringLiteral("online"), true);
    object.insert(QStringLiteral("collecting"), true);
    object.insert(QStringLiteral("latencyMs"), 37);

    HeartbeatRecord heartbeat;
    QString errorMessage;
    QVERIFY2(ProtocolPayloadMapper::toHeartbeatRecord(
                 QStringLiteral("DEV-002"), 1727000000999LL,
                 QJsonDocument(object).toJson(QJsonDocument::Compact),
                 &heartbeat, &errorMessage),
             qPrintable(errorMessage));

    QCOMPARE(heartbeat.deviceId, QStringLiteral("DEV-002"));
    QCOMPARE(heartbeat.heartbeatAt,
             QDateTime::fromMSecsSinceEpoch(1727000000999LL, QTimeZone::UTC));
    QVERIFY(heartbeat.online);
    QVERIFY(heartbeat.collecting);
    QCOMPARE(heartbeat.latencyMs, 37);
}

void ProtocolPayloadMapperTest::mapsEmptyHeartbeatPayload()
{
    HeartbeatRecord heartbeat;
    QString errorMessage;
    QVERIFY2(ProtocolPayloadMapper::toHeartbeatRecord(
                 QStringLiteral("DEV-003"), 1727000001000LL, QByteArray(),
                 &heartbeat, &errorMessage),
             qPrintable(errorMessage));

    QCOMPARE(heartbeat.deviceId, QStringLiteral("DEV-003"));
    QCOMPARE(heartbeat.heartbeatAt,
             QDateTime::fromMSecsSinceEpoch(1727000001000LL, QTimeZone::UTC));
    QVERIFY(heartbeat.online);
    QVERIFY(heartbeat.collecting);
    QCOMPARE(heartbeat.latencyMs, -1);
}
QTEST_GUILESS_MAIN(ProtocolPayloadMapperTest)

#include "ProtocolPayloadMapperTest.moc"
