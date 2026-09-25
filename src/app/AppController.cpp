#include "app/AppController.h"

#include "app/MonitoringService.h"
#include "database/DatabaseManager.h"
#include "network/IDeviceDataSource.h"

#include <QDateTime>
#include <QPromise>
#include <QSharedPointer>

#include <utility>

namespace {

template <typename T>
QFuture<T> rejectedFuture(const QString &errorMessage)
{
    auto promise = QSharedPointer<QPromise<T>>::create();
    promise->start();
    QFuture<T> future = promise->future();
    T result;
    result.error = errorMessage;
    promise->addResult(std::move(result));
    promise->finish();
    return future;
}

} // namespace

AppController::AppController(IDeviceDataSource *dataSource,
                             TelemetryRepository *repository,
                             const QString &currentUser, QObject *parent)
    : QObject(parent)
    , m_dataSource(dataSource)
    , m_repository(repository)
    , m_currentUser(currentUser)
{
    m_monitoringService = new MonitoringService(m_dataSource, this);

    if (m_repository) {
        m_persistenceQueueCapacity = m_repository->options().queueCapacity;
        connect(m_repository, &TelemetryRepository::telemetryBatchCompleted,
                this, &AppController::handleTelemetryBatchCompleted);
        connect(m_repository, &TelemetryRepository::heartbeatBatchCompleted,
                this, &AppController::handleHeartbeatBatchCompleted);
        connect(m_repository, &TelemetryRepository::requestRejected,
                this, &AppController::handleRequestRejected);
        connect(m_repository, &TelemetryRepository::queueDepthChanged,
                this, &AppController::handleQueueDepthChanged);
        connect(m_repository, &TelemetryRepository::errorOccurred,
                this, &AppController::handleRepositoryError);
    }

    connect(m_monitoringService, &MonitoringService::telemetryBatchReceived,
            this, [this](const QList<TelemetryRecord> &records) {
                persistTelemetry(records);
                emit telemetryBatchReceived(records);
            });
    connect(m_monitoringService, &MonitoringService::heartbeatBatchReceived,
            this, [this](const QList<HeartbeatRecord> &heartbeats) {
                persistHeartbeats(heartbeats);
                emit heartbeatBatchReceived(heartbeats);
            });
    connect(m_monitoringService, &MonitoringService::connectionStateChanged,
            this, &AppController::connectionStateChanged);
    connect(m_monitoringService, &MonitoringService::collectionStateChanged,
            this, &AppController::collectionStateChanged);
    connect(m_monitoringService, &MonitoringService::deviceStateChanged,
            this, &AppController::deviceStateChanged);
    connect(m_monitoringService, &MonitoringService::onlineDeviceCountChanged,
            this, &AppController::onlineDeviceCountChanged);
    connect(m_monitoringService, &MonitoringService::alarmRaised,
            this, [this](const QString &deviceId, const QString &message) {
                persistAlarm(deviceId, message);
                emit alarmRaised(deviceId, message);
            });
    connect(m_monitoringService, &MonitoringService::errorOccurred,
            this, [this](const QString &message) {
                DatabaseManager::instance().insertLog(
                    QStringLiteral("ERROR"), QStringLiteral("application"), message);
                emit errorOccurred(message);
            });

    loadDevices();
}

AppController::~AppController()
{
    if (m_repository) {
        QObject::disconnect(m_repository, nullptr, this, nullptr);
    }

    if (!m_monitoringService) {
        return;
    }

    QObject::disconnect(m_monitoringService, nullptr, this, nullptr);
    if (m_dataSource && m_dataSource->isRunning()) {
        m_monitoringService->stop();
    }
}

QList<DeviceInfo> AppController::devices() const
{
    return m_devices;
}

QString AppController::currentUser() const
{
    return m_currentUser;
}

QString AppController::currentUserRole() const
{
    return DatabaseManager::instance().roleForUser(m_currentUser);
}

QString AppController::databasePath() const
{
    return DatabaseManager::instance().databasePath();
}

bool AppController::start()
{
    const bool started = m_monitoringService->start();
    DatabaseManager::instance().insertLog(
        QStringLiteral("INFO"), QStringLiteral("connection"),
        started
            ? QStringLiteral("用户 %1 启动监控").arg(m_currentUser)
            : QStringLiteral("用户 %1 启动监控失败").arg(m_currentUser));
    return started;
}

void AppController::stop()
{
    m_monitoringService->stop();
    DatabaseManager::instance().insertLog(
        QStringLiteral("INFO"), QStringLiteral("connection"),
        QStringLiteral("用户 %1 停止监控").arg(m_currentUser));
}

bool AppController::pauseCollection()
{
    const bool paused = m_monitoringService->pauseCollection();
    if (paused) {
        DatabaseManager::instance().insertLog(
            QStringLiteral("INFO"), QStringLiteral("collection"),
            QStringLiteral("用户 %1 暂停采集").arg(m_currentUser));
    }
    return paused;
}

bool AppController::resumeCollection()
{
    const bool resumed = m_monitoringService->resumeCollection();
    if (resumed) {
        DatabaseManager::instance().insertLog(
            QStringLiteral("INFO"), QStringLiteral("collection"),
            QStringLiteral("用户 %1 继续采集").arg(m_currentUser));
    }
    return resumed;
}

bool AppController::setDeviceCollection(const QString &deviceId, bool enabled)
{
    const bool changed = m_monitoringService->setDeviceCollection(deviceId, enabled);
    if (changed) {
        DatabaseManager::instance().insertLog(
            QStringLiteral("INFO"), QStringLiteral("device"),
            QStringLiteral("用户 %1 %2设备采集：%3")
                .arg(m_currentUser,
                     enabled ? QStringLiteral("开始") : QStringLiteral("停止"),
                     deviceId));
    }
    return changed;
}

ConnectionState AppController::connectionState() const
{
    return m_monitoringService->connectionState();
}

CollectionState AppController::collectionState() const
{
    return m_monitoringService->collectionState();
}

bool AppController::isDeviceOnline(const QString &deviceId) const
{
    return m_monitoringService->isDeviceOnline(deviceId);
}

bool AppController::isDeviceCollecting(const QString &deviceId) const
{
    return m_monitoringService->isDeviceCollecting(deviceId);
}

int AppController::onlineDeviceCount() const
{
    return m_monitoringService->onlineDeviceCount();
}

// Legacy synchronous readers retained for source compatibility with the
// current UI. Async readers below are the migration target.
QList<TelemetryRecord> AppController::latestDeviceRecords(QString *errorMessage) const
{
    return DatabaseManager::instance().latestDeviceRecords(errorMessage);
}

qint64 AppController::telemetryRecordCount(QString *errorMessage) const
{
    return DatabaseManager::instance().telemetryRecordCount(errorMessage);
}

QList<TelemetryRecord> AppController::recentTelemetryRecords(
    int limit, const QString &deviceId, QString *errorMessage) const
{
    return DatabaseManager::instance().recentTelemetryRecords(
        limit, deviceId, errorMessage);
}

QList<TelemetryRecord> AppController::telemetryHistory(
    const QDateTime &start, const QDateTime &end, const QString &deviceId,
    int limit, QString *errorMessage) const
{
    return DatabaseManager::instance().telemetryHistory(
        start, end, deviceId, limit, errorMessage);
}

QList<HeartbeatRecord> AppController::latestHeartbeatRecords(
    QString *errorMessage) const
{
    return DatabaseManager::instance().latestHeartbeatRecords(errorMessage);
}

QList<AlarmRecord> AppController::alarmHistoryForDevice(
    const QString &deviceId, int limit, QString *errorMessage) const
{
    return DatabaseManager::instance().alarmHistoryForDevice(
        deviceId, limit, errorMessage);
}

bool AppController::updateDeviceInfo(const DeviceInfo &device,
                                     QString *errorMessage) const
{
    return DatabaseManager::instance().updateDeviceInfo(device, errorMessage);
}

QFuture<TelemetryRecordsResult> AppController::latestDeviceRecordsAsync()
{
    if (!m_repository) {
        return rejectedFuture<TelemetryRecordsResult>(QStringLiteral("遥测仓库未配置"));
    }
    return m_repository->latestDeviceRecords();
}

QFuture<TelemetryCountResult> AppController::telemetryRecordCountAsync()
{
    if (!m_repository) {
        return rejectedFuture<TelemetryCountResult>(QStringLiteral("遥测仓库未配置"));
    }
    return m_repository->telemetryRecordCount();
}

QFuture<TelemetryRecordsResult> AppController::recentTelemetryRecordsAsync(
    int limit, const QString &deviceId)
{
    if (!m_repository) {
        return rejectedFuture<TelemetryRecordsResult>(QStringLiteral("遥测仓库未配置"));
    }
    return m_repository->recentTelemetryRecords(limit, deviceId);
}

QFuture<TelemetryRecordsResult> AppController::telemetryHistoryAsync(
    const QDateTime &start, const QDateTime &end, const QString &deviceId,
    int limit)
{
    if (!m_repository) {
        return rejectedFuture<TelemetryRecordsResult>(QStringLiteral("遥测仓库未配置"));
    }
    return m_repository->telemetryHistory(start, end, deviceId, limit);
}

QFuture<HeartbeatRecordsResult> AppController::latestHeartbeatRecordsAsync()
{
    if (!m_repository) {
        return rejectedFuture<HeartbeatRecordsResult>(QStringLiteral("遥测仓库未配置"));
    }
    return m_repository->latestHeartbeatRecords();
}

int AppController::persistenceQueueDepth() const
{
    return m_persistenceQueueDepth;
}

int AppController::persistenceQueueCapacity() const
{
    return m_persistenceQueueCapacity;
}

int AppController::pendingPersistenceRequests() const
{
    return m_pendingRepositoryRequests.size();
}

quint64 AppController::acceptedTelemetryBatches() const
{
    return m_acceptedTelemetryBatches;
}

quint64 AppController::acceptedHeartbeatBatches() const
{
    return m_acceptedHeartbeatBatches;
}

quint64 AppController::completedTelemetryBatches() const
{
    return m_completedTelemetryBatches;
}

quint64 AppController::completedHeartbeatBatches() const
{
    return m_completedHeartbeatBatches;
}

quint64 AppController::rejectedPersistenceRequests() const
{
    return m_rejectedPersistenceRequests;
}

quint64 AppController::failedPersistenceBatches() const
{
    return m_failedPersistenceBatches;
}

QString AppController::lastPersistenceError() const
{
    return m_lastPersistenceError;
}

void AppController::logEvent(const QString &level, const QString &source,
                             const QString &message) const
{
    DatabaseManager::instance().insertLog(level, source, message);
}

void AppController::loadDevices()
{
    QList<DeviceInfo> defaults;
    defaults.reserve(100);
    for (int i = 1; i <= 100; ++i) {
        DeviceInfo device;
        device.deviceId = QStringLiteral("DEV-%1").arg(i, 3, 10, QLatin1Char('0'));
        device.name = QStringLiteral("模拟设备 %1").arg(i, 3, 10, QLatin1Char('0'));
        device.model = QStringLiteral("MU-%1").arg(i, 3, 10, QLatin1Char('0'));
        device.location = QStringLiteral("产线 %1").arg(((i - 1) / 10) + 1);
        device.protocol = (i % 2 == 0)
            ? QStringLiteral("Modbus TCP")
            : QStringLiteral("TCP");
        defaults.append(device);
    }

    QString errorMessage;
    if (!DatabaseManager::instance().ensureDeviceInfos(defaults, &errorMessage)) {
        emit errorOccurred(
            QStringLiteral("初始化设备信息失败：%1").arg(errorMessage));
    }

    QHash<QString, DeviceInfo> storedDevices;
    const QList<DeviceInfo> stored =
        DatabaseManager::instance().deviceInfos(&errorMessage);
    if (!errorMessage.isEmpty()) {
        emit errorOccurred(
            QStringLiteral("读取设备信息失败：%1").arg(errorMessage));
    }
    for (const DeviceInfo &device : stored) {
        storedDevices.insert(device.deviceId, device);
    }

    m_devices.clear();
    m_devices.reserve(defaults.size());
    for (const DeviceInfo &defaultDevice : defaults) {
        m_devices.append(storedDevices.value(defaultDevice.deviceId, defaultDevice));
    }

    m_monitoringService->setDevices(m_devices);
    emit devicesChanged(m_devices);
}

void AppController::persistTelemetry(const QList<TelemetryRecord> &records)
{
    if (records.isEmpty()) {
        return;
    }
    if (!m_repository) {
        handleRepositoryError(QStringLiteral("遥测仓库未配置"));
        return;
    }

    const quint64 rejectedBefore = m_rejectedPersistenceRequests;
    const quint64 requestId = m_repository->submitTelemetryBatch(records);
    if (requestId == 0) {
        if (m_rejectedPersistenceRequests == rejectedBefore) {
            handleRequestRejected(0, QStringLiteral("遥测批量提交被拒绝"));
        }
        return;
    }

    m_pendingRepositoryRequests.insert(requestId);
    ++m_acceptedTelemetryBatches;
    emit persistenceStatusChanged();
}

void AppController::persistHeartbeats(const QList<HeartbeatRecord> &heartbeats)
{
    if (heartbeats.isEmpty()) {
        return;
    }
    if (!m_repository) {
        handleRepositoryError(QStringLiteral("遥测仓库未配置"));
        return;
    }

    const quint64 rejectedBefore = m_rejectedPersistenceRequests;
    const quint64 requestId = m_repository->submitHeartbeatBatch(heartbeats);
    if (requestId == 0) {
        if (m_rejectedPersistenceRequests == rejectedBefore) {
            handleRequestRejected(0, QStringLiteral("心跳批量提交被拒绝"));
        }
        return;
    }

    m_pendingRepositoryRequests.insert(requestId);
    ++m_acceptedHeartbeatBatches;
    emit persistenceStatusChanged();
}

void AppController::persistAlarm(const QString &deviceId, const QString &message)
{
    QString errorMessage;
    if (!DatabaseManager::instance().insertAlarmRecord(
            deviceId, QStringLiteral("WARN"), message, &errorMessage)) {
        emit errorOccurred(
            QStringLiteral("告警记录入库失败：%1").arg(errorMessage));
    }
    DatabaseManager::instance().insertLog(
        QStringLiteral("WARN"), QStringLiteral("alarm"),
        QStringLiteral("%1 %2").arg(deviceId, message));
}

void AppController::handleTelemetryBatchCompleted(
    quint64 requestId, int insertedCount, const QString &error)
{
    Q_UNUSED(insertedCount)
    m_pendingRepositoryRequests.remove(requestId);
    ++m_completedTelemetryBatches;
    if (!error.isEmpty()) {
        ++m_failedPersistenceBatches;
        m_lastPersistenceError = error;
    }
    emit persistenceStatusChanged();
}

void AppController::handleHeartbeatBatchCompleted(
    quint64 requestId, int insertedCount, const QString &error)
{
    Q_UNUSED(insertedCount)
    m_pendingRepositoryRequests.remove(requestId);
    ++m_completedHeartbeatBatches;
    if (!error.isEmpty()) {
        ++m_failedPersistenceBatches;
        m_lastPersistenceError = error;
    }
    emit persistenceStatusChanged();
}

void AppController::handleRequestRejected(quint64 requestId,
                                          const QString &reason)
{
    Q_UNUSED(requestId)
    ++m_rejectedPersistenceRequests;
    m_lastPersistenceError = reason;
    emit persistenceStatusChanged();
    emit errorOccurred(QStringLiteral("数据库任务被拒绝：%1").arg(reason));
}

void AppController::handleQueueDepthChanged(int depth, int capacity)
{
    m_persistenceQueueDepth = depth;
    m_persistenceQueueCapacity = capacity;
    emit persistenceQueueChanged(depth, capacity);
    emit persistenceStatusChanged();
}

void AppController::handleRepositoryError(const QString &message)
{
    m_lastPersistenceError = message;
    emit persistenceStatusChanged();
    emit errorOccurred(message);
}
