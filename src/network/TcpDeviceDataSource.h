#pragma once

#include <network/TcpConnectionWorker.h>

#include <QObject>
#include <QString>
#include <atomic>

class QThread;

class TcpDeviceDataSource : public QObject
{
    Q_OBJECT

public:
    explicit TcpDeviceDataSource(QObject *parent = nullptr);
    ~TcpDeviceDataSource() override;

    TcpConnectionState state() const;
    QString host() const;
    quint16 port() const;
    bool isReconnectEnabled() const;
    int reconnectDelayMs() const;
    int reconnectMaxDelayMs() const;
    int connectTimeoutMs() const;
    int readTimeoutMs() const;

public slots:
    void connectToDevice(const QString &host, quint16 port);
    void disconnectFromDevice();
    void reconnectNow();
    void sendFrame(const Frame &frame);
    void setReconnectEnabled(bool enabled);
    void setReconnectDelayMs(int delayMs);
    void setReconnectMaxDelayMs(int delayMs);
    void setConnectTimeoutMs(int timeoutMs);
    void setReadTimeoutMs(int timeoutMs);

signals:
    void frameReceived(const Frame &frame);
    void stateChanged(TcpConnectionState state);
    void connected();
    void disconnected();
    void errorOccurred(const QString &message);
    void protocolErrorOccurred(const QString &message);
    void reconnectScheduled(int delayMs, int attempt);

private:
    QThread *m_thread = nullptr;
    TcpConnectionWorker *m_worker = nullptr;
    std::atomic<TcpConnectionState> m_state{TcpConnectionState::Disconnected};
    std::atomic_bool m_reconnectEnabled{true};
    std::atomic_int m_reconnectDelayMs{TcpConnectionWorker::kDefaultReconnectDelayMs};
    std::atomic_int m_reconnectMaxDelayMs{TcpConnectionWorker::kDefaultReconnectMaxDelayMs};
    std::atomic_int m_connectTimeoutMs{TcpConnectionWorker::kDefaultConnectTimeoutMs};
    std::atomic_int m_readTimeoutMs{TcpConnectionWorker::kDefaultReadTimeoutMs};
    QString m_host;
    quint16 m_port = 0;
};
