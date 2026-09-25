#pragma once

#include "alarm/AlarmEvent.h"
#include "core/DeviceInfo.h"
#include "core/HeartbeatRecord.h"
#include "core/TelemetryRecord.h"
#include "core/TelemetrySample.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QTimer>

class AlarmEngine;
class IDeviceDataSource;

class MonitoringService : public QObject
{
    Q_OBJECT

public:
    explicit MonitoringService(IDeviceDataSource *dataSource, QObject *parent = nullptr);
    MonitoringService(IDeviceDataSource *dataSource, AlarmEngine *alarmEngine,
                      QObject *parent = nullptr);

    void setDevices(const QList<DeviceInfo> &devices);
    void registerDevice(const DeviceInfo &device);
    QList<DeviceInfo> devices() const;

    bool start();
    void stop();
    bool pauseCollection();
    bool resumeCollection();
    bool setDeviceCollection(const QString &deviceId, bool enabled);

    AlarmEngine *alarmEngine() const;
    bool acknowledgeAlarm(const QString &eventId, const QString &operatorId,
                          const QDateTime &at = QDateTime());
    void updateAlarmStates(const QDateTime &at = QDateTime());

    ConnectionState connectionState() const;
    CollectionState collectionState() const;
    bool isDeviceOnline(const QString &deviceId) const;
    bool isDeviceCollecting(const QString &deviceId) const;
    int onlineDeviceCount() const;

signals:
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
    void alarmStateChanged(const AlarmEvent &event, AlarmState previous,
                           AlarmState current);
    void errorOccurred(const QString &message);

private slots:
    void processTelemetry(const QList<TelemetrySample> &samples);
    void processHeartbeats(const QList<HeartbeatRecord> &heartbeats);
    void handleSourceConnectionState(ConnectionState state);
    void handleSourceError(const QString &message);

private:
    void initializeAlarmEngine(AlarmEngine *alarmEngine);
    void setConnectionState(ConnectionState state);
    void setCollectionState(CollectionState state);
    TelemetryRecord toTelemetryRecord(const TelemetrySample &sample) const;
    bool deviceHasActiveAlarm(const QString &deviceId) const;

    IDeviceDataSource *m_dataSource = nullptr;
    AlarmEngine *m_alarmEngine = nullptr;
    QTimer m_offlineCheckTimer;
    QList<DeviceInfo> m_devices;
    QHash<QString, DeviceInfo> m_deviceById;
    QHash<QString, bool> m_online;
    QHash<QString, bool> m_collecting;
    ConnectionState m_connectionState = ConnectionState::Disconnected;
    CollectionState m_collectionState = CollectionState::Stopped;
};
