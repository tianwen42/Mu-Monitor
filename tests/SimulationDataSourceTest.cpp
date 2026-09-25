#include "network/SimulationDataSource.h"

#include <QTest>

#include <cmath>

namespace {
DeviceInfo makeDevice(const QString &id)
{
    DeviceInfo device;
    device.deviceId = id;
    device.name = QStringLiteral("设备 %1").arg(id);
    device.protocol = QStringLiteral("TCP");
    return device;
}

QList<TelemetrySample> collectSamples(SimulationDataSource &source, quint32 seed)
{
    QList<TelemetrySample> samples;
    const QMetaObject::Connection connection = QObject::connect(
        &source, &IDeviceDataSource::telemetryGenerated,
        [&samples](const QList<TelemetrySample> &batch) {
            samples = batch;
        });
    source.setSeed(seed);
    source.triggerTelemetry();
    QObject::disconnect(connection);
    return samples;
}
}

class SimulationDataSourceTest : public QObject
{
    Q_OBJECT

private slots:
    void startsStopsAndIgnoresRepeatedStart();
    void restartAfterStopKeepsSingleTimer();
    void fixedSeedProducesDeterministicValues();
    void offlineDeviceIsExcludedFromTelemetry();
};

void SimulationDataSourceTest::startsStopsAndIgnoresRepeatedStart()
{
    SimulationDataSource source;
    source.setDevices({makeDevice(QStringLiteral("DEV-001"))});
    source.setSamplingInterval(10);
    source.setHeartbeatInterval(1000);

    int telemetryBatches = 0;
    connect(&source, &IDeviceDataSource::telemetryGenerated,
            this, [&telemetryBatches](const QList<TelemetrySample> &) {
                ++telemetryBatches;
            });

    QVERIFY(source.start());
    QVERIFY(source.start());
    QVERIFY(source.isRunning());
    QTRY_VERIFY_WITH_TIMEOUT(telemetryBatches > 0, 300);

    source.stop();
    source.stop();
    QVERIFY(!source.isRunning());

    const int stoppedCount = telemetryBatches;
    QTest::qWait(40);
    QCOMPARE(telemetryBatches, stoppedCount);
}

void SimulationDataSourceTest::restartAfterStopKeepsSingleTimer()
{
    SimulationDataSource source;
    source.setDevices({makeDevice(QStringLiteral("DEV-001"))});
    source.setSamplingInterval(10);
    source.setHeartbeatInterval(1000);

    int telemetryBatches = 0;
    connect(&source, &IDeviceDataSource::telemetryGenerated,
            this, [&telemetryBatches](const QList<TelemetrySample> &) {
                ++telemetryBatches;
            });

    QVERIFY(source.start());
    QTRY_VERIFY_WITH_TIMEOUT(telemetryBatches > 0, 300);
    source.stop();
    const int stoppedCount = telemetryBatches;
    QTest::qWait(30);
    QCOMPARE(telemetryBatches, stoppedCount);

    QVERIFY(source.start());
    QVERIFY(source.start());
    QTRY_VERIFY_WITH_TIMEOUT(telemetryBatches > stoppedCount, 300);
    source.stop();
    QVERIFY(!source.isRunning());
}
void SimulationDataSourceTest::fixedSeedProducesDeterministicValues()
{
    SimulationDataSource first;
    first.setDevices({
        makeDevice(QStringLiteral("DEV-001")),
        makeDevice(QStringLiteral("DEV-002")),
    });

    SimulationDataSource second;
    second.setDevices({
        makeDevice(QStringLiteral("DEV-001")),
        makeDevice(QStringLiteral("DEV-002")),
    });

    const QList<TelemetrySample> firstSamples = collectSamples(first, 20260925);
    const QList<TelemetrySample> secondSamples = collectSamples(second, 20260925);

    QCOMPARE(firstSamples.size(), 2);
    QCOMPARE(secondSamples.size(), firstSamples.size());
    for (int i = 0; i < firstSamples.size(); ++i) {
        QCOMPARE(firstSamples.at(i).deviceId, secondSamples.at(i).deviceId);
        QCOMPARE(firstSamples.at(i).measurements.size(),
                 secondSamples.at(i).measurements.size());
        for (int j = 0; j < firstSamples.at(i).measurements.size(); ++j) {
            const double firstValue = firstSamples.at(i).measurements.at(j).value;
            const double secondValue = secondSamples.at(i).measurements.at(j).value;
            QVERIFY2(std::abs(firstValue - secondValue) < 1e-12,
                     "固定随机种子必须产生完全一致的模拟数据");
        }
    }
}

void SimulationDataSourceTest::offlineDeviceIsExcludedFromTelemetry()
{
    SimulationDataSource source;
    source.setDevices({
        makeDevice(QStringLiteral("DEV-001")),
        makeDevice(QStringLiteral("DEV-002")),
    });
    source.setDeviceOnline(QStringLiteral("DEV-002"), false);

    QList<TelemetrySample> samples;
    connect(&source, &IDeviceDataSource::telemetryGenerated,
            this, [&samples](const QList<TelemetrySample> &batch) {
                samples = batch;
            });
    source.triggerTelemetry();

    QCOMPARE(samples.size(), 1);
    QCOMPARE(samples.first().deviceId, QStringLiteral("DEV-001"));

    QList<HeartbeatRecord> heartbeats;
    connect(&source, &IDeviceDataSource::heartbeatGenerated,
            this, [&heartbeats](const QList<HeartbeatRecord> &batch) {
                heartbeats = batch;
            });
    source.triggerHeartbeat();
    QCOMPARE(heartbeats.size(), 2);
    QVERIFY(!heartbeats.at(1).online);
}

QTEST_GUILESS_MAIN(SimulationDataSourceTest)

#include "SimulationDataSourceTest.moc"
