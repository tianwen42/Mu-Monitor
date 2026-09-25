#include "app/AppController.h"

#include "app/MonitoringService.h"
#include "database/DatabaseManager.h"
#include "network/IDeviceDataSource.h"

#include <QDateTime>

AppController::AppController(IDeviceDataSource *dataSource, const QString &currentUser,
                             QObject *parent)
    : QObject(parent)
    , m_dataSource(dataSource)
    , m_currentUser(currentUser)
{
    m_monitoringService = new MonitoringService(m_dataSource, this);

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
    QString errorMessage;
    if (!DatabaseManager::instance().insertTelemetryRecords(records, &errorMessage)) {
        emit errorOccurred(
            QStringLiteral("遥测数据入库失败：%1").arg(errorMessage));
    }
}

void AppController::persistHeartbeats(const QList<HeartbeatRecord> &heartbeats)
{
    QString errorMessage;
    if (!DatabaseManager::instance().insertHeartbeatRecords(
            heartbeats, &errorMessage)) {
        emit errorOccurred(
            QStringLiteral("心跳数据入库失败：%1").arg(errorMessage));
    }
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