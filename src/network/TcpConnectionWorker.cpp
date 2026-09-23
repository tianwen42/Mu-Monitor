#include "TcpConnectionWorker.h"

#include "FrameDecoder.h"

#include <protocol/FrameCodec.h>

#include <QAbstractSocket>
#include <QTcpSocket>
#include <QTimer>

namespace {

QString socketStateText(QAbstractSocket::SocketState state)
{
    switch (state) {
    case QAbstractSocket::UnconnectedState:
        return QStringLiteral("未连接");
    case QAbstractSocket::HostLookupState:
        return QStringLiteral("解析主机");
    case QAbstractSocket::ConnectingState:
        return QStringLiteral("连接中");
    case QAbstractSocket::ConnectedState:
        return QStringLiteral("已连接");
    case QAbstractSocket::BoundState:
        return QStringLiteral("已绑定");
    case QAbstractSocket::ListeningState:
        return QStringLiteral("监听中");
    case QAbstractSocket::ClosingState:
        return QStringLiteral("关闭中");
    }
    return QStringLiteral("未知");
}

} // namespace

TcpConnectionWorker::TcpConnectionWorker(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_reconnectTimer(new QTimer(this))
    , m_decoder(new FrameDecoder)
{
    qRegisterMetaType<Frame>("Frame");
    qRegisterMetaType<TcpConnectionState>("TcpConnectionState");

    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &TcpConnectionWorker::reconnectNow);
    connect(m_socket, &QTcpSocket::connected, this, &TcpConnectionWorker::handleConnected);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &TcpConnectionWorker::handleDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred,
            this, &TcpConnectionWorker::handleSocketError);
    connect(m_socket, &QTcpSocket::stateChanged,
            this, &TcpConnectionWorker::handleSocketStateChanged);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &TcpConnectionWorker::handleReadyRead);
}

TcpConnectionWorker::~TcpConnectionWorker()
{
    delete m_decoder;
    m_decoder = nullptr;
}

void TcpConnectionWorker::connectToHost(const QString &host, quint16 port)
{
    if (host.trimmed().isEmpty() || port == 0) {
        emit errorOccurred(QStringLiteral("TCP 目标地址或端口无效"));
        return;
    }

    m_host = host.trimmed();
    m_port = port;
    m_reconnectTimer->stop();

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_userDisconnected = true;
        m_socket->abort();
    }

    m_userDisconnected = false;
    m_decoder->clear();
    setState(TcpConnectionState::Connecting);
    m_socket->connectToHost(m_host, m_port);
}

void TcpConnectionWorker::disconnectFromHost()
{
    m_userDisconnected = true;
    m_reconnectTimer->stop();
    m_decoder->clear();

    if (m_socket->state() == QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    } else {
        m_socket->disconnectFromHost();
    }
    setState(TcpConnectionState::Disconnected);
}

void TcpConnectionWorker::reconnectNow()
{
    if (m_socket->state() == QAbstractSocket::ConnectedState
        || m_socket->state() == QAbstractSocket::ConnectingState
        || m_host.isEmpty()) {
        return;
    }

    connectToHost(m_host, m_port);
}

void TcpConnectionWorker::shutdown()
{
    m_reconnectEnabled = false;
    m_userDisconnected = true;
    m_reconnectTimer->stop();
    m_socket->abort();
    setState(TcpConnectionState::Disconnected);
}

void TcpConnectionWorker::sendFrame(const Frame &frame)
{
    QString errorMessage;
    const QByteArray encoded = FrameCodec::encode(frame, &errorMessage);
    if (encoded.isEmpty()) {
        emit protocolErrorOccurred(errorMessage.isEmpty()
                                      ? QStringLiteral("帧编码失败")
                                      : errorMessage);
        return;
    }

    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        emit errorOccurred(QStringLiteral("TCP 未连接，无法发送帧"));
        return;
    }

    if (m_socket->write(encoded) != encoded.size()) {
        emit errorOccurred(QStringLiteral("TCP 写入失败：%1").arg(m_socket->errorString()));
        return;
    }
    m_socket->flush();
}

void TcpConnectionWorker::setReconnectEnabled(bool enabled)
{
    m_reconnectEnabled = enabled;
    if (!enabled) {
        m_reconnectTimer->stop();
        if (m_state == TcpConnectionState::Reconnecting) {
            setState(TcpConnectionState::Disconnected);
        }
    } else if (m_socket->state() == QAbstractSocket::UnconnectedState
               && !m_userDisconnected && !m_host.isEmpty()) {
        scheduleReconnect();
    }
}

void TcpConnectionWorker::setReconnectDelayMs(int delayMs)
{
    m_reconnectDelayMs = qMax(50, delayMs);
}

void TcpConnectionWorker::handleConnected()
{
    m_userDisconnected = false;
    m_reconnectTimer->stop();
    setState(TcpConnectionState::Connected);
    emit connected();
}

void TcpConnectionWorker::handleDisconnected()
{
    emit disconnected();
    if (m_userDisconnected) {
        setState(TcpConnectionState::Disconnected);
    }
}

void TcpConnectionWorker::handleSocketError()
{
    emit errorOccurred(QStringLiteral("TCP 错误：%1（%2）")
                           .arg(m_socket->errorString(),
                                socketStateText(m_socket->state())));
}

void TcpConnectionWorker::handleSocketStateChanged()
{
    if (m_socket->state() == QAbstractSocket::UnconnectedState
        && !m_userDisconnected) {
        scheduleReconnect();
    }
}

void TcpConnectionWorker::handleReadyRead()
{
    if (!m_socket) {
        return;
    }
    m_decoder->appendData(m_socket->readAll());
    processDecoderEvents();
}

void TcpConnectionWorker::processDecoderEvents()
{
    while (m_decoder->hasEvents()) {
        const FrameDecoder::Event event = m_decoder->takeNextEvent();
        if (event.type == FrameDecoder::EventType::DecodedFrame) {
            emit frameReceived(event.frame);
        } else {
            emit protocolErrorOccurred(event.message);
        }
    }
}

void TcpConnectionWorker::setState(TcpConnectionState state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(m_state);
}

void TcpConnectionWorker::scheduleReconnect()
{
    if (!m_reconnectEnabled || m_userDisconnected || m_host.isEmpty()
        || m_socket->state() != QAbstractSocket::UnconnectedState
        || m_reconnectTimer->isActive()) {
        return;
    }

    setState(TcpConnectionState::Reconnecting);
    m_reconnectTimer->start(m_reconnectDelayMs);
}
