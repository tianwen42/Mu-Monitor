#include "TcpDeviceDataSource.h"

#include <QMetaObject>
#include <QThread>

TcpDeviceDataSource::TcpDeviceDataSource(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<Frame>("Frame");
    qRegisterMetaType<TcpConnectionState>("TcpConnectionState");

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("TcpDeviceDataSourceThread"));

    m_worker = new TcpConnectionWorker;
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &TcpConnectionWorker::frameReceived,
            this, &TcpDeviceDataSource::frameReceived, Qt::QueuedConnection);
    connect(m_worker, &TcpConnectionWorker::connected,
            this, &TcpDeviceDataSource::connected, Qt::QueuedConnection);
    connect(m_worker, &TcpConnectionWorker::disconnected,
            this, &TcpDeviceDataSource::disconnected, Qt::QueuedConnection);
    connect(m_worker, &TcpConnectionWorker::errorOccurred,
            this, &TcpDeviceDataSource::errorOccurred, Qt::QueuedConnection);
    connect(m_worker, &TcpConnectionWorker::protocolErrorOccurred,
            this, &TcpDeviceDataSource::protocolErrorOccurred, Qt::QueuedConnection);
    connect(m_worker, &TcpConnectionWorker::reconnectScheduled,
            this, &TcpDeviceDataSource::reconnectScheduled, Qt::QueuedConnection);
    connect(m_worker, &TcpConnectionWorker::stateChanged, this,
            [this](TcpConnectionState state) {
                m_state.store(state);
                emit stateChanged(state);
            },
            Qt::QueuedConnection);

    m_thread->start();
}

TcpDeviceDataSource::~TcpDeviceDataSource()
{
    if (!m_thread || !m_thread->isRunning()) {
        return;
    }

    QMetaObject::invokeMethod(m_worker, "shutdown", Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait(3000);
}

TcpConnectionState TcpDeviceDataSource::state() const
{
    return m_state.load();
}

QString TcpDeviceDataSource::host() const
{
    return m_host;
}

quint16 TcpDeviceDataSource::port() const
{
    return m_port;
}

bool TcpDeviceDataSource::isReconnectEnabled() const
{
    return m_reconnectEnabled.load();
}

int TcpDeviceDataSource::reconnectDelayMs() const
{
    return m_reconnectDelayMs.load();
}

int TcpDeviceDataSource::reconnectMaxDelayMs() const
{
    return m_reconnectMaxDelayMs.load();
}

int TcpDeviceDataSource::connectTimeoutMs() const
{
    return m_connectTimeoutMs.load();
}

int TcpDeviceDataSource::readTimeoutMs() const
{
    return m_readTimeoutMs.load();
}

void TcpDeviceDataSource::connectToDevice(const QString &host, quint16 port)
{
    if (host.trimmed().isEmpty() || port == 0) {
        emit errorOccurred(QStringLiteral("TCP 目标地址或端口无效"));
        return;
    }

    m_host = host.trimmed();
    m_port = port;
    m_state.store(TcpConnectionState::Connecting);
    emit stateChanged(TcpConnectionState::Connecting);

    QMetaObject::invokeMethod(
        m_worker,
        [worker = m_worker, host = m_host, port = m_port]() {
            worker->connectToHost(host, port);
        },
        Qt::QueuedConnection);
}

void TcpDeviceDataSource::disconnectFromDevice()
{
    QMetaObject::invokeMethod(m_worker, &TcpConnectionWorker::disconnectFromHost,
                              Qt::QueuedConnection);
}

void TcpDeviceDataSource::reconnectNow()
{
    QMetaObject::invokeMethod(m_worker, &TcpConnectionWorker::reconnectNow,
                              Qt::QueuedConnection);
}

void TcpDeviceDataSource::sendFrame(const Frame &frame)
{
    QMetaObject::invokeMethod(
        m_worker,
        [worker = m_worker, frame]() {
            worker->sendFrame(frame);
        },
        Qt::QueuedConnection);
}

void TcpDeviceDataSource::setReconnectEnabled(bool enabled)
{
    m_reconnectEnabled.store(enabled);
    QMetaObject::invokeMethod(
        m_worker,
        [worker = m_worker, enabled]() {
            worker->setReconnectEnabled(enabled);
        },
        Qt::QueuedConnection);
}

void TcpDeviceDataSource::setReconnectDelayMs(int delayMs)
{
    const int boundedDelay = qMax(50, delayMs);
    m_reconnectDelayMs.store(boundedDelay);
    if (m_reconnectMaxDelayMs.load() < boundedDelay) {
        m_reconnectMaxDelayMs.store(boundedDelay);
    }
    QMetaObject::invokeMethod(
        m_worker,
        [worker = m_worker, boundedDelay]() {
            worker->setReconnectDelayMs(boundedDelay);
        },
        Qt::QueuedConnection);
}

void TcpDeviceDataSource::setReconnectMaxDelayMs(int delayMs)
{
    const int boundedDelay = qMax(m_reconnectDelayMs.load(), delayMs);
    m_reconnectMaxDelayMs.store(boundedDelay);
    QMetaObject::invokeMethod(
        m_worker,
        [worker = m_worker, boundedDelay]() {
            worker->setReconnectMaxDelayMs(boundedDelay);
        },
        Qt::QueuedConnection);
}

void TcpDeviceDataSource::setConnectTimeoutMs(int timeoutMs)
{
    const int boundedTimeout = qMax(0, timeoutMs);
    m_connectTimeoutMs.store(boundedTimeout);
    QMetaObject::invokeMethod(
        m_worker,
        [worker = m_worker, boundedTimeout]() {
            worker->setConnectTimeoutMs(boundedTimeout);
        },
        Qt::QueuedConnection);
}

void TcpDeviceDataSource::setReadTimeoutMs(int timeoutMs)
{
    const int boundedTimeout = qMax(0, timeoutMs);
    m_readTimeoutMs.store(boundedTimeout);
    QMetaObject::invokeMethod(
        m_worker,
        [worker = m_worker, boundedTimeout]() {
            worker->setReadTimeoutMs(boundedTimeout);
        },
        Qt::QueuedConnection);
}
