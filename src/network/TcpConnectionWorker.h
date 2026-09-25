#pragma once

#include <protocol/Frame.h>

#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

enum class TcpConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
};

Q_DECLARE_METATYPE(TcpConnectionState)

class TcpConnectionWorker : public QObject
{
    Q_OBJECT

public:
    static constexpr int kDefaultReconnectDelayMs = 1000;
    static constexpr int kDefaultReconnectMaxDelayMs = 30000;
    static constexpr int kDefaultConnectTimeoutMs = 5000;
    static constexpr int kDefaultReadTimeoutMs = 15000;

    explicit TcpConnectionWorker(QObject *parent = nullptr);
    ~TcpConnectionWorker() override;

public slots:
    void connectToHost(const QString &host, quint16 port);
    void disconnectFromHost();
    void reconnectNow();
    void shutdown();
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

private slots:
    void handleConnected();
    void handleDisconnected();
    void handleSocketError();
    void handleSocketStateChanged();
    void handleReadyRead();
    void processDecoderEvents();
    void handleConnectTimeout();
    void handleReadTimeout();
    void handleReconnectTimer();

private:
    void setState(TcpConnectionState state);
    void scheduleReconnect();
    void startConnection();

    QTcpSocket *m_socket = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QTimer *m_connectTimer = nullptr;
    QTimer *m_readTimer = nullptr;
    class FrameDecoder *m_decoder = nullptr;
    QString m_host;
    quint16 m_port = 0;
    int m_reconnectDelayMs = kDefaultReconnectDelayMs;
    int m_reconnectMaxDelayMs = kDefaultReconnectMaxDelayMs;
    int m_connectTimeoutMs = kDefaultConnectTimeoutMs;
    int m_readTimeoutMs = kDefaultReadTimeoutMs;
    int m_reconnectAttempt = 0;
    bool m_reconnectEnabled = true;
    bool m_userDisconnected = true;
    TcpConnectionState m_state = TcpConnectionState::Disconnected;
};
