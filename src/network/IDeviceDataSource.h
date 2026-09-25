#pragma once

#include "core/BusinessStates.h"
#include "core/DeviceInfo.h"
#include "core/HeartbeatRecord.h"
#include "core/TelemetrySample.h"

#include <QList>
#include <QObject>

class IDeviceDataSource : public QObject
{
    Q_OBJECT

public:
    explicit IDeviceDataSource(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~IDeviceDataSource() override = default;

    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

    virtual void setDevices(const QList<DeviceInfo> &devices) = 0;
    virtual QList<DeviceInfo> devices() const = 0;

    virtual bool setCollectionEnabled(bool enabled) = 0;
    virtual bool setDeviceCollectionEnabled(const QString &deviceId, bool enabled) = 0;
    virtual bool discoversDevicesDynamically() const { return false; }

signals:
    void deviceDiscovered(const DeviceInfo &device);
    void telemetryGenerated(const QList<TelemetrySample> &samples);
    void heartbeatGenerated(const QList<HeartbeatRecord> &heartbeats);
    void connectionStateChanged(ConnectionState state);
    void errorOccurred(const QString &message);
};