#pragma once

#include "core/HeartbeatRecord.h"
#include "core/TelemetryRecord.h"

#include <QDateTime>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

class QThread;

struct TelemetryRepositoryOptions
{
    int batchSize = 500;
    int queueCapacity = 2048;
    int busyTimeoutMs = 5000;
};

struct TelemetryRecordsResult
{
    bool success = false;
    QList<TelemetryRecord> records;
    QString error;
};

struct HeartbeatRecordsResult
{
    bool success = false;
    QList<HeartbeatRecord> records;
    QString error;
};

struct TelemetryCountResult
{
    bool success = false;
    qint64 count = 0;
    QString error;
};

class TelemetryRepository : public QObject
{
    Q_OBJECT

public:
    explicit TelemetryRepository(QObject *parent = nullptr);
    ~TelemetryRepository() override;

    virtual bool start(QString *errorMessage = nullptr) = 0;
    virtual void shutdown() = 0;
    virtual bool isRunning() const = 0;
    virtual TelemetryRepositoryOptions options() const = 0;

    // Returns a non-zero request id when the batch was accepted. A zero id
    // means the queue was full, the repository was stopping, or the batch was empty.
    virtual quint64 submitTelemetryBatch(
        const QList<TelemetryRecord> &records) = 0;
    virtual quint64 submitHeartbeatBatch(
        const QList<HeartbeatRecord> &records) = 0;

    virtual QFuture<TelemetryRecordsResult> latestDeviceRecords() = 0;
    virtual QFuture<HeartbeatRecordsResult> latestHeartbeatRecords() = 0;
    virtual QFuture<TelemetryRecordsResult> recentTelemetryRecords(
        int limit, const QString &deviceId = QString()) = 0;
    virtual QFuture<TelemetryRecordsResult> telemetryHistory(
        const QDateTime &start, const QDateTime &end,
        const QString &deviceId = QString(), int limit = 2000) = 0;
    virtual QFuture<TelemetryRecordsResult> telemetryBetween(
        const QDateTime &start, const QDateTime &end) = 0;
    virtual QFuture<TelemetryCountResult> telemetryRecordCount() = 0;

signals:
    void databaseThreadStarted(QThread *thread);
    void telemetryBatchCompleted(quint64 requestId, int insertedCount,
                                 const QString &error);
    void heartbeatBatchCompleted(quint64 requestId, int insertedCount,
                                 const QString &error);
    void requestRejected(quint64 requestId, const QString &reason);
    void queueDepthChanged(int depth, int capacity);
    void errorOccurred(const QString &message);
};
