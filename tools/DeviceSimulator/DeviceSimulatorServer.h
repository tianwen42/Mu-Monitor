#pragma once

#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QTcpServer>
#include <QTimer>

class QTcpSocket;

class DeviceSimulatorServer : public QObject
{
    Q_OBJECT

public:
    enum class Scenario {
        Normal,
        HighTemperature,
        HighPressure,
        Offline,
        BadCrc,
        ActiveDisconnect,
    };

    enum class WireFormat {
        JsonLines,
        ProtocolV1,
    };

    struct Device {
        QString id;
        QString name;
        Scenario scenario = Scenario::Normal;
        double temperature = 0.0;
        double pressure = 0.0;
        double speed = 0.0;
        double voltage = 0.0;
    };

    explicit DeviceSimulatorServer(QObject *parent = nullptr);

    bool start(const QHostAddress &address, quint16 port, QString *errorMessage = nullptr);
    void stop();
    bool isRunning() const;
    quint16 serverPort() const;
    int clientCount() const;
    QList<Device> devices() const;
    bool setScenario(const QString &deviceId, Scenario scenario);
    QString scenarioText(const QString &deviceId) const;
    WireFormat wireFormat() const;
    void setWireFormat(WireFormat format);
    void setSendIntervalMs(int intervalMs);

signals:
    void runningChanged(bool running);
    void clientCountChanged(int count);
    void devicesChanged();
    void logMessage(const QString &message);

private slots:
    void handleNewConnection();
    void handleClientDisconnected();
    void sendTelemetry();
    void disconnectAllClients();

private:
    Device *findDevice(const QString &deviceId);
    const Device *findDevice(const QString &deviceId) const;
    QByteArray createJsonPayload(const Device &device) const;
    QString scenarioName(Scenario scenario) const;

    QTcpServer m_server;
    QTimer m_timer;
    QList<Device> m_devices;
    QList<QTcpSocket *> m_clients;
    WireFormat m_wireFormat = WireFormat::ProtocolV1;
    quint32 m_sequence = 0;
};
