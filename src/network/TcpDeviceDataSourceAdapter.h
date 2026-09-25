#pragma once

#include "network/IDeviceDataSource.h"
#include "network/TcpConnectionWorker.h"
#include "protocol/Frame.h"

#include <QHash>
#include <QSet>

class TcpDeviceDataSource;

class TcpDeviceDataSourceAdapter final : public IDeviceDataSource
{
    Q_OBJECT

public:
    static constexpr quint16 kDefaultPort = 45454;

    explicit TcpDeviceDataSourceAdapter(QObject *parent = nullptr);
    ~TcpDeviceDataSourceAdapter() override;

    bool start() override;
    void stop() override;
    bool isRunning() const override;

    void setDevices(const QList<DeviceInfo> &devices) override;
    QList<DeviceInfo> devices() const override;

    bool setCollectionEnabled(bool enabled) override;
    bool setDeviceCollectionEnabled(const QString &deviceId, bool enabled) override;

    void setEndpoint(const QString &host, quint16 port);
    QString host() const;
    quint16 port() const;

    ConnectionState connectionState() const;
    TcpDeviceDataSource *tcpDeviceDataSource() const;

private slots:
    void handleFrame(const Frame &frame);
    void handleTcpStateChanged(TcpConnectionState state);

private:
    static ConnectionState mapConnectionState(TcpConnectionState state);
    void setConnectionState(ConnectionState state);
    void reportFrameError(const Frame &frame, const QString &message);

    TcpDeviceDataSource *m_tcpSource = nullptr;
    QString m_host = QStringLiteral("127.0.0.1");
    quint16 m_port = kDefaultPort;

    QList<DeviceInfo> m_devices;
    QSet<QString> m_deviceIds;
    QHash<QString, bool> m_collecting;

    ConnectionState m_connectionState = ConnectionState::Disconnected;
    bool m_running = false;
    bool m_collectionEnabled = true;
};