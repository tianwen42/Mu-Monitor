#pragma once

#include "database/TelemetryRepository.h"

#include <memory>

class SqliteTelemetryRepositoryPrivate;

class SqliteTelemetryRepository final : public TelemetryRepository
{
    Q_OBJECT

public:
    // If databasePath is empty, the same DataDirectory rules as DatabaseManager
    // are used. The database schema must already have been initialized and
    // migrated by DatabaseManager.
    explicit SqliteTelemetryRepository(
        const QString &databasePath = QString(),
        const TelemetryRepositoryOptions &options = {},
        QObject *parent = nullptr);
    ~SqliteTelemetryRepository() override;

    bool start(QString *errorMessage = nullptr) override;
    void shutdown() override;
    bool isRunning() const override;
    TelemetryRepositoryOptions options() const override;

    quint64 submitTelemetryBatch(
        const QList<TelemetryRecord> &records) override;
    quint64 submitHeartbeatBatch(
        const QList<HeartbeatRecord> &records) override;

    QFuture<TelemetryRecordsResult> latestDeviceRecords() override;
    QFuture<HeartbeatRecordsResult> latestHeartbeatRecords() override;
    QFuture<TelemetryRecordsResult> telemetryBetween(
        const QDateTime &start, const QDateTime &end) override;
    QFuture<TelemetryCountResult> telemetryRecordCount() override;

private:
    std::unique_ptr<SqliteTelemetryRepositoryPrivate> d;
};
