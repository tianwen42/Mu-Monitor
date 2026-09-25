#include "app/MonitoringService.h"

#include "network/IDeviceDataSource.h"

#include <QStringList>

namespace {
constexpr double kHighTemperatureThreshold = 80.0;
constexpr double kHighPressureThreshold = 1.8;

double measurementValue(const TelemetrySample &sample, MeasurementType type)
{
    const MeasurementValue *measurement = sample.measurement(type);
    return measurement ? measurement->value : 0.0;
}
}

MonitoringService::MonitoringService(IDeviceDataSource *dataSource, QObject *parent)
    : QObject(parent)
    , m_dataSource(dataSource)
{
    if (!m_dataSource) {
        return;
    }

    connect(m_dataSource, &IDeviceDataSource::telemetryGenerated,
            this, &MonitoringService::processTelemetry);
    connect(m_dataSource, &IDeviceDataSource::heartbeatGenerated,
            this, &MonitoringService::processHeartbeats);
    connect(m_dataSource, &IDeviceDataSource::connectionStateChanged,
            this, &MonitoringService::handleSourceConnectionState);
    connect(m_dataSource, &IDeviceDataSource::errorOccurred,
            this, &MonitoringService::handleSourceError);
}

void MonitoringService::setDevices(const QList<DeviceInfo> &devices)
{
    m_devices = devices;
    m_deviceById.clear();
    m_online.clear();
    m_collecting.clear();

    for (int i = 0; i < m_devices.size(); ++i) {
        const DeviceInfo &device = m_devices.at(i);
        m_deviceById.insert(device.deviceId, device);
        const bool online = ((i + 1) % 10) != 0;
        m_online.insert(device.deviceId, online);
        m_collecting.insert(device.deviceId, true);
        emit deviceStateChanged(device.deviceId, online, true);
    }

    if (m_dataSource) {
        m_dataSource->setDevices(m_devices);
    }
    emit onlineDeviceCountChanged(onlineDeviceCount());
}

QList<DeviceInfo> MonitoringService::devices() const
{
    return m_devices;
}

bool MonitoringService::start()
{
    if (!m_dataSource) {
        setCollectionState(CollectionState::Faulted);
        emit errorOccurred(QStringLiteral("数据源未配置"));
        return false;
    }
    if (m_dataSource->isRunning()) {
        setConnectionState(ConnectionState::Connected);
        setCollectionState(CollectionState::Running);
        return true;
    }

    setConnectionState(ConnectionState::Connecting);
    if (!m_dataSource->start()) {
        setConnectionState(ConnectionState::Disconnected);
        setCollectionState(CollectionState::Faulted);
        return false;
    }

    if (m_connectionState != ConnectionState::Connected) {
        setConnectionState(ConnectionState::Connected);
    }
    setCollectionState(CollectionState::Running);
    return true;
}

void MonitoringService::stop()
{
    if (!m_dataSource) {
        setConnectionState(ConnectionState::Disconnected);
        setCollectionState(CollectionState::Stopped);
        return;
    }

    setConnectionState(ConnectionState::Stopping);
    m_dataSource->stop();
    setConnectionState(ConnectionState::Disconnected);
    setCollectionState(CollectionState::Stopped);
}

bool MonitoringService::pauseCollection()
{
    if (!m_dataSource || m_collectionState != CollectionState::Running) {
        return false;
    }
    if (!m_dataSource->setCollectionEnabled(false)) {
        return false;
    }
    setCollectionState(CollectionState::Paused);
    return true;
}

bool MonitoringService::resumeCollection()
{
    if (!m_dataSource || m_collectionState != CollectionState::Paused) {
        return false;
    }
    if (!m_dataSource->setCollectionEnabled(true)) {
        return false;
    }
    setCollectionState(CollectionState::Running);
    return true;
}

bool MonitoringService::setDeviceCollection(const QString &deviceId, bool enabled)
{
    if (!m_collecting.contains(deviceId)) {
        return false;
    }
    if (m_dataSource && !m_dataSource->setDeviceCollectionEnabled(deviceId, enabled)) {
        return false;
    }

    m_collecting[deviceId] = enabled;
    emit deviceStateChanged(
        deviceId, m_online.value(deviceId, false), enabled);
    return true;
}

ConnectionState MonitoringService::connectionState() const
{
    return m_connectionState;
}

CollectionState MonitoringService::collectionState() const
{
    return m_collectionState;
}

bool MonitoringService::isDeviceOnline(const QString &deviceId) const
{
    return m_online.value(deviceId, false);
}

bool MonitoringService::isDeviceCollecting(const QString &deviceId) const
{
    return m_collecting.value(deviceId, false);
}

int MonitoringService::onlineDeviceCount() const
{
    int count = 0;
    for (auto it = m_online.constBegin(); it != m_online.constEnd(); ++it) {
        if (it.value()) {
            ++count;
        }
    }
    return count;
}

void MonitoringService::processTelemetry(const QList<TelemetrySample> &samples)
{
    QList<TelemetryRecord> records;
    records.reserve(samples.size());

    for (const TelemetrySample &sample : samples) {
        QString validationError;
        if (!sample.isValid(&validationError)) {
            emit errorOccurred(
                QStringLiteral("忽略无效遥测数据：%1").arg(validationError));
            continue;
        }

        bool isAlarm = false;
        QString alarmMessage;
        const TelemetryRecord record =
            toTelemetryRecord(sample, &isAlarm, &alarmMessage);
        records.append(record);
        if (isAlarm) {
            emit alarmRaised(record.deviceId, alarmMessage);
        }
    }

    if (!records.isEmpty()) {
        emit telemetryBatchReceived(records);
    }
}

void MonitoringService::processHeartbeats(const QList<HeartbeatRecord> &heartbeats)
{
    for (const HeartbeatRecord &heartbeat : heartbeats) {
        if (!heartbeat.deviceId.isEmpty()) {
            m_online[heartbeat.deviceId] = heartbeat.online;
            m_collecting[heartbeat.deviceId] = heartbeat.collecting;
            emit deviceStateChanged(
                heartbeat.deviceId, heartbeat.online, heartbeat.collecting);
        }
    }

    if (!heartbeats.isEmpty()) {
        emit heartbeatBatchReceived(heartbeats);
    }
    emit onlineDeviceCountChanged(onlineDeviceCount());
}

void MonitoringService::handleSourceConnectionState(ConnectionState state)
{
    setConnectionState(state);
}

void MonitoringService::handleSourceError(const QString &message)
{
    setCollectionState(CollectionState::Faulted);
    emit errorOccurred(message);
}

void MonitoringService::setConnectionState(ConnectionState state)
{
    if (m_connectionState == state) {
        return;
    }
    m_connectionState = state;
    emit connectionStateChanged(state);
}

void MonitoringService::setCollectionState(CollectionState state)
{
    if (m_collectionState == state) {
        return;
    }
    m_collectionState = state;
    emit collectionStateChanged(state);
}

TelemetryRecord MonitoringService::toTelemetryRecord(const TelemetrySample &sample,
                                                    bool *isAlarm,
                                                    QString *alarmMessage) const
{
    const bool online = m_online.value(sample.deviceId, true);
    const bool collecting = m_collecting.value(sample.deviceId, true);

    TelemetryRecord record;
    record.deviceId = sample.deviceId;
    record.name = m_deviceById.value(sample.deviceId).name;
    record.temperature = measurementValue(sample, MeasurementType::Temperature);
    record.pressure = measurementValue(sample, MeasurementType::Pressure);
    record.speed = measurementValue(sample, MeasurementType::Speed);
    record.voltage = measurementValue(sample, MeasurementType::Voltage);
    record.updatedAt = sample.collectedAt.toLocalTime();
    record.status = !online
        ? TelemetryStatus::Offline
        : (collecting ? TelemetryStatus::Online : TelemetryStatus::Stopped);

    QStringList alarms;
    if (online && collecting && record.temperature > kHighTemperatureThreshold) {
        alarms << QStringLiteral("温度超过阈值 %1 °C")
                      .arg(kHighTemperatureThreshold, 0, 'f', 1);
    }
    if (online && collecting && record.pressure > kHighPressureThreshold) {
        alarms << QStringLiteral("压力超过阈值 %1 MPa")
                      .arg(kHighPressureThreshold, 0, 'f', 2);
    }

    if (!alarms.isEmpty()) {
        record.status = TelemetryStatus::Alarm;
        if (alarmMessage) {
            *alarmMessage = alarms.join(QStringLiteral("；"));
        }
    }
    if (isAlarm) {
        *isAlarm = !alarms.isEmpty();
    }
    return record;
}