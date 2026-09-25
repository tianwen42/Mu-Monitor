#include "network/SimulationDataSource.h"

#include <QDateTime>

#include <algorithm>
#include <utility>

namespace {
constexpr int kDefaultSamplingIntervalMs = 1000;
constexpr int kDefaultHeartbeatIntervalMs = 3000;
constexpr double kDefaultTemperatureBase = 58.0;
constexpr double kDefaultPressureBase = 1.05;
constexpr double kDefaultSpeedBase = 1200.0;
constexpr double kDefaultVoltageBase = 220.0;

double randomSignedUnit(std::mt19937 &random)
{
    std::uniform_real_distribution<double> distribution(-0.5, 0.5);
    return distribution(random);
}
}

SimulationDataSource::SimulationDataSource(QObject *parent)
    : IDeviceDataSource(parent)
    , m_random(std::random_device{}())
{
    m_telemetryTimer.setInterval(kDefaultSamplingIntervalMs);
    m_heartbeatTimer.setInterval(kDefaultHeartbeatIntervalMs);
    m_telemetryTimer.setSingleShot(false);
    m_heartbeatTimer.setSingleShot(false);

    connect(&m_telemetryTimer, &QTimer::timeout,
            this, &SimulationDataSource::generateTelemetry);
    connect(&m_heartbeatTimer, &QTimer::timeout,
            this, &SimulationDataSource::generateHeartbeat);
}

SimulationDataSource::~SimulationDataSource()
{
    m_running = false;
    m_telemetryTimer.stop();
    m_heartbeatTimer.stop();
}
bool SimulationDataSource::start()
{
    if (m_running) {
        return true;
    }
    if (m_devices.isEmpty()) {
        emit errorOccurred(QStringLiteral("没有可用的模拟设备"));
        return false;
    }

    m_running = true;
    m_tick = 0;
    emit connectionStateChanged(ConnectionState::Connecting);
    emit connectionStateChanged(ConnectionState::Connected);

    if (m_collectionEnabled) {
        m_telemetryTimer.start();
    }
    m_heartbeatTimer.start();
    triggerHeartbeat();
    return true;
}

void SimulationDataSource::stop()
{
    if (!m_running) {
        return;
    }

    m_running = false;
    m_telemetryTimer.stop();
    m_heartbeatTimer.stop();
    emit connectionStateChanged(ConnectionState::Stopping);
    emit connectionStateChanged(ConnectionState::Disconnected);
}

bool SimulationDataSource::isRunning() const
{
    return m_running;
}

void SimulationDataSource::setDevices(const QList<DeviceInfo> &devices)
{
    const bool wasRunning = m_running;
    if (wasRunning) {
        stop();
    }

    m_devices = devices;
    m_online.clear();
    m_collecting.clear();
    for (int i = 0; i < m_devices.size(); ++i) {
        const QString deviceId = m_devices.at(i).deviceId;
        m_online.insert(deviceId, ((i + 1) % 10) != 0);
        m_collecting.insert(deviceId, true);
    }
    m_tick = 0;

    if (wasRunning) {
        start();
    }
}

QList<DeviceInfo> SimulationDataSource::devices() const
{
    return m_devices;
}

bool SimulationDataSource::setCollectionEnabled(bool enabled)
{
    m_collectionEnabled = enabled;
    if (!m_running) {
        return true;
    }

    if (enabled) {
        m_telemetryTimer.start();
    } else {
        m_telemetryTimer.stop();
    }
    return true;
}

bool SimulationDataSource::setDeviceCollectionEnabled(const QString &deviceId, bool enabled)
{
    if (!m_collecting.contains(deviceId)) {
        return false;
    }
    m_collecting[deviceId] = enabled;
    return true;
}

void SimulationDataSource::setSeed(quint32 seed)
{
    m_seed = seed;
    m_random.seed(seed);
}

quint32 SimulationDataSource::seed() const
{
    return m_seed;
}

void SimulationDataSource::setSamplingInterval(int milliseconds)
{
    m_telemetryTimer.setInterval(std::max(1, milliseconds));
}

int SimulationDataSource::samplingInterval() const
{
    return m_telemetryTimer.interval();
}

void SimulationDataSource::setHeartbeatInterval(int milliseconds)
{
    m_heartbeatTimer.setInterval(std::max(1, milliseconds));
}

int SimulationDataSource::heartbeatInterval() const
{
    return m_heartbeatTimer.interval();
}

void SimulationDataSource::setDeviceOnline(const QString &deviceId, bool online)
{
    if (m_online.contains(deviceId)) {
        m_online[deviceId] = online;
    }
}

bool SimulationDataSource::isDeviceOnline(const QString &deviceId) const
{
    return m_online.value(deviceId, false);
}

void SimulationDataSource::triggerTelemetry()
{
    if (m_devices.isEmpty()) {
        return;
    }

    ++m_tick;
    QList<TelemetrySample> samples;
    samples.reserve(m_devices.size());

    for (int i = 0; i < m_devices.size(); ++i) {
        const DeviceInfo &device = m_devices.at(i);
        const bool online = m_online.value(device.deviceId, false);
        const bool collecting = m_collecting.value(device.deviceId, false);
        if (!online || !collecting) {
            continue;
        }

        const double jitterTemperature = randomSignedUnit(m_random) * 7.0;
        const double jitterPressure = randomSignedUnit(m_random) * 0.16;
        const double jitterSpeed = randomSignedUnit(m_random) * 120.0;
        const double jitterVoltage = randomSignedUnit(m_random) * 5.0;

        double temperature = kDefaultTemperatureBase + i * 5.0 + jitterTemperature;
        double pressure = kDefaultPressureBase + i * 0.10 + jitterPressure;
        const double speed = kDefaultSpeedBase + i * 170.0 + jitterSpeed;
        const double voltage = kDefaultVoltageBase + jitterVoltage;

        if (m_tick % 13 == 0 && i == 1) {
            pressure = 2.08;
        }
        if (m_tick % 17 == 0 && i == 2) {
            temperature = 86.5;
        }

        TelemetrySample sample;
        sample.deviceId = device.deviceId;
        sample.collectedAt = QDateTime::currentDateTimeUtc();
        sample.measurements = {
            {MeasurementType::Temperature, temperature, QualityCode::Good},
            {MeasurementType::Pressure, pressure, QualityCode::Good},
            {MeasurementType::Speed, speed, QualityCode::Good},
            {MeasurementType::Voltage, voltage, QualityCode::Good},
        };
        samples.append(sample);
    }

    if (!samples.isEmpty()) {
        emit telemetryGenerated(samples);
    }
}

void SimulationDataSource::triggerHeartbeat()
{
    QList<HeartbeatRecord> heartbeats;
    heartbeats.reserve(m_devices.size());

    std::uniform_int_distribution<int> latencyDistribution(20, 89);
    for (const DeviceInfo &device : std::as_const(m_devices)) {
        const bool online = m_online.value(device.deviceId, false);
        const bool collecting = m_collecting.value(device.deviceId, false);
        HeartbeatRecord heartbeat;
        heartbeat.deviceId = device.deviceId;
        heartbeat.heartbeatAt = QDateTime::currentDateTimeUtc();
        heartbeat.online = online;
        heartbeat.collecting = collecting;
        heartbeat.latencyMs = online ? latencyDistribution(m_random) : -1;
        heartbeats.append(heartbeat);
    }

    if (!heartbeats.isEmpty()) {
        emit heartbeatGenerated(heartbeats);
    }
}

void SimulationDataSource::generateTelemetry()
{
    if (m_running && m_collectionEnabled) {
        triggerTelemetry();
    }
}

void SimulationDataSource::generateHeartbeat()
{
    if (m_running) {
        triggerHeartbeat();
    }
}