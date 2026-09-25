#pragma once

#include "network/IDeviceDataSource.h"

#include <QHash>
#include <QTimer>

#include <random>

class SimulationDataSource final : public IDeviceDataSource
{
    Q_OBJECT

public:
    explicit SimulationDataSource(QObject *parent = nullptr);
    ~SimulationDataSource() override;

    bool start() override;
    void stop() override;
    bool isRunning() const override;

    void setDevices(const QList<DeviceInfo> &devices) override;
    QList<DeviceInfo> devices() const override;

    bool setCollectionEnabled(bool enabled) override;
    bool setDeviceCollectionEnabled(const QString &deviceId, bool enabled) override;

    void setSeed(quint32 seed);
    quint32 seed() const;

    void setSamplingInterval(int milliseconds);
    int samplingInterval() const;
    void setHeartbeatInterval(int milliseconds);
    int heartbeatInterval() const;

    void setDeviceOnline(const QString &deviceId, bool online);
    bool isDeviceOnline(const QString &deviceId) const;

    void triggerTelemetry();
    void triggerHeartbeat();

private slots:
    void generateTelemetry();
    void generateHeartbeat();

private:
    QList<DeviceInfo> m_devices;
    QHash<QString, bool> m_online;
    QHash<QString, bool> m_collecting;
    QTimer m_telemetryTimer;
    QTimer m_heartbeatTimer;
    std::mt19937 m_random;
    quint32 m_seed = 0;
    quint64 m_tick = 0;
    bool m_running = false;
    bool m_collectionEnabled = true;
};
