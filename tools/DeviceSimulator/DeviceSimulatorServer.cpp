#include "DeviceSimulatorServer.h"

#include "FrameEncoder.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTcpSocket>

#include <cmath>

DeviceSimulatorServer::DeviceSimulatorServer(QObject *parent)
    : QObject(parent)
{
    for (int i = 1; i <= 4; ++i) {
        Device device;
        device.id = QStringLiteral("DEV-%1").arg(i, 3, 10, QLatin1Char('0'));
        device.name = QStringLiteral("模拟设备 %1").arg(i, 3, 10, QLatin1Char('0'));
        device.temperature = 58.0 + i * 5.0;
        device.pressure = 1.05 + i * 0.10;
        device.speed = 1200.0 + i * 170.0;
        device.voltage = 220.0;
        m_devices.append(device);
    }

    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &DeviceSimulatorServer::sendTelemetry);
    connect(&m_server, &QTcpServer::newConnection,
            this, &DeviceSimulatorServer::handleNewConnection);
}

bool DeviceSimulatorServer::start(const QHostAddress &address, quint16 port,
                                  QString *errorMessage)
{
    if (m_server.isListening()) {
        stop();
    }

    if (!m_server.listen(address, port)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法监听 %1:%2：%3")
                                .arg(address.toString())
                                .arg(port)
                                .arg(m_server.errorString());
        }
        return false;
    }

    m_timer.start();
    emit runningChanged(true);
    emit logMessage(QStringLiteral("[服务端] 已监听 %1:%2")
                        .arg(m_server.serverAddress().toString())
                        .arg(m_server.serverPort()));
    return true;
}

void DeviceSimulatorServer::stop()
{
    const bool wasRunning = m_server.isListening() || m_timer.isActive();
    m_timer.stop();
    m_server.close();

    const QList<QTcpSocket *> clients = m_clients;
    m_clients.clear();
    for (QTcpSocket *client : clients) {
        client->disconnect(this);
        client->disconnectFromHost();
        client->deleteLater();
    }

    if (wasRunning) {
        emit logMessage(QStringLiteral("[服务端] 已停止监听"));
    }
    emit clientCountChanged(0);
    emit runningChanged(false);
}

bool DeviceSimulatorServer::isRunning() const
{
    return m_server.isListening();
}

quint16 DeviceSimulatorServer::serverPort() const
{
    return m_server.serverPort();
}

int DeviceSimulatorServer::clientCount() const
{
    return m_clients.size();
}

QList<DeviceSimulatorServer::Device> DeviceSimulatorServer::devices() const
{
    return m_devices;
}

bool DeviceSimulatorServer::setScenario(const QString &deviceId, Scenario scenario)
{
    Device *device = findDevice(deviceId);
    if (!device) {
        return false;
    }

    device->scenario = scenario;
    emit devicesChanged();
    emit logMessage(QStringLiteral("[场景] %1 已切换为 %2")
                        .arg(device->id, scenarioName(scenario)));
    return true;
}

QString DeviceSimulatorServer::scenarioText(const QString &deviceId) const
{
    const Device *device = findDevice(deviceId);
    return device ? scenarioName(device->scenario) : QString();
}

DeviceSimulatorServer::WireFormat DeviceSimulatorServer::wireFormat() const
{
    return m_wireFormat;
}

void DeviceSimulatorServer::setWireFormat(WireFormat format)
{
    if (m_wireFormat == format) {
        return;
    }

    m_wireFormat = format;
    emit logMessage(m_wireFormat == WireFormat::ProtocolV1
                        ? QStringLiteral("[协议] 已切换为 Protocol v1 二进制帧")
                        : QStringLiteral("[协议] 已切换为 JSON Lines 测试模式"));
}

void DeviceSimulatorServer::setSendIntervalMs(int intervalMs)
{
    m_timer.setInterval(qMax(10, intervalMs));
}

void DeviceSimulatorServer::handleNewConnection()
{
    while (QTcpSocket *client = m_server.nextPendingConnection()) {
        m_clients.append(client);
        connect(client, &QTcpSocket::disconnected,
                this, &DeviceSimulatorServer::handleClientDisconnected);
        emit logMessage(QStringLiteral("[连接] 客户端已连接：%1:%2")
                            .arg(client->peerAddress().toString())
                            .arg(client->peerPort()));
    }

    emit clientCountChanged(m_clients.size());
}

void DeviceSimulatorServer::handleClientDisconnected()
{
    auto *client = qobject_cast<QTcpSocket *>(sender());
    if (!client) {
        return;
    }

    m_clients.removeAll(client);
    emit logMessage(QStringLiteral("[连接] 客户端已断开：%1:%2")
                        .arg(client->peerAddress().toString())
                        .arg(client->peerPort()));
    client->deleteLater();
    emit clientCountChanged(m_clients.size());
}

void DeviceSimulatorServer::sendTelemetry()
{
    auto jitter = [](double amplitude) {
        return (QRandomGenerator::global()->generateDouble() - 0.5) * amplitude;
    };

    bool disconnectRequested = false;
    for (Device &device : m_devices) {
        if (device.scenario == Scenario::ActiveDisconnect) {
            disconnectRequested = true;
            continue;
        }

        QString alarm;
        if (device.scenario == Scenario::HighTemperature) {
            device.temperature = 86.5 + jitter(1.0);
            alarm = QStringLiteral("温度超过阈值 80 °C");
        } else if (device.scenario == Scenario::HighPressure) {
            device.pressure = 2.08 + jitter(0.04);
            alarm = QStringLiteral("压力超过阈值 1.80 MPa");
        } else if (device.scenario == Scenario::Offline) {
            device.temperature = 0.0;
            device.pressure = 0.0;
            device.speed = 0.0;
            device.voltage = 0.0;
        } else {
            const int index = device.id.mid(4).toInt();
            device.temperature = 58.0 + index * 5.0 + jitter(7.0);
            device.pressure = 1.05 + index * 0.10 + jitter(0.16);
            device.speed = 1200.0 + index * 170.0 + jitter(120.0);
            device.voltage = 220.0 + jitter(5.0);
        }

        QByteArray payload = createJsonPayload(device);
        if (m_wireFormat == WireFormat::ProtocolV1) {
            QString errorMessage;
            if (device.scenario == Scenario::BadCrc) {
                payload = FrameEncoder::encodeTelemetryWithBadCrc(
                    device.id, m_sequence++, payload, &errorMessage);
            } else {
                payload = FrameEncoder::encodeTelemetry(
                    device.id, m_sequence++, payload, &errorMessage);
            }
            if (payload.isEmpty()) {
                emit logMessage(QStringLiteral("[编码] %1 帧编码失败：%2")
                                    .arg(device.id, errorMessage));
                continue;
            }
        } else {
            payload.append('\n');
        }

        for (QTcpSocket *client : m_clients) {
            if (client->state() == QAbstractSocket::ConnectedState) {
                client->write(payload);
            }
        }

        if (!m_clients.isEmpty()) {
            emit logMessage(QStringLiteral("[发送] %1 %2，温度=%3 °C，压力=%4 MPa%5")
                                .arg(device.id,
                                     scenarioName(device.scenario))
                                .arg(device.temperature, 0, 'f', 1)
                                .arg(device.pressure, 0, 'f', 2)
                                .arg(alarm.isEmpty()
                                         ? QString()
                                         : QStringLiteral("，%1").arg(alarm)));
        }
    }

    if (disconnectRequested) {
        disconnectAllClients();
    }

    emit devicesChanged();
}

void DeviceSimulatorServer::disconnectAllClients()
{
    if (m_clients.isEmpty()) {
        return;
    }

    emit logMessage(QStringLiteral("[场景] 主动断开全部客户端"));
    const QList<QTcpSocket *> clients = m_clients;
    for (QTcpSocket *client : clients) {
        if (client->state() != QAbstractSocket::UnconnectedState) {
            client->disconnectFromHost();
        }
    }
}

DeviceSimulatorServer::Device *DeviceSimulatorServer::findDevice(const QString &deviceId)
{
    for (Device &device : m_devices) {
        if (device.id == deviceId) {
            return &device;
        }
    }
    return nullptr;
}

const DeviceSimulatorServer::Device *DeviceSimulatorServer::findDevice(
    const QString &deviceId) const
{
    for (const Device &device : m_devices) {
        if (device.id == deviceId) {
            return &device;
        }
    }
    return nullptr;
}

QByteArray DeviceSimulatorServer::createJsonPayload(const Device &device) const
{
    const bool online = device.scenario != Scenario::Offline;
    QString status = QStringLiteral("normal");
    QString alarm;

    if (!online) {
        status = QStringLiteral("offline");
    } else if (device.scenario == Scenario::HighTemperature) {
        status = QStringLiteral("alarm");
        alarm = QStringLiteral("temperature_high");
    } else if (device.scenario == Scenario::HighPressure) {
        status = QStringLiteral("alarm");
        alarm = QStringLiteral("pressure_high");
    }

    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("telemetry"));
    object.insert(QStringLiteral("version"), 1);
    object.insert(QStringLiteral("timestamp"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("deviceId"), device.id);
    object.insert(QStringLiteral("name"), device.name);
    object.insert(QStringLiteral("online"), online);
    object.insert(QStringLiteral("collecting"), online);
    object.insert(QStringLiteral("status"), status);
    object.insert(QStringLiteral("alarm"), alarm);
    object.insert(QStringLiteral("temperature"),
                  std::round(device.temperature * 10.0) / 10.0);
    object.insert(QStringLiteral("pressure"),
                  std::round(device.pressure * 100.0) / 100.0);
    object.insert(QStringLiteral("speed"),
                  std::round(device.speed * 10.0) / 10.0);
    object.insert(QStringLiteral("voltage"),
                  std::round(device.voltage * 10.0) / 10.0);

    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString DeviceSimulatorServer::scenarioName(Scenario scenario) const
{
    switch (scenario) {
    case Scenario::HighTemperature:
        return QStringLiteral("高温");
    case Scenario::HighPressure:
        return QStringLiteral("高压");
    case Scenario::Offline:
        return QStringLiteral("离线");
    case Scenario::BadCrc:
        return QStringLiteral("坏 CRC");
    case Scenario::ActiveDisconnect:
        return QStringLiteral("主动断开");
    case Scenario::Normal:
    default:
        return QStringLiteral("正常");
    }
}
