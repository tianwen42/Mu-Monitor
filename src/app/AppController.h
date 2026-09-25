#pragma once

#include "alarm/AlarmEvent.h"
#include "core/DeviceInfo.h"
#include "core/HeartbeatRecord.h"
#include "core/TelemetryRecord.h"
#include "database/TelemetryRepository.h"

#include <QFuture>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

class IDeviceDataSource;
class MonitoringService;

class AppController : public QObject
{
    Q_OBJECT

public:
    AppController(IDeviceDataSource *dataSource, TelemetryRepository *repository,
                  const QString &currentUser, QObject *parent = nullptr);
    ~AppController() override;

    QList<DeviceInfo> devices() const;
    QString currentUser() const;
    QString currentUserRole() const;
    QString databasePath() const;

    bool start();
    void stop();
    bool pauseCollection();
    bool resumeCollection();
    bool setDeviceCollection(const QString &deviceId, bool enabled);

    ConnectionState connectionState() const;
    CollectionState collectionState() const;
    bool isDeviceOnline(const QString &deviceId) const;
    bool isDeviceCollecting(const QString &deviceId) const;
    int onlineDeviceCount() const;
    int activeAlarmCount() const;
    QList<AlarmEvent> activeAlarms() const;
    bool acknowledgeAlarm(const QString &eventId);

    // Legacy synchronous readers retained for the current UI. New UI code must
    // use the Async methods below so SQLite work stays off the GUI thread.
    QList<TelemetryRecord> latestDeviceRecords(QString *errorMessage = nullptr) const;
    qint64 telemetryRecordCount(QString *errorMessage = nullptr) const;
    QList<TelemetryRecord> recentTelemetryRecords(
        int limit, const QString &deviceId = QString(),
        QString *errorMessage = nullptr) const;
    QList<TelemetryRecord> telemetryHistory(
        const QDateTime &start, const QDateTime &end,
        const QString &deviceId = QString(), int limit = 2000,
        QString *errorMessage = nullptr) const;
    QList<HeartbeatRecord> latestHeartbeatRecords(
        QString *errorMessage = nullptr) const;
    QList<AlarmRecord> alarmHistoryForDevice(
        const QString &deviceId, int limit = 500,
        QString *errorMessage = nullptr) const;
    bool updateDeviceInfo(const DeviceInfo &device,
                          QString *errorMessage = nullptr) const;

    // Non-blocking query interface used by new UI callers.
    QFuture<TelemetryRecordsResult> latestDeviceRecordsAsync();
    QFuture<TelemetryCountResult> telemetryRecordCountAsync();
    QFuture<TelemetryRecordsResult> recentTelemetryRecordsAsync(
        int limit, const QString &deviceId = QString());
    QFuture<TelemetryRecordsResult> telemetryHistoryAsync(
        const QDateTime &start, const QDateTime &end,
        const QString &deviceId = QString(), int limit = 2000);
    QFuture<HeartbeatRecordsResult> latestHeartbeatRecordsAsync();

    int persistenceQueueDepth() const;
    int persistenceQueueCapacity() const;
    int pendingPersistenceRequests() const;
    quint64 acceptedTelemetryBatches() const;
    quint64 acceptedHeartbeatBatches() const;
    quint64 completedTelemetryBatches() const;
    quint64 completedHeartbeatBatches() const;
    quint64 rejectedPersistenceRequests() const;
    quint64 failedPersistenceBatches() const;
    QString lastPersistenceError() const;

    void logEvent(const QString &level, const QString &source,
                  const QString &message) const;

signals:
    void devicesChanged(const QList<DeviceInfo> &devices);
    void telemetryBatchReceived(const QList<TelemetryRecord> &records);
    void heartbeatBatchReceived(const QList<HeartbeatRecord> &heartbeats);
    void connectionStateChanged(ConnectionState state);
    void collectionStateChanged(CollectionState state);
    void deviceStateChanged(const QString &deviceId, bool online, bool collecting);
    void onlineDeviceCountChanged(int count);
    void alarmRaised(const AlarmEvent &event);
    void alarmRaised(const QString &deviceId, const QString &message);
    void alarmAcknowledged(const AlarmEvent &event);
    void alarmCleared(const AlarmEvent &event);
    void persistenceStatusChanged();
    void persistenceQueueChanged(int depth, int capacity);
    void errorOccurred(const QString &message);

private:
    void loadDevices();
    void persistTelemetry(const QList<TelemetryRecord> &records);
    void persistHeartbeats(const QList<HeartbeatRecord> &heartbeats);
    void persistAlarm(const QString &deviceId, const QString &message);

    void handleTelemetryBatchCompleted(quint64 requestId, int insertedCount,
                                       const QString &error);
    void handleHeartbeatBatchCompleted(quint64 requestId, int insertedCount,
                                       const QString &error);
    void handleRequestRejected(quint64 requestId, const QString &reason);
    void handleQueueDepthChanged(int depth, int capacity);
    void handleRepositoryError(const QString &message);

    IDeviceDataSource *m_dataSource = nullptr;
    TelemetryRepository *m_repository = nullptr;
    MonitoringService *m_monitoringService = nullptr;
    QString m_currentUser;
    QList<DeviceInfo> m_devices;

    QSet<quint64> m_pendingRepositoryRequests;
    int m_persistenceQueueDepth = 0;
    int m_persistenceQueueCapacity = 0;
    quint64 m_acceptedTelemetryBatches = 0;
    quint64 m_acceptedHeartbeatBatches = 0;
    quint64 m_completedTelemetryBatches = 0;
    quint64 m_completedHeartbeatBatches = 0;
    quint64 m_rejectedPersistenceRequests = 0;
    quint64 m_failedPersistenceBatches = 0;
    QString m_lastPersistenceError;
};
