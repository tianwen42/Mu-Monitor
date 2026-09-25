#pragma once

#include "core/DeviceInfo.h"
#include "core/HeartbeatRecord.h"
#include "core/TelemetryRecord.h"
#include "core/TelemetrySample.h"

#include <QHash>
#include <QList>
#include <QObject>

class IDeviceDataSource;

class MonitoringService : public QObject
{
    Q_OBJECT

public:
    explicit MonitoringService(IDeviceDataSource *dataSource, QObject *parent = nullptr);

    void setDevices(const QList<DeviceInfo> &devices);
    QList<DeviceInfo> devices() const;

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

signals:
    void telemetryBatchReceived(const QList<TelemetryRecord> &records);
    void heartbeatBatchReceived(const QList<HeartbeatRecord> &heartbeats);
    void connectionStateChanged(ConnectionState state);
    void collectionStateChanged(CollectionState state);
    void deviceStateChanged(const QString &deviceId, bool online, bool collecting);
    void onlineDeviceCountChanged(int count);
    void alarmRaised(const QString &deviceId, const QString &message);
    void errorOccurred(const QString &message);

private slots:
    void processTelemetry(const QList<TelemetrySample> &samples);
    void processHeartbeats(const QList<HeartbeatRecord> &heartbeats);
    void handleSourceConnectionState(ConnectionState state);
    void handleSourceError(const QString &message);

private:
    void setConnectionState(ConnectionState state);
    void setCollectionState(CollectionState state);
    TelemetryRecord toTelemetryRecord(const TelemetrySample &sample, bool *isAlarm,
                                      QString *alarmMessage) const;

    IDeviceDataSource *m_dataSource = nullptr;
    QList<DeviceInfo> m_devices;
    QHash<QString, DeviceInfo> m_deviceById;
    QHash<QString, bool> m_online;
    QHash<QString, bool> m_collecting;
    ConnectionState m_connectionState = ConnectionState::Disconnected;
    CollectionState m_collectionState = CollectionState::Stopped;
};