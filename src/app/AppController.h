#pragma once

#include "core/DeviceInfo.h"
#include "core/HeartbeatRecord.h"
#include "core/TelemetryRecord.h"

#include <QList>
#include <QObject>
#include <QString>

class IDeviceDataSource;
class MonitoringService;

class AppController : public QObject
{
    Q_OBJECT

public:
    AppController(IDeviceDataSource *dataSource, const QString &currentUser,
                  QObject *parent = nullptr);

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
    void alarmRaised(const QString &deviceId, const QString &message);
    void errorOccurred(const QString &message);

private:
    void loadDevices();
    void persistTelemetry(const QList<TelemetryRecord> &records);
    void persistHeartbeats(const QList<HeartbeatRecord> &heartbeats);
    void persistAlarm(const QString &deviceId, const QString &message);

    IDeviceDataSource *m_dataSource = nullptr;
    MonitoringService *m_monitoringService = nullptr;
    QString m_currentUser;
    QList<DeviceInfo> m_devices;
};
