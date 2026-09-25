#include "database/DatabaseManager.h"
#include "database/SqliteTelemetryRepository.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <atomic>
#include <memory>
#include <thread>

namespace {

QList<TelemetryRecord> telemetryRecords(int count, const QString &deviceId,
                                        const QDateTime &base)
{
    QList<TelemetryRecord> records;
    records.reserve(count);
    for (int index = 0; index < count; ++index) {
        TelemetryRecord record;
        record.deviceId = deviceId;
        record.name = QStringLiteral("压力采集器 %1").arg(deviceId);
        record.status = TelemetryStatus::Online;
        record.temperature = 50.0 + index;
        record.pressure = 0.8 + index * 0.01;
        record.speed = 1200.0 + index;
        record.voltage = 220.0 + index * 0.1;
        record.updatedAt = base.addSecs(index * 10);
        records.append(record);
    }
    return records;
}

QList<HeartbeatRecord> heartbeatRecords(int count, const QDateTime &base)
{
    QList<HeartbeatRecord> records;
    records.reserve(count);
    for (int index = 0; index < count; ++index) {
        HeartbeatRecord record;
        record.deviceId = QStringLiteral("DEV-%1").arg(index + 1, 3, 10, QLatin1Char('0'));
        record.heartbeatAt = base.addSecs(index);
        record.online = true;
        record.collecting = index % 2 == 0;
        record.latencyMs = 10 + index;
        records.append(record);
    }
    return records;
}

} // namespace

class TelemetryRepositoryTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void startsDedicatedDatabaseThread();
    void writesTelemetryAndHeartbeatsInBatches();
    void queriesLatestDeviceDataTimeRangeAndCount();
    void acceptsConcurrentSubmissions();
    void rejectsWhenQueueIsFull();
    void shutdownDrainsAcceptedWorkAndRejectsNewSubmissions();
    void reportsBusyDatabaseWithoutBlockingCaller();
    void rejectsInvalidDatabasePath();

private:
    std::unique_ptr<QTemporaryDir> m_temporaryDirectory;
    QString m_dataDirectory;
    QString m_databasePath;
};

void TelemetryRepositoryTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("Mu-MonitorTests"));
    QCoreApplication::setApplicationName(QStringLiteral("TelemetryRepositoryTest"));
}

void TelemetryRepositoryTest::init()
{
    DatabaseManager::instance().shutdown();
    m_temporaryDirectory = std::make_unique<QTemporaryDir>();
    QVERIFY(m_temporaryDirectory->isValid());
    m_dataDirectory = QDir(m_temporaryDirectory->path()).filePath(QStringLiteral("data"));
    qputenv("MU_MONITOR_DATA_DIR", m_dataDirectory.toUtf8());

    QString errorMessage;
    QVERIFY2(DatabaseManager::instance().initialize(&errorMessage), qPrintable(errorMessage));
    m_databasePath = DatabaseManager::instance().databasePath();
    QVERIFY(QFileInfo::exists(m_databasePath));
}

void TelemetryRepositoryTest::cleanup()
{
    DatabaseManager::instance().shutdown();
    qunsetenv("MU_MONITOR_DATA_DIR");
    m_temporaryDirectory.reset();
}

void TelemetryRepositoryTest::startsDedicatedDatabaseThread()
{
    SqliteTelemetryRepository repository(m_databasePath);
    QSignalSpy threadSpy(&repository, &TelemetryRepository::databaseThreadStarted);

    QString errorMessage;
    QVERIFY2(repository.start(&errorMessage), qPrintable(errorMessage));
    QTRY_COMPARE_WITH_TIMEOUT(threadSpy.count(), 1, 3000);

    QThread *workerThread = threadSpy.constFirst().constFirst().value<QThread *>();
    QVERIFY(workerThread != nullptr);
    QVERIFY(workerThread != QThread::currentThread());
    QVERIFY(repository.isRunning());

    repository.shutdown();
    QVERIFY(!repository.isRunning());
}

void TelemetryRepositoryTest::writesTelemetryAndHeartbeatsInBatches()
{
    TelemetryRepositoryOptions options;
    options.batchSize = 4;
    options.queueCapacity = 32;
    SqliteTelemetryRepository repository(m_databasePath, options);

    QSignalSpy telemetrySpy(&repository, &TelemetryRepository::telemetryBatchCompleted);
    QSignalSpy heartbeatSpy(&repository, &TelemetryRepository::heartbeatBatchCompleted);

    QString errorMessage;
    QVERIFY2(repository.start(&errorMessage), qPrintable(errorMessage));

    const QDateTime base(QDate(2026, 9, 25), QTime(8, 0), QTimeZone::UTC);
    const QList<TelemetryRecord> telemetry = telemetryRecords(17, QStringLiteral("DEV-001"), base);
    const QList<HeartbeatRecord> heartbeats = heartbeatRecords(3, base);

    const quint64 telemetryId = repository.submitTelemetryBatch(telemetry);
    const quint64 heartbeatId = repository.submitHeartbeatBatch(heartbeats);
    QVERIFY(telemetryId != 0);
    QVERIFY(heartbeatId != 0);

    QTRY_COMPARE_WITH_TIMEOUT(telemetrySpy.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(heartbeatSpy.count(), 1, 5000);

    const QList<QVariant> telemetryResult = telemetrySpy.constFirst();
    QCOMPARE(telemetryResult.at(0).toULongLong(), telemetryId);
    QCOMPARE(telemetryResult.at(1).toInt(), telemetry.size());
    QVERIFY2(telemetryResult.at(2).toString().isEmpty(),
             qPrintable(telemetryResult.at(2).toString()));

    const QList<QVariant> heartbeatResult = heartbeatSpy.constFirst();
    QCOMPARE(heartbeatResult.at(0).toULongLong(), heartbeatId);
    QCOMPARE(heartbeatResult.at(1).toInt(), heartbeats.size());
    QVERIFY2(heartbeatResult.at(2).toString().isEmpty(),
             qPrintable(heartbeatResult.at(2).toString()));
}

void TelemetryRepositoryTest::queriesLatestDeviceDataTimeRangeAndCount()
{
    SqliteTelemetryRepository repository(m_databasePath);
    QSignalSpy completed(&repository, &TelemetryRepository::telemetryBatchCompleted);

    QString errorMessage;
    QVERIFY2(repository.start(&errorMessage), qPrintable(errorMessage));

    const QDateTime base(QDate(2026, 9, 25), QTime(9, 0), QTimeZone::UTC);
    QList<TelemetryRecord> records;
    records.append(telemetryRecords(3, QStringLiteral("DEV-001"), base));
    records.append(telemetryRecords(2, QStringLiteral("DEV-002"), base.addSecs(1)));

    QVERIFY(repository.submitTelemetryBatch(records) != 0);
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 5000);

    QFuture<TelemetryCountResult> countFuture = repository.telemetryRecordCount();
    QTRY_VERIFY_WITH_TIMEOUT(countFuture.isFinished(), 5000);
    const TelemetryCountResult countResult = countFuture.result();
    QVERIFY2(countResult.success, qPrintable(countResult.error));
    QCOMPARE(countResult.count, 5);

    QFuture<TelemetryRecordsResult> latestFuture = repository.latestDeviceRecords();
    QTRY_VERIFY_WITH_TIMEOUT(latestFuture.isFinished(), 5000);
    const TelemetryRecordsResult latest = latestFuture.result();
    QVERIFY2(latest.success, qPrintable(latest.error));
    QCOMPARE(latest.records.size(), 2);
    QCOMPARE(latest.records.at(0).deviceId, QStringLiteral("DEV-001"));
    QCOMPARE(latest.records.at(1).deviceId, QStringLiteral("DEV-002"));
    QCOMPARE(latest.records.at(0).updatedAt.toUTC(), base.addSecs(20));
    QCOMPARE(latest.records.at(1).updatedAt.toUTC(), base.addSecs(11));

    QFuture<TelemetryRecordsResult> rangeFuture = repository.telemetryBetween(
        base.addSecs(-1), base.addSecs(19));
    QTRY_VERIFY_WITH_TIMEOUT(rangeFuture.isFinished(), 5000);
    const TelemetryRecordsResult range = rangeFuture.result();
    QVERIFY2(range.success, qPrintable(range.error));
    QCOMPARE(range.records.size(), 4);

    QFuture<HeartbeatRecordsResult> heartbeatFuture = repository.latestHeartbeatRecords();
    QTRY_VERIFY_WITH_TIMEOUT(heartbeatFuture.isFinished(), 5000);
    QVERIFY2(heartbeatFuture.result().success,
             qPrintable(heartbeatFuture.result().error));
    QVERIFY(heartbeatFuture.result().records.isEmpty());
}

void TelemetryRepositoryTest::acceptsConcurrentSubmissions()
{
    TelemetryRepositoryOptions options;
    options.batchSize = 7;
    options.queueCapacity = 512;
    SqliteTelemetryRepository repository(m_databasePath, options);

    QSignalSpy completed(&repository, &TelemetryRepository::telemetryBatchCompleted);
    QString errorMessage;
    QVERIFY2(repository.start(&errorMessage), qPrintable(errorMessage));

    constexpr int threadCount = 4;
    constexpr int batchesPerThread = 25;
    constexpr int recordsPerBatch = 4;
    std::atomic<int> accepted{0};
    std::atomic<int> rejected{0};
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    const QDateTime base = QDateTime::currentDateTimeUtc();
    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex) {
        threads.emplace_back([&, threadIndex]() {
            for (int batchIndex = 0; batchIndex < batchesPerThread; ++batchIndex) {
                const QString deviceId = QStringLiteral("DEV-%1")
                                             .arg(threadIndex + 1, 3, 10, QLatin1Char('0'));
                const QList<TelemetryRecord> records = telemetryRecords(
                    recordsPerBatch, deviceId, base.addMSecs(batchIndex));
                if (repository.submitTelemetryBatch(records) == 0) {
                    ++rejected;
                } else {
                    ++accepted;
                }
            }
        });
    }

    for (std::thread &thread : threads) {
        thread.join();
    }

    QCOMPARE(rejected.load(), 0);
    QCOMPARE(accepted.load(), threadCount * batchesPerThread);
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), accepted.load(), 15000);

    QFuture<TelemetryCountResult> countFuture = repository.telemetryRecordCount();
    QTRY_VERIFY_WITH_TIMEOUT(countFuture.isFinished(), 5000);
    QVERIFY2(countFuture.result().success, qPrintable(countFuture.result().error));
    QCOMPARE(countFuture.result().count,
             static_cast<qint64>(threadCount * batchesPerThread * recordsPerBatch));
}

void TelemetryRepositoryTest::rejectsWhenQueueIsFull()
{
    TelemetryRepositoryOptions options;
    options.batchSize = 1;
    options.queueCapacity = 1;
    options.busyTimeoutMs = 300;
    SqliteTelemetryRepository repository(m_databasePath, options);

    QSignalSpy rejected(&repository, &TelemetryRepository::requestRejected);
    QSignalSpy completed(&repository, &TelemetryRepository::telemetryBatchCompleted);
    QString errorMessage;
    QVERIFY2(repository.start(&errorMessage), qPrintable(errorMessage));

    const QString connectionName = QStringLiteral("repository-blocker");
    int acceptedCount = 0;
    {
        QSqlDatabase blocker = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                         connectionName);
        blocker.setDatabaseName(m_databasePath);
        QVERIFY2(blocker.open(), qPrintable(blocker.lastError().text()));
        QSqlQuery lockQuery(blocker);
        QVERIFY2(lockQuery.exec(QStringLiteral("BEGIN IMMEDIATE")),
                 qPrintable(lockQuery.lastError().text()));

        const QDateTime base = QDateTime::currentDateTimeUtc();
        const quint64 first = repository.submitTelemetryBatch(
            telemetryRecords(1, QStringLiteral("DEV-001"), base));
        const quint64 second = repository.submitTelemetryBatch(
            telemetryRecords(1, QStringLiteral("DEV-002"), base));
        const quint64 third = repository.submitTelemetryBatch(
            telemetryRecords(1, QStringLiteral("DEV-003"), base));
        acceptedCount = (first != 0 ? 1 : 0) + (second != 0 ? 1 : 0)
            + (third != 0 ? 1 : 0);

        QVERIFY(acceptedCount >= 1);
        QVERIFY(acceptedCount <= 2);
        QVERIFY(!rejected.isEmpty());

        QVERIFY2(lockQuery.exec(QStringLiteral("ROLLBACK")),
                 qPrintable(lockQuery.lastError().text()));
        blocker.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), acceptedCount, 5000);
}

void TelemetryRepositoryTest::shutdownDrainsAcceptedWorkAndRejectsNewSubmissions()
{
    TelemetryRepositoryOptions options;
    options.batchSize = 2;
    options.queueCapacity = 64;
    SqliteTelemetryRepository repository(m_databasePath, options);

    QString errorMessage;
    QVERIFY2(repository.start(&errorMessage), qPrintable(errorMessage));

    const QDateTime base = QDateTime::currentDateTimeUtc();
    for (int index = 0; index < 5; ++index) {
        QVERIFY(repository.submitTelemetryBatch(
                    telemetryRecords(4, QStringLiteral("DEV-%1").arg(index + 1), base))
                != 0);
    }

    repository.shutdown();
    QVERIFY(!repository.isRunning());
    QCOMPARE(repository.submitTelemetryBatch(
                 telemetryRecords(1, QStringLiteral("DEV-X"), base)),
             0U);

    QFuture<TelemetryCountResult> closedQuery = repository.telemetryRecordCount();
    QTRY_VERIFY_WITH_TIMEOUT(closedQuery.isFinished(), 3000);
    QVERIFY(!closedQuery.result().success);
    QVERIFY(!closedQuery.result().error.isEmpty());

    QString countError;
    QCOMPARE(DatabaseManager::instance().telemetryRecordCount(&countError), 20);
    QVERIFY2(countError.isEmpty(), qPrintable(countError));

    repository.shutdown();
    QVERIFY(!repository.isRunning());
}

void TelemetryRepositoryTest::reportsBusyDatabaseWithoutBlockingCaller()
{
    TelemetryRepositoryOptions options;
    options.batchSize = 1;
    options.queueCapacity = 8;
    options.busyTimeoutMs = 150;
    SqliteTelemetryRepository repository(m_databasePath, options);

    QSignalSpy completed(&repository, &TelemetryRepository::telemetryBatchCompleted);
    QString errorMessage;
    QVERIFY2(repository.start(&errorMessage), qPrintable(errorMessage));

    const QString connectionName = QStringLiteral("repository-busy-blocker");
    {
        QSqlDatabase blocker = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                         connectionName);
        blocker.setDatabaseName(m_databasePath);
        QVERIFY2(blocker.open(), qPrintable(blocker.lastError().text()));
        QSqlQuery lockQuery(blocker);
        QVERIFY2(lockQuery.exec(QStringLiteral("BEGIN IMMEDIATE")),
                 qPrintable(lockQuery.lastError().text()));

        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(repository.submitTelemetryBatch(
                    telemetryRecords(1, QStringLiteral("DEV-001"),
                                     QDateTime::currentDateTimeUtc()))
                != 0);
        QVERIFY(elapsed.elapsed() < 100);

        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 3000);
        const QList<QVariant> result = completed.constFirst();
        QCOMPARE(result.at(1).toInt(), 0);
        QVERIFY(!result.at(2).toString().isEmpty());

        QVERIFY2(lockQuery.exec(QStringLiteral("ROLLBACK")),
                 qPrintable(lockQuery.lastError().text()));
        blocker.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

void TelemetryRepositoryTest::rejectsInvalidDatabasePath()
{
    SqliteTelemetryRepository repository(m_temporaryDirectory->path());
    QSignalSpy threadSpy(&repository, &TelemetryRepository::databaseThreadStarted);

    QString errorMessage;
    QVERIFY(!repository.start(&errorMessage));
    QVERIFY(!errorMessage.isEmpty());
    QVERIFY(!repository.isRunning());
    QCOMPARE(threadSpy.count(), 0);
}

QTEST_MAIN(TelemetryRepositoryTest)

#include "TelemetryRepositoryTest.moc"
