#include "network/TcpDeviceDataSourceAdapter.h"

#include "network/ProtocolPayloadMapper.h"
#include "network/TcpDeviceDataSource.h"
#include "protocol/Protocol.h"

#include <QString>

TcpDeviceDataSourceAdapter::TcpDeviceDataSourceAdapter(QObject *parent)
    : IDeviceDataSource(parent)
    , m_tcpSource(new TcpDeviceDataSource(this))
{
    connect(m_tcpSource, &TcpDeviceDataSource::frameReceived,
            this, &TcpDeviceDataSourceAdapter::handleFrame);
    connect(m_tcpSource, &TcpDeviceDataSource::stateChanged,
            this, &TcpDeviceDataSourceAdapter::handleTcpStateChanged);
    connect(m_tcpSource, &TcpDeviceDataSource::errorOccurred, this,
            [this](const QString &message) {
                emit errorOccurred(message);
            });
    connect(m_tcpSource, &TcpDeviceDataSource::protocolErrorOccurred, this,
            [this](const QString &message) {
                emit errorOccurred(message);
            });
}

TcpDeviceDataSourceAdapter::~TcpDeviceDataSourceAdapter() = default;

bool TcpDeviceDataSourceAdapter::start()
{
    if (m_running) {
        return true;
    }
    if (m_host.trimmed().isEmpty() || m_port == 0) {
        emit errorOccurred(QStringLiteral("TCP 目标地址或端口无效"));
        return false;
    }

    m_running = true;
    setConnectionState(ConnectionState::Connecting);
    m_tcpSource->connectToDevice(m_host.trimmed(), m_port);
    return true;
}

void TcpDeviceDataSourceAdapter::stop()
{
    if (!m_running) {
        return;
    }

    m_running = false;
    setConnectionState(ConnectionState::Stopping);
    m_tcpSource->disconnectFromDevice();
    setConnectionState(ConnectionState::Disconnected);
}

bool TcpDeviceDataSourceAdapter::isRunning() const
{
    return m_running;
}

void TcpDeviceDataSourceAdapter::setDevices(const QList<DeviceInfo> &devices)
{
    m_devices = devices;
    m_deviceIds.clear();
    m_collecting.clear();
    m_dynamicDiscovery = devices.isEmpty();

    for (const DeviceInfo &device : m_devices) {
        const QString deviceId = device.deviceId.trimmed();
        if (deviceId.isEmpty()) {
            continue;
        }
        m_deviceIds.insert(deviceId);
        m_collecting.insert(deviceId, true);
    }
}

QList<DeviceInfo> TcpDeviceDataSourceAdapter::devices() const
{
    return m_devices;
}

bool TcpDeviceDataSourceAdapter::setCollectionEnabled(bool enabled)
{
    m_collectionEnabled = enabled;
    return true;
}

bool TcpDeviceDataSourceAdapter::setDeviceCollectionEnabled(
    const QString &deviceId, bool enabled)
{
    const QString normalizedId = deviceId.trimmed();
    if (!m_collecting.contains(normalizedId)) {
        return false;
    }
    m_collecting[normalizedId] = enabled;
    return true;
}

bool TcpDeviceDataSourceAdapter::discoversDevicesDynamically() const
{
    return m_dynamicDiscovery;
}

void TcpDeviceDataSourceAdapter::setEndpoint(const QString &host, quint16 port)
{
    m_host = host.trimmed();
    m_port = port;
}

QString TcpDeviceDataSourceAdapter::host() const
{
    return m_host;
}

quint16 TcpDeviceDataSourceAdapter::port() const
{
    return m_port;
}

ConnectionState TcpDeviceDataSourceAdapter::connectionState() const
{
    return m_connectionState;
}

TcpDeviceDataSource *TcpDeviceDataSourceAdapter::tcpDeviceDataSource() const
{
    return m_tcpSource;
}

void TcpDeviceDataSourceAdapter::handleFrame(const Frame &frame)
{
    if (!m_running || !m_collectionEnabled) {
        return;
    }

    const QString deviceId = frame.deviceId.trimmed();
    if (deviceId.isEmpty()) {
        return;
    }

    if (!m_deviceIds.contains(deviceId)) {
        if (!m_dynamicDiscovery) {
            return;
        }

        DeviceInfo discovered;
        discovered.deviceId = deviceId;
        discovered.name = QStringLiteral("TCP 设备 %1").arg(deviceId);
        discovered.protocol = QStringLiteral("TCP");
        m_devices.append(discovered);
        m_deviceIds.insert(deviceId);
        m_collecting.insert(deviceId, true);
        emit deviceDiscovered(discovered);
    }

    if (!m_collecting.value(deviceId, false)) {
        return;
    }

    QString errorMessage;
    switch (frame.messageType) {
    case Protocol::MessageType::Telemetry: {
        TelemetrySample sample;
        if (!ProtocolPayloadMapper::toTelemetrySample(
                deviceId, frame.timestampUtcMs, frame.payload,
                &sample, &errorMessage)) {
            reportFrameError(frame, errorMessage);
            return;
        }
        QList<TelemetrySample> samples;
        samples.append(sample);
        emit telemetryGenerated(samples);
        return;
    }
    case Protocol::MessageType::Heartbeat: {
        HeartbeatRecord heartbeat;
        if (!ProtocolPayloadMapper::toHeartbeatRecord(
                deviceId, frame.timestampUtcMs, frame.payload,
                &heartbeat, &errorMessage)) {
            reportFrameError(frame, errorMessage);
            return;
        }
        QList<HeartbeatRecord> heartbeats;
        heartbeats.append(heartbeat);
        emit heartbeatGenerated(heartbeats);
        return;
    }
    case Protocol::MessageType::Command:
    case Protocol::MessageType::Response:
    case Protocol::MessageType::Error:
        return;
    }
}

void TcpDeviceDataSourceAdapter::handleTcpStateChanged(TcpConnectionState state)
{
    if (!m_running && state != TcpConnectionState::Disconnected) {
        return;
    }
    setConnectionState(mapConnectionState(state));
}

ConnectionState TcpDeviceDataSourceAdapter::mapConnectionState(
    TcpConnectionState state)
{
    switch (state) {
    case TcpConnectionState::Disconnected:
        return ConnectionState::Disconnected;
    case TcpConnectionState::Connecting:
        return ConnectionState::Connecting;
    case TcpConnectionState::Connected:
        return ConnectionState::Connected;
    case TcpConnectionState::Reconnecting:
        return ConnectionState::Reconnecting;
    }
    return ConnectionState::Disconnected;
}

void TcpDeviceDataSourceAdapter::setConnectionState(ConnectionState state)
{
    if (m_connectionState == state) {
        return;
    }
    m_connectionState = state;
    emit connectionStateChanged(state);
}

void TcpDeviceDataSourceAdapter::reportFrameError(
    const Frame &frame, const QString &message)
{
    emit errorOccurred(
        QStringLiteral("TCP %1 帧处理失败（设备 %2）：%3")
            .arg(Protocol::messageTypeName(frame.messageType),
                 frame.deviceId.trimmed(), message));
}