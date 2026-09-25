#include "alarm/AlarmEngine.h"
#include "alarm/AlarmRule.h"
#include "app/AppController.h"
#include "app/MonitoringService.h"
#include "database/DatabaseManager.h"
#include "database/TelemetryRepository.h"
#include "network/IDeviceDataSource.h"
#include "network/SimulationDataSource.h"

#include <QDir>
#include <QPromise>
#include <QSharedPointer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>

#include <utility>

namespace {

template <typename T>
QFuture<T> readyFuture(T result)
{
    auto promise = QSharedPointer<QPromise<T>>::create();
    promise->start();
    QFuture<T> future = promise->future();
    promise->addResult(std::move(result));
    promise->finish();
    return future;
}

} // namespace

#include <chrono>

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

    void emitHeartbeat(const QList<HeartbeatRecord> &heartbeats)
    {
        emit heartbeatGenerated(heartbeats);
    }

private:
    bool m_running = false;
    bool m_collectionEnabled = true;
    QList<DeviceInfo> m_devices;
};

class FakeTelemetryRepository final : public TelemetryRepository
{
public:
    explicit FakeTelemetryRepository(QObject *parent = nullptr)
        : TelemetryRepository(parent)
    {
    }

    bool start(QString *errorMessage = nullptr) override
    {
        if (errorMessage) {
            errorMessage->clear();
        }
        m_running = true;
        emit queueDepthChanged(0, m_options.queueCapacity);
        return true;
    }

    void shutdown() override
    {
        if (!m_running) {
            return;
        }
        m_running = false;
        ++shutdownCount;
        emit queueDepthChanged(0, m_options.queueCapacity);
    }

    bool isRunning() const override
    {
        return m_running;
    }

    TelemetryRepositoryOptions options() const override
    {
        return m_options;
    }

    quint64 submitTelemetryBatch(const QList<TelemetryRecord> &records) override
    {
        ++telemetrySubmissionCount;
        submittedTelemetry = records;
        const quint64 requestId = m_nextRequestId++;
        if (rejectNextTelemetry) {
            rejectNextTelemetry = false;
            rejectReason = QStringLiteral("模拟遥测队列已满");
            emit requestRejected(requestId, rejectReason);
            return 0;
        }

        emit queueDepthChanged(1, m_options.queueCapacity);
        const bool fail = failNextTelemetry;
        failNextTelemetry = false;
        QTimer::singleShot(0, this, [this, requestId, fail, count = records.size()]() {
            emit queueDepthChanged(0, m_options.queueCapacity);
            emit telemetryBatchCompleted(
                requestId, fail ? 0 : count,
                fail ? QStringLiteral("模拟遥测写入失败") : QString());
            if (fail) {
                emit errorOccurred(QStringLiteral("模拟遥测写入失败"));
            }
        });
        return requestId;
    }

    quint64 submitHeartbeatBatch(const QList<HeartbeatRecord> &records) override
    {
        ++heartbeatSubmissionCount;
        const quint64 requestId = m_nextRequestId++;
        emit queueDepthChanged(1, m_options.queueCapacity);
        QTimer::singleShot(0, this, [this, requestId, count = records.size()]() {
            emit queueDepthChanged(0, m_options.queueCapacity);
            emit heartbeatBatchCompleted(requestId, count, QString());
        });
        return requestId;
    }

    QFuture<TelemetryRecordsResult> latestDeviceRecords() override
    {
        ++latestQueryCount;
        return readyFuture(latestTelemetry);
    }

    QFuture<HeartbeatRecordsResult> latestHeartbeatRecords() override
    {
        ++heartbeatQueryCount;
        return readyFuture(latestHeartbeats);
    }

    QFuture<TelemetryRecordsResult> recentTelemetryRecords(
        int, const QString &) override
    {
        ++recentQueryCount;
        return readyFuture(recentTelemetry);
    }

    QFuture<TelemetryRecordsResult> telemetryHistory(
        const QDateTime &, const QDateTime &, const QString &, int) override
    {
        ++historyQueryCount;
        return readyFuture(historyTelemetry);
    }

    QFuture<TelemetryRecordsResult> telemetryBetween(
        const QDateTime &, const QDateTime &) override
    {
        ++betweenQueryCount;
        return readyFuture(historyTelemetry);
    }

    QFuture<TelemetryCountResult> telemetryRecordCount() override
    {
        ++countQueryCount;
        return readyFuture(recordCount);
    }

    TelemetryRepositoryOptions m_options;
    bool rejectNextTelemetry = false;
    bool failNextTelemetry = false;
    int telemetrySubmissionCount = 0;
    int heartbeatSubmissionCount = 0;
    int shutdownCount = 0;
    int latestQueryCount = 0;
    int heartbeatQueryCount = 0;
    int recentQueryCount = 0;
    int historyQueryCount = 0;
    int betweenQueryCount = 0;
    int countQueryCount = 0;
    QList<TelemetryRecord> submittedTelemetry;
    TelemetryRecordsResult latestTelemetry;
    HeartbeatRecordsResult latestHeartbeats;
    TelemetryRecordsResult recentTelemetry;
    TelemetryRecordsResult historyTelemetry;
    TelemetryCountResult recordCount;
    QString rejectReason;

private:
    bool m_running = false;
    quint64 m_nextRequestId = 1;
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

QDateTime utc(const QString &value)
{
    return QDateTime::fromString(value, Qt::ISODate).toUTC();
}

TelemetrySample temperatureSample(const QString &deviceId, double value,
                                  const QDateTime &at)
{
    TelemetrySample sample;
    sample.deviceId = deviceId;
    sample.collectedAt = at.toUTC();
    sample.measurements = {
        {MeasurementType::Temperature, value, QualityCode::Good},
        {MeasurementType::Pressure, 1.2, QualityCode::Good},
        {MeasurementType::Speed, 1500.0, QualityCode::Good},
        {MeasurementType::Voltage, 220.0, QualityCode::Good},
    };
    return sample;
}

TelemetryRecord telemetryRecord(const QString &deviceId)
{
    TelemetryRecord record;
    record.deviceId = deviceId;
    record.name = QStringLiteral("设备 %1").arg(deviceId);
    record.status = TelemetryStatus::Online;
    record.temperature = 55.0;
    record.pressure = 1.1;
    record.speed = 1200.0;
    record.voltage = 220.0;
    record.updatedAt = QDateTime::currentDateTimeUtc();
    return record;
AlarmRule highTemperatureRule(double threshold)
{
    AlarmRule rule;
    rule.ruleId = QStringLiteral("temperature-high");
    rule.type = AlarmRuleType::HighThreshold;
    rule.measurement = MeasurementType::Temperature;
    rule.threshold = threshold;
    rule.severity = AlarmSeverity::Warning;
    return rule;
}

AlarmRule offlineRule(std::chrono::milliseconds timeout)
{
    AlarmRule rule;
    rule.ruleId = QStringLiteral("device-offline");
    rule.type = AlarmRuleType::Offline;
    rule.offlineTimeout = timeout;
    rule.severity = AlarmSeverity::Critical;
    return rule;
}}
} // namespace

class ApplicationLayerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void monitoringServiceDrivesAlarmLifecycle();
    void monitoringServiceDrivesOfflineAlarmRecovery();
    void appControllerForwardsCommands();
    void controllerDestructorStopsSource();
    void controllerSubmitsPersistenceAsynchronously();
    void controllerReportsRejectedAndFailedWrites();
    void controllerQueriesRepositoryWithFutures();
    void repositoryShutdownIsExplicitAndDrainsController();
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

void ApplicationLayerTest::monitoringServiceDrivesAlarmLifecycle()
{
    QDateTime now = utc(QStringLiteral("2026-09-25T10:00:00Z"));
    AlarmEngine alarmEngine;
    alarmEngine.setClock([&now]() { return now; });
    QVERIFY(alarmEngine.addRule(highTemperatureRule(80.0)));

    FakeDataSource source;
    MonitoringService service(&source, &alarmEngine);
    service.setDevices({makeDevice(QStringLiteral("DEV-001"))});

    QList<TelemetryRecord> records;
    QList<AlarmEvent> raised;
    QList<AlarmEvent> acknowledged;
    QList<AlarmEvent> cleared;
    QList<QPair<AlarmState, AlarmState>> transitions;
    connect(&service, &MonitoringService::telemetryBatchReceived,
            this, [&records](const QList<TelemetryRecord> &value) {
                records.append(value);
            });
    connect(&service,
            QOverload<const AlarmEvent &>::of(&MonitoringService::alarmRaised),
            this, [&raised](const AlarmEvent &event) { raised.append(event); });
    connect(&service, &MonitoringService::alarmAcknowledged,
            this, [&acknowledged](const AlarmEvent &event) {
                acknowledged.append(event);
            });
    connect(&service, &MonitoringService::alarmCleared,
            this, [&cleared](const AlarmEvent &event) { cleared.append(event); });
    connect(&service, &MonitoringService::alarmStateChanged,
            this, [&transitions](const AlarmEvent &, AlarmState previous,
                                 AlarmState current) {
                transitions.append({previous, current});
            });

    QVERIFY(service.start());
    QCOMPARE(service.connectionState(), ConnectionState::Connected);
    source.emitTelemetry(
        {temperatureSample(QStringLiteral("DEV-001"), 86.5, now)});

    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().status, TelemetryStatus::Alarm);
    QCOMPARE(records.first().deviceId, QStringLiteral("DEV-001"));
    QCOMPARE(raised.size(), 1);
    QCOMPARE(raised.first().deviceId, QStringLiteral("DEV-001"));
    QCOMPARE(raised.first().state, AlarmState::Active);

    now = now.addSecs(1);
    QVERIFY(service.acknowledgeAlarm(
        raised.first().eventId, QStringLiteral("operator"), now));
    QCOMPARE(acknowledged.size(), 1);
    QCOMPARE(acknowledged.first().state, AlarmState::Acknowledged);
    QCOMPARE(acknowledged.first().acknowledgedBy, QStringLiteral("operator"));

    now = now.addSecs(1);
    source.emitTelemetry(
        {temperatureSample(QStringLiteral("DEV-001"), 70.0, now)});

    QCOMPARE(records.size(), 2);
    QCOMPARE(records.last().status, TelemetryStatus::Online);
    QCOMPARE(cleared.size(), 1);
    QCOMPARE(cleared.first().state, AlarmState::Cleared);
    QCOMPARE(transitions.size(), 4);
    QCOMPARE(transitions.at(0).first, AlarmState::Normal);
    QCOMPARE(transitions.at(0).second, AlarmState::Active);
    QCOMPARE(transitions.at(1).first, AlarmState::Active);
    QCOMPARE(transitions.at(1).second, AlarmState::Acknowledged);
    QCOMPARE(transitions.at(2).first, AlarmState::Acknowledged);
    QCOMPARE(transitions.at(2).second, AlarmState::Cleared);
    QCOMPARE(transitions.at(3).first, AlarmState::Cleared);
    QCOMPARE(transitions.at(3).second, AlarmState::Normal);
}

void ApplicationLayerTest::monitoringServiceDrivesOfflineAlarmRecovery()
{
    QDateTime now = utc(QStringLiteral("2026-09-25T11:00:00Z"));
    AlarmEngine alarmEngine;
    alarmEngine.setClock([&now]() { return now; });
    QVERIFY(alarmEngine.addRule(offlineRule(std::chrono::milliseconds{3000})));

    FakeDataSource source;
    MonitoringService service(&source, &alarmEngine);
    service.setDevices({makeDevice(QStringLiteral("DEV-001"))});

    QList<AlarmEvent> raised;
    QList<AlarmEvent> cleared;
    connect(&service,
            QOverload<const AlarmEvent &>::of(&MonitoringService::alarmRaised),
            this, [&raised](const AlarmEvent &event) { raised.append(event); });
    connect(&service, &MonitoringService::alarmCleared,
            this, [&cleared](const AlarmEvent &event) { cleared.append(event); });

    QVERIFY(service.start());

    HeartbeatRecord heartbeat;
    heartbeat.deviceId = QStringLiteral("DEV-001");
    heartbeat.heartbeatAt = now;
    heartbeat.online = true;
    heartbeat.collecting = true;
    source.emitHeartbeat({heartbeat});

    now = now.addMSecs(2999);
    service.updateAlarmStates(now);
    QCOMPARE(raised.size(), 0);

    now = now.addMSecs(1);
    service.updateAlarmStates(now);
    QCOMPARE(raised.size(), 1);
    QCOMPARE(raised.first().ruleId, QStringLiteral("device-offline"));
    QCOMPARE(raised.first().state, AlarmState::Active);

    now = now.addSecs(1);
    heartbeat.heartbeatAt = now;
    source.emitHeartbeat({heartbeat});

    QCOMPARE(cleared.size(), 1);
    QCOMPARE(cleared.first().ruleId, QStringLiteral("device-offline"));
    QCOMPARE(cleared.first().state, AlarmState::Cleared);
    QVERIFY(service.isDeviceOnline(QStringLiteral("DEV-001")));
}

void ApplicationLayerTest::appControllerForwardsCommands()
{
    FakeDataSource source;
    FakeTelemetryRepository repository;
    QVERIFY(repository.start());
    AppController controller(&source, &repository, QStringLiteral("admin"));

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

void ApplicationLayerTest::controllerDestructorStopsSource()
{
    auto *source = new SimulationDataSource;
    source->setSamplingInterval(1000);
    source->setHeartbeatInterval(1000);
    FakeTelemetryRepository repository;
    QVERIFY(repository.start());

    {
        AppController controller(source, &repository, QStringLiteral("admin"));
        QVERIFY(controller.start());
        QVERIFY(source->isRunning());
    }

    QVERIFY(!source->isRunning());
    delete source;
}

void ApplicationLayerTest::controllerSubmitsPersistenceAsynchronously()
{
    FakeDataSource source;
    FakeTelemetryRepository repository;
    QVERIFY(repository.start());
    AppController controller(&source, &repository, QStringLiteral("admin"));
    QVERIFY(controller.start());

    QSignalSpy receivedSpy(&controller, &AppController::telemetryBatchReceived);
    QSignalSpy statusSpy(&controller, &AppController::persistenceStatusChanged);
    source.emitTelemetry({highTemperatureSample()});

    QCOMPARE(repository.telemetrySubmissionCount, 1);
    QCOMPARE(repository.submittedTelemetry.size(), 1);
    QCOMPARE(controller.acceptedTelemetryBatches(), quint64(1));
    QCOMPARE(controller.persistenceQueueCapacity(), repository.options().queueCapacity);
    QTRY_COMPARE_WITH_TIMEOUT(controller.completedTelemetryBatches(), quint64(1), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.pendingPersistenceRequests(), 0, 3000);
    QCOMPARE(receivedSpy.count(), 1);
    QVERIFY(statusSpy.count() >= 2);
    QVERIFY(controller.lastPersistenceError().isEmpty());
}

void ApplicationLayerTest::controllerReportsRejectedAndFailedWrites()
{
    FakeDataSource source;
    FakeTelemetryRepository repository;
    QVERIFY(repository.start());
    AppController controller(&source, &repository, QStringLiteral("admin"));
    QVERIFY(controller.start());

    QSignalSpy errorSpy(&controller, &AppController::errorOccurred);

    repository.failNextTelemetry = true;
    source.emitTelemetry({highTemperatureSample()});
    QTRY_COMPARE_WITH_TIMEOUT(controller.failedPersistenceBatches(), quint64(1), 3000);
    QVERIFY(controller.lastPersistenceError().contains(QStringLiteral("失败")));

    repository.rejectNextTelemetry = true;
    source.emitTelemetry({highTemperatureSample()});
    QCOMPARE(repository.telemetrySubmissionCount, 2);
    QCOMPARE(controller.rejectedPersistenceRequests(), quint64(1));
    QVERIFY(controller.lastPersistenceError().contains(QStringLiteral("拒绝"))
            || controller.lastPersistenceError().contains(QStringLiteral("已满")));
    QVERIFY(errorSpy.count() >= 2);
}

void ApplicationLayerTest::controllerQueriesRepositoryWithFutures()
{
    FakeDataSource source;
    FakeTelemetryRepository repository;
    QVERIFY(repository.start());
    AppController controller(&source, &repository, QStringLiteral("admin"));

    TelemetryRecordsResult telemetryResult;
    telemetryResult.success = true;
    telemetryResult.records = {telemetryRecord(QStringLiteral("DEV-001"))};
    repository.latestTelemetry = telemetryResult;
    repository.recentTelemetry = telemetryResult;
    repository.historyTelemetry = telemetryResult;
    repository.recordCount.success = true;
    repository.recordCount.count = 1;
    repository.latestHeartbeats.success = true;
    repository.latestHeartbeats.records = {HeartbeatRecord{}};

    QFuture<TelemetryRecordsResult> latest =
        controller.latestDeviceRecordsAsync();
    QTRY_VERIFY_WITH_TIMEOUT(latest.isFinished(), 3000);
    QVERIFY(latest.result().success);
    QCOMPARE(latest.result().records.size(), 1);
    QCOMPARE(repository.latestQueryCount, 1);

    const QDateTime end = QDateTime::currentDateTimeUtc();
    QFuture<TelemetryRecordsResult> history =
        controller.telemetryHistoryAsync(end.addSecs(-60), end,
                                         QStringLiteral("DEV-001"), 100);
    QTRY_VERIFY_WITH_TIMEOUT(history.isFinished(), 3000);
    QVERIFY(history.result().success);
    QCOMPARE(repository.historyQueryCount, 1);

    QFuture<TelemetryCountResult> count = controller.telemetryRecordCountAsync();
    QTRY_VERIFY_WITH_TIMEOUT(count.isFinished(), 3000);
    QCOMPARE(count.result().count, qint64(1));
    QCOMPARE(repository.countQueryCount, 1);

    QFuture<HeartbeatRecordsResult> heartbeats =
        controller.latestHeartbeatRecordsAsync();
    QTRY_VERIFY_WITH_TIMEOUT(heartbeats.isFinished(), 3000);
    QVERIFY(heartbeats.result().success);
    QCOMPARE(repository.heartbeatQueryCount, 1);
}

void ApplicationLayerTest::repositoryShutdownIsExplicitAndDrainsController()
{
    auto *source = new FakeDataSource;
    FakeTelemetryRepository repository;
    QVERIFY(repository.start());

    {
        AppController controller(source, &repository, QStringLiteral("admin"));
        QVERIFY(controller.start());
        QVERIFY(repository.isRunning());
    }

    QVERIFY(!source->isRunning());
    QVERIFY(repository.isRunning());
    repository.shutdown();
    QCOMPARE(repository.shutdownCount, 1);
    QVERIFY(!repository.isRunning());
    delete source;
}

QTEST_GUILESS_MAIN(ApplicationLayerTest)

#include "ApplicationLayerTest.moc"
