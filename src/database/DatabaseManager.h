#pragma once

#include "core/DeviceInfo.h"
#include "core/HeartbeatRecord.h"
#include "core/TelemetryRecord.h"

#include <QDateTime>
#include <QList>
#include <QSqlDatabase>
#include <QString>

class DatabaseManager
{
public:
    static DatabaseManager &instance();

    bool initialize(QString *errorMessage = nullptr);
    bool initializeAt(const QString &dataDirectory, QString *errorMessage = nullptr);
    void shutdown();
    bool validateUser(const QString &username, const QString &password);
    QString roleForUser(const QString &username);
    bool createRememberSession(const QString &username, QString *rawToken,
                               QString *errorMessage = nullptr);
    bool validateRememberSession(const QString &rawToken, QString *username,
                                 QString *errorMessage = nullptr);
    void revokeRememberSession(const QString &rawToken);
    void cleanupExpiredSessions();
    bool insertTelemetryRecords(const QList<TelemetryRecord> &records,
                                QString *errorMessage = nullptr);
    QList<TelemetryRecord> latestDeviceRecords(QString *errorMessage = nullptr);
    qint64 telemetryRecordCount(QString *errorMessage = nullptr);
    QList<HeartbeatRecord> latestHeartbeatRecords(QString *errorMessage = nullptr);
    QList<TelemetryRecord> recentTelemetryRecords(int limit,
                                                   const QString &deviceId = QString(),
                                                   QString *errorMessage = nullptr);
    QList<TelemetryRecord> telemetryHistory(const QDateTime &start,
                                            const QDateTime &end,
                                            const QString &deviceId = QString(),
                                            int limit = 2000,
                                            QString *errorMessage = nullptr);
    QList<TelemetryRecord> telemetryBetween(const QDateTime &start,
                                            const QDateTime &end,
                                            QString *errorMessage = nullptr);
    QList<DeviceInfo> deviceInfos(QString *errorMessage = nullptr);
    bool ensureDeviceInfos(const QList<DeviceInfo> &devices,
                           QString *errorMessage = nullptr);
    bool updateDeviceInfo(const DeviceInfo &device,
                          QString *errorMessage = nullptr);
    bool insertAlarmRecord(const QString &deviceId,
                           const QString &level,
                           const QString &message,
                           QString *errorMessage = nullptr);
    QList<AlarmRecord> alarmHistoryForDevice(const QString &deviceId,
                                             int limit = 500,
                                             QString *errorMessage = nullptr);
    bool insertHeartbeatRecords(const QList<HeartbeatRecord> &records,
                                QString *errorMessage = nullptr);
    bool insertLog(const QString &level, const QString &source,
                   const QString &message, QString *errorMessage = nullptr);
    static QString runtimeDataDirectory();
    QString dataDirectory() const;
    QString databasePath() const;
    QString lastError() const;

    DatabaseManager(const DatabaseManager &) = delete;
    DatabaseManager &operator=(const DatabaseManager &) = delete;

private:
    DatabaseManager();
    ~DatabaseManager();

    bool configureConnection(QString *errorMessage);
    bool importLegacyDatabase(const QString &sourcePath, QString *errorMessage);
    QString legacyDatabasePath() const;
    bool createTables(QString *errorMessage);
    bool normalizeTimestampStorage(QString *errorMessage);
    bool normalizeTelemetryStatusStorage(QString *errorMessage);
    bool ensureColumn(const QString &table, const QString &column,
                      const QString &definition, QString *errorMessage);
    bool ensureDefaultUser(QString *errorMessage);
    QByteArray passwordHash(const QString &password, const QByteArray &salt) const;
    QString generateSaltHex() const;
    QString generateTokenHex() const;
    QString tokenHash(const QString &rawToken) const;

    QString m_connectionName;
    QString m_dataDirectory;
    QString m_databasePath;
    QString m_lastError;
    QSqlDatabase m_database;
    bool m_initialized = false;
};
