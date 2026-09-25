#include "app/AppController.h"
#include "app/MonitoringService.h"
#include "database/DatabaseManager.h"
#include "network/IDeviceDataSource.h"

#include <QDir>
#include <QStandardPaths>
#include <QTest>

class FakeDataSource : public IDeviceDataSource
{
public:
    bool start() override
    {
        m_running = true;
        emit connectionStateChanged(ConnectionState::Connecting);
        emit connectionStateChanged(ConnectionState::Connected);
        return true;
    }

    void stop() override
    {
        m_running = false;
        emit connectionStateChanged(ConnectionState::Disconnected);
    }

    bool isRunning() const override
    {
        return m_running;
    }

    void setDevices(const QList<DeviceInfo> &devices) override
    {
        m_devices = devices;
    }

    QList<DeviceInfo> devices() const override
    {
        return m_devices;
    }

    bool setCollectionEnabled(bool enabled) override
    {
        m_collectionEnabled = enabled;
        return true;
    }

    bool setDeviceCollectionEnabled(const QString &, bool) override
    {
        return true;
    }

    void emitTelemetry(const QList<TelemetrySample> &samples)
    {
        emit telemetryGenerated(samples);
    }

private:
    bool m_running = false;
    bool m_collectionEnabled = true;
    QList<DeviceInfo> m_devices;
};

namespace {
DeviceInfo makeDevice(const QString &id)
{
    DeviceInfo device;
    device.deviceId = id;
    device.name = QStringLiteral("设备 %1").arg(id);
    device.protocol = QStringLiteral("TCP");
    return device;
}

TelemetrySample highTemperatureSample()
{
    TelemetrySample sample;
    sample.deviceId = QStringLiteral("DEV-001");
    sample.collectedAt = QDateTime::currentDateTimeUtc();
    sample.measurements = {
        {MeasurementType::Temperature, 86.5, QualityCode::Good},
        {MeasurementType::Pressure, 1.2, QualityCode::Good},
        {MeasurementType::Speed, 1500.0, QualityCode::Good},
        {MeasurementType::Voltage, 220.0, QualityCode::Good},
    };
    return sample;
}
}

class ApplicationLayerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void monitoringServiceForwardsAndMarksAlarm();
    void appControllerForwardsCommands();
};

void ApplicationLayerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("Mu-MonitorTests"));
    QCoreApplication::setApplicationName(QStringLiteral("ApplicationLayerTest"));

    const QString dataDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QVERIFY(!dataDirectory.isEmpty());
    QDir(dataDirectory).removeRecursively();
    QVERIFY(QDir().mkpath(dataDirectory));

    QString errorMessage;
    QVERIFY2(DatabaseManager::instance().initialize(&errorMessage),
             qPrintable(errorMessage));
}

void ApplicationLayerTest::cleanupTestCase()
{
    DatabaseManager::instance().shutdown();
}

void ApplicationLayerTest::monitoringServiceForwardsAndMarksAlarm()
{
    FakeDataSource source;
    MonitoringService service(&source);
    service.setDevices({makeDevice(QStringLiteral("DEV-001"))});

    QList<TelemetryRecord> receivedRecords;
    QString alarmDeviceId;
    connect(&service, &MonitoringService::telemetryBatchReceived,
            this, [&receivedRecords](const QList<TelemetryRecord> &records) {
                receivedRecords = records;
            });
    connect(&service, &MonitoringService::alarmRaised,
            this, [&alarmDeviceId](const QString &deviceId, const QString &) {
                alarmDeviceId = deviceId;
            });

    QVERIFY(service.start());
    QCOMPARE(service.connectionState(), ConnectionState::Connected);
    source.emitTelemetry({highTemperatureSample()});

    QCOMPARE(receivedRecords.size(), 1);
    QCOMPARE(receivedRecords.first().status, TelemetryStatus::Alarm);
    QCOMPARE(receivedRecords.first().deviceId, QStringLiteral("DEV-001"));
    QCOMPARE(alarmDeviceId, QStringLiteral("DEV-001"));
}

void ApplicationLayerTest::appControllerForwardsCommands()
{
    FakeDataSource source;
    AppController controller(&source, QStringLiteral("admin"));

    QCOMPARE(controller.devices().size(), 100);
    QVERIFY(controller.start());
    QVERIFY(source.isRunning());
    QCOMPARE(controller.connectionState(), ConnectionState::Connected);

    QVERIFY(controller.pauseCollection());
    QCOMPARE(controller.collectionState(), CollectionState::Paused);
    QVERIFY(controller.resumeCollection());
    QCOMPARE(controller.collectionState(), CollectionState::Running);

    const QString deviceId = controller.devices().first().deviceId;
    QVERIFY(controller.setDeviceCollection(deviceId, false));
    QVERIFY(!controller.isDeviceCollecting(deviceId));
    QVERIFY(controller.setDeviceCollection(deviceId, true));
    QVERIFY(controller.isDeviceCollecting(deviceId));

    controller.stop();
    QVERIFY(!source.isRunning());
    QCOMPARE(controller.connectionState(), ConnectionState::Disconnected);
}

QTEST_GUILESS_MAIN(ApplicationLayerTest)

#include "ApplicationLayerTest.moc"