#include "database/DatabaseManager.h"

#include "utils/TimeUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTimeZone>
#include <QtTest>

class DatabaseManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void initializesDefaultUserAndRole();
    void rejectsInvalidCredentials();
    void rememberSessionRoundTrip();
    void sessionTokenIsHashedAndExpiryIsEnforced();
    void storesTelemetryAndQueriesTimeRange();
    void latestRecordReturnsOneWhenTimestampsTie();
    void persistsTelemetryAcrossReinitialize();
    void storesHeartbeatAndSystemLog();

private:
    bool removeTestDataDirectory() const;
    bool initialize(QString *errorMessage = nullptr);

    QString m_dataDirectory;
};

void DatabaseManagerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("Mu-MonitorTests"));
    QCoreApplication::setApplicationName(
        QStringLiteral("Mu-MonitorDatabaseTest-%1").arg(QCoreApplication::applicationPid()));
    m_dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QVERIFY2(!m_dataDirectory.isEmpty(), "AppDataLocation must not be empty");
}

void DatabaseManagerTest::init()
{
    DatabaseManager::instance().shutdown();
    removeTestDataDirectory();
}

void DatabaseManagerTest::cleanup()
{
    DatabaseManager::instance().shutdown();
    removeTestDataDirectory();
}

bool DatabaseManagerTest::removeTestDataDirectory() const
{
    if (m_dataDirectory.isEmpty()) {
        return false;
    }

    const QString normalized = QDir::fromNativeSeparators(m_dataDirectory).toLower();
    if (!normalized.contains(QStringLiteral("qttest"))
        && !normalized.contains(QStringLiteral("mu-monitordatabasetest"))) {
        return false;
    }

    return QDir(m_dataDirectory).removeRecursively() || !QFileInfo::exists(m_dataDirectory);
}

bool DatabaseManagerTest::initialize(QString *errorMessage)
{
    QString message;
    const bool initialized =
        DatabaseManager::instance().initializeAt(m_dataDirectory, &message);
    if (errorMessage) {
        *errorMessage = message;
    }
    return initialized;
}

void DatabaseManagerTest::initializesDefaultUserAndRole()
{
    QVERIFY(initialize());
    QVERIFY(QFileInfo::exists(DatabaseManager::instance().databasePath()));
    QCOMPARE(QFileInfo(DatabaseManager::instance().databasePath()).absolutePath(),
             QFileInfo(m_dataDirectory).absoluteFilePath());
    QCOMPARE(DatabaseManager::instance().roleForUser(QStringLiteral("admin")),
             QStringLiteral("admin"));
}

void DatabaseManagerTest::rejectsInvalidCredentials()
{
    QVERIFY(initialize());
    QVERIFY(DatabaseManager::instance().validateUser(QStringLiteral("admin"),
                                                     QStringLiteral("123456")));
    QVERIFY(!DatabaseManager::instance().validateUser(QStringLiteral("admin"),
                                                      QStringLiteral("wrong-password")));
    QVERIFY(!DatabaseManager::instance().validateUser(QStringLiteral("missing"),
                                                      QStringLiteral("123456")));
}

void DatabaseManagerTest::rememberSessionRoundTrip()
{
    QVERIFY(initialize());

    QString token;
    QString errorMessage;
    QVERIFY2(DatabaseManager::instance().createRememberSession(
                 QStringLiteral("admin"), &token, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY(!token.isEmpty());

    QString username;
    QVERIFY2(DatabaseManager::instance().validateRememberSession(
                 token, &username, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(username, QStringLiteral("admin"));

    DatabaseManager::instance().revokeRememberSession(token);
    QVERIFY(!DatabaseManager::instance().validateRememberSession(token, &username));
}

void DatabaseManagerTest::sessionTokenIsHashedAndExpiryIsEnforced()
{
    QVERIFY(initialize());

    QString token;
    QString errorMessage;
    QVERIFY2(DatabaseManager::instance().createRememberSession(
                 QStringLiteral("admin"), &token, &errorMessage),
             qPrintable(errorMessage));

    const QString connectionName = QStringLiteral("DatabaseManagerTestInspection");
    {
        QSqlDatabase inspection = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                            connectionName);
        inspection.setDatabaseName(DatabaseManager::instance().databasePath());
        QVERIFY2(inspection.open(), qPrintable(inspection.lastError().text()));

        QSqlQuery query(inspection);
        query.prepare(QStringLiteral(
            "SELECT token_hash, expires_at FROM sessions WHERE username = ?"));
        query.addBindValue(QStringLiteral("admin"));
        QVERIFY(query.exec());
        QVERIFY(query.next());

        const QString storedHash = query.value(0).toString();
        const QDateTime expiresAt = TimeUtils::fromIso8601(query.value(1).toString());
        QVERIFY(storedHash != token);
        QCOMPARE(storedHash.size(), 64);
        QVERIFY(expiresAt.toUTC() > QDateTime::currentDateTimeUtc().addDays(29));

        QSqlQuery expire(inspection);
        expire.prepare(QStringLiteral(
            "UPDATE sessions SET expires_at = ? WHERE username = ?"));
        expire.addBindValue(TimeUtils::toUtcIso8601(
            QDateTime::currentDateTimeUtc().addSecs(-1)));
        expire.addBindValue(QStringLiteral("admin"));
        QVERIFY(expire.exec());

        inspection.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    QVERIFY(!DatabaseManager::instance().validateRememberSession(token, nullptr, &errorMessage));
    QVERIFY(!errorMessage.isEmpty());
}

void DatabaseManagerTest::storesTelemetryAndQueriesTimeRange()
{
    QVERIFY(initialize());

    const QDateTime base(QDate(2026, 9, 20), QTime(10, 0), QTimeZone::UTC);
    QList<TelemetryRecord> records;
    for (int i = 0; i < 3; ++i) {
        TelemetryRecord record;
        record.deviceId = i < 2 ? QStringLiteral("DEV-001") : QStringLiteral("DEV-002");
        record.name = QStringLiteral("测试设备 %1").arg(i + 1);
        record.status = TelemetryStatus::Online;
        record.temperature = 60.0 + i;
        record.pressure = 1.1 + i * 0.1;
        record.speed = 1400.0 + i * 100.0;
        record.voltage = 220.0 + i;
        record.updatedAt = base.addSecs(i * 10);
        records.append(record);
    }

    QString errorMessage;
    QVERIFY2(DatabaseManager::instance().insertTelemetryRecords(records, &errorMessage),
             qPrintable(errorMessage));

    const QList<TelemetryRecord> latest =
        DatabaseManager::instance().latestDeviceRecords(&errorMessage);
    QCOMPARE(latest.size(), 2);

    const QList<TelemetryRecord> range = DatabaseManager::instance().telemetryBetween(
        base.addSecs(-1), base.addSecs(21), &errorMessage);
    QCOMPARE(range.size(), 3);
    QCOMPARE(range.constFirst().deviceId, QStringLiteral("DEV-001"));
    QCOMPARE(range.constFirst().updatedAt.toUTC(), base);
    QCOMPARE(range.constLast().deviceId, QStringLiteral("DEV-002"));
}

void DatabaseManagerTest::latestRecordReturnsOneWhenTimestampsTie()
{
    QVERIFY(initialize());

    const QDateTime timestamp =
        QDateTime(QDate(2026, 9, 20), QTime(11, 0), QTimeZone::UTC);
    TelemetryRecord older;
    older.deviceId = QStringLiteral("DEV-100");
    older.name = QStringLiteral("重复时间设备");
    older.status = TelemetryStatus::Online;
    older.temperature = 10.0;
    older.updatedAt = timestamp;

    TelemetryRecord newer = older;
    newer.temperature = 20.0;

    QString errorMessage;
    QVERIFY2(DatabaseManager::instance().insertTelemetryRecords({older, newer}, &errorMessage),
             qPrintable(errorMessage));

    const QList<TelemetryRecord> latest =
        DatabaseManager::instance().latestDeviceRecords(&errorMessage);
    QCOMPARE(latest.size(), 1);
    QCOMPARE(latest.constFirst().deviceId, QStringLiteral("DEV-100"));
    QCOMPARE(latest.constFirst().status, TelemetryStatus::Online);
    QCOMPARE(latest.constFirst().temperature, 20.0);
}

void DatabaseManagerTest::persistsTelemetryAcrossReinitialize()
{
    QVERIFY(initialize());

    TelemetryRecord record;
    record.deviceId = QStringLiteral("DEV-009");
    record.name = QStringLiteral("持久化测试设备");
    record.status = TelemetryStatus::Online;
    record.temperature = 66.6;
    record.pressure = 1.33;
    record.speed = 1777.0;
    record.voltage = 219.5;
    record.updatedAt = QDateTime::currentDateTimeUtc();

    QString errorMessage;
    QVERIFY2(DatabaseManager::instance().insertTelemetryRecords({record}, &errorMessage),
             qPrintable(errorMessage));

    DatabaseManager::instance().shutdown();
    QVERIFY2(initialize(&errorMessage), qPrintable(errorMessage));

    const QList<TelemetryRecord> records =
        DatabaseManager::instance().latestDeviceRecords(&errorMessage);
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.constFirst().deviceId, QStringLiteral("DEV-009"));
    QCOMPARE(records.constFirst().status, TelemetryStatus::Online);
    QCOMPARE(records.constFirst().temperature, 66.6);
}

void DatabaseManagerTest::storesHeartbeatAndSystemLog()
{
    QVERIFY(initialize());

    HeartbeatRecord heartbeat;
    heartbeat.deviceId = QStringLiteral("DEV-001");
    heartbeat.heartbeatAt = QDateTime::currentDateTimeUtc();
    heartbeat.online = true;
    heartbeat.collecting = true;
    heartbeat.latencyMs = 42;

    QString errorMessage;
    QVERIFY2(DatabaseManager::instance().insertHeartbeatRecords({heartbeat}, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(DatabaseManager::instance().insertLog(
                 QStringLiteral("INFO"), QStringLiteral("test"),
                 QStringLiteral("数据库测试日志"), &errorMessage),
             qPrintable(errorMessage));
}

QTEST_MAIN(DatabaseManagerTest)

#include "DatabaseManagerTest.moc"
