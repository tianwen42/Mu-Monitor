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

signals:
    void frameReceived(const Frame &frame);
    void stateChanged(TcpConnectionState state);
    void connected();
    void disconnected();
    void errorOccurred(const QString &message);
    void protocolErrorOccurred(const QString &message);

private slots:
    void handleConnected();
    void handleDisconnected();
    void handleSocketError();
    void handleSocketStateChanged();
    void handleReadyRead();
    void processDecoderEvents();

private:
    void setState(TcpConnectionState state);
    void scheduleReconnect();

    QTcpSocket *m_socket = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    class FrameDecoder *m_decoder = nullptr;
    QString m_host;
    quint16 m_port = 0;
    int m_reconnectDelayMs = 1000;
    bool m_reconnectEnabled = true;
    bool m_userDisconnected = true;
    TcpConnectionState m_state = TcpConnectionState::Disconnected;
};
