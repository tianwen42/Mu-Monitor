#include "database/SqliteTelemetryRepository.h"

#include "database/DataDirectory.h"
#include "utils/TimeUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPromise>
#include <QSharedPointer>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>

#include <algorithm>
#include <deque>
#include <exception>
#include <functional>
#include <utility>

namespace {

using Task = std::function<void(class DatabaseWorker &, quint64)>;

struct QueueEntry
{
    quint64 requestId = 0;
    Task task;
};

bool fail(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
    return false;
}

template <typename T>
void resolvePromise(const QSharedPointer<QPromise<T>> &promise, T result)
{
    promise->addResult(std::move(result));
    promise->finish();
}

template <typename T>
QFuture<T> rejectedFuture(const QString &errorMessage)
{
    auto promise = QSharedPointer<QPromise<T>>::create();
    promise->start();
    QFuture<T> future = promise->future();
    T result;
    result.error = errorMessage;
    resolvePromise(promise, std::move(result));
    return future;
}

class DatabaseWorker final : public QObject
{
public:
    ~DatabaseWorker() override
    {
        close();
    }

    bool open(const QString &databasePath, int busyTimeoutMs, QString *errorMessage)
    {
        close();
        if (errorMessage) {
            errorMessage->clear();
        }

        if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
            return fail(errorMessage, QStringLiteral("未找到 Qt SQLite 驱动 QSQLITE"));
        }

        const QFileInfo databaseInfo(databasePath);
        if (!databaseInfo.exists()) {
            return fail(errorMessage,
                        QStringLiteral("数据库文件不存在，请先完成数据库初始化：%1")
                            .arg(databasePath));
        }
        if (!databaseInfo.isFile()) {
            return fail(errorMessage,
                        QStringLiteral("数据库路径不是普通文件：%1").arg(databasePath));
        }

        const QFileInfo directoryInfo(databaseInfo.absolutePath());
        if (!directoryInfo.isDir() || !directoryInfo.isWritable()) {
            return fail(errorMessage,
                        QStringLiteral("数据库目录不可写：%1").arg(directoryInfo.absoluteFilePath()));
        }

        m_connectionName = QStringLiteral("mu_monitor_telemetry_repository-%1")
                               .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        m_database.setDatabaseName(databaseInfo.absoluteFilePath());
        m_database.setConnectOptions(
            QStringLiteral("QSQLITE_BUSY_TIMEOUT=%1").arg(std::max(0, busyTimeoutMs)));

        if (!m_database.open()) {
            const QString message = QStringLiteral("无法打开遥测数据库 %1：%2")
                                        .arg(databaseInfo.absoluteFilePath(),
                                             m_database.lastError().text());
            close();
            return fail(errorMessage, message);
        }

        const QStringList setupStatements = {
            QStringLiteral("PRAGMA foreign_keys = ON"),
            QStringLiteral("PRAGMA busy_timeout = %1").arg(std::max(0, busyTimeoutMs)),
            QStringLiteral("PRAGMA journal_mode = WAL"),
            QStringLiteral("PRAGMA synchronous = NORMAL"),
        };

        for (const QString &statement : setupStatements) {
            QSqlQuery query(m_database);
            if (!query.exec(statement)) {
                const QString message = QStringLiteral("配置遥测数据库连接失败：%1；SQL：%2")
                                            .arg(query.lastError().text(), statement);
                close();
                return fail(errorMessage, message);
            }
        }

        {
            QSqlQuery check(m_database);
            if (!check.exec(QStringLiteral("PRAGMA quick_check"))
                || !check.next()
                || check.value(0).toString().compare(QStringLiteral("ok"),
                                                     Qt::CaseInsensitive) != 0) {
                const QString message = QStringLiteral("遥测数据库完整性检查失败：%1")
                                            .arg(check.lastError().text());
                close();
                return fail(errorMessage, message);
            }
        }

        int requiredTables = 0;
        {
            QSqlQuery tableQuery(m_database);
            if (!tableQuery.exec(QStringLiteral(
                    "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' "
                    "AND name IN ('telemetry', 'device_status')"))
                || !tableQuery.next()) {
                const QString message = QStringLiteral("检查遥测表结构失败：%1")
                                            .arg(tableQuery.lastError().text());
                close();
                return fail(errorMessage, message);
            }
            requiredTables = tableQuery.value(0).toInt();
        }

        if (requiredTables != 2) {
            const QString message = QStringLiteral(
                "数据库尚未初始化到可用的遥测 schema，请先运行 DatabaseManager 迁移。路径：%1")
                                        .arg(databaseInfo.absoluteFilePath());
            close();
            return fail(errorMessage, message);
        }

        return true;
    }

    void close()
    {
        if (m_database.isValid()) {
            m_database.close();
        }
        m_database = QSqlDatabase();
        if (!m_connectionName.isEmpty() && QSqlDatabase::contains(m_connectionName)) {
            QSqlDatabase::removeDatabase(m_connectionName);
        }
        m_connectionName.clear();
    }

    bool insertTelemetry(const QList<TelemetryRecord> &records, int batchSize,
                         int *insertedCount, QString *errorMessage)
    {
        if (insertedCount) {
            *insertedCount = 0;
        }
        if (errorMessage) {
            errorMessage->clear();
        }
        if (records.isEmpty()) {
            return true;
        }

        const int effectiveBatchSize = std::max(1, batchSize);
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral(
            "INSERT INTO telemetry ("
            "device_id, name, status, temperature, pressure, speed, voltage, recorded_at"
            ") VALUES (:device_id, :name, :status, :temperature, :pressure, "
            ":speed, :voltage, :recorded_at)"));

        for (int offset = 0; offset < records.size(); offset += effectiveBatchSize) {
            const int end = std::min(static_cast<int>(records.size()), offset + effectiveBatchSize);
            if (!m_database.transaction()) {
                return fail(errorMessage,
                            QStringLiteral("无法开始遥测数据事务：%1")
                                .arg(m_database.lastError().text()));
            }

            for (int index = offset; index < end; ++index) {
                const TelemetryRecord &record = records.at(index);
                const QDateTime timestamp = record.updatedAt.isValid()
                    ? record.updatedAt.toUTC()
                    : QDateTime::currentDateTimeUtc();

                query.bindValue(QStringLiteral(":device_id"), record.deviceId);
                query.bindValue(QStringLiteral(":name"), record.name);
                query.bindValue(QStringLiteral(":status"), telemetryStatusCode(record.status));
                query.bindValue(QStringLiteral(":temperature"), record.temperature);
                query.bindValue(QStringLiteral(":pressure"), record.pressure);
                query.bindValue(QStringLiteral(":speed"), record.speed);
                query.bindValue(QStringLiteral(":voltage"), record.voltage);
                query.bindValue(QStringLiteral(":recorded_at"),
                                TimeUtils::toUtcIso8601(timestamp));

                if (!query.exec()) {
                    m_database.rollback();
                    return fail(errorMessage,
                                QStringLiteral("写入遥测数据失败：%1")
                                    .arg(query.lastError().text()));
                }
            }

            if (!m_database.commit()) {
                m_database.rollback();
                return fail(errorMessage,
                            QStringLiteral("提交遥测数据失败：%1")
                                .arg(m_database.lastError().text()));
            }
            if (insertedCount) {
                *insertedCount += end - offset;
            }
        }

        return true;
    }

    bool insertHeartbeats(const QList<HeartbeatRecord> &records, int batchSize,
                          int *insertedCount, QString *errorMessage)
    {
        if (insertedCount) {
            *insertedCount = 0;
        }
        if (errorMessage) {
            errorMessage->clear();
        }
        if (records.isEmpty()) {
            return true;
        }

        const int effectiveBatchSize = std::max(1, batchSize);
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO device_status "
            "(device_id, online, collecting, heartbeat_at, latency_ms) "
            "VALUES (:device_id, :online, :collecting, :heartbeat_at, :latency_ms)"));

        for (int offset = 0; offset < records.size(); offset += effectiveBatchSize) {
            const int end = std::min(static_cast<int>(records.size()), offset + effectiveBatchSize);
            if (!m_database.transaction()) {
                return fail(errorMessage,
                            QStringLiteral("无法开始心跳事务：%1")
                                .arg(m_database.lastError().text()));
            }

            for (int index = offset; index < end; ++index) {
                const HeartbeatRecord &record = records.at(index);
                const QDateTime heartbeatAt = record.heartbeatAt.isValid()
                    ? record.heartbeatAt.toUTC()
                    : QDateTime::currentDateTimeUtc();

                query.bindValue(QStringLiteral(":device_id"), record.deviceId);
                query.bindValue(QStringLiteral(":online"), record.online ? 1 : 0);
                query.bindValue(QStringLiteral(":collecting"), record.collecting ? 1 : 0);
                query.bindValue(QStringLiteral(":heartbeat_at"),
                                TimeUtils::toUtcIso8601(heartbeatAt));
                query.bindValue(QStringLiteral(":latency_ms"), record.latencyMs);

                if (!query.exec()) {
                    m_database.rollback();
                    return fail(errorMessage,
                                QStringLiteral("写入心跳失败：%1")
                                    .arg(query.lastError().text()));
                }
            }

            if (!m_database.commit()) {
                m_database.rollback();
                return fail(errorMessage,
                            QStringLiteral("提交心跳失败：%1")
                                .arg(m_database.lastError().text()));
            }
            if (insertedCount) {
                *insertedCount += end - offset;
            }
        }

        return true;
    }

    TelemetryRecordsResult latestDeviceRecords()
    {
        TelemetryRecordsResult result;
        QSqlQuery query(m_database);
        const QString sql = QStringLiteral(
            "SELECT t.device_id, t.name, t.status, t.temperature, t.pressure, "
            "t.speed, t.voltage, t.recorded_at "
            "FROM telemetry t "
            "WHERE t.rowid = ("
            "  SELECT t2.rowid FROM telemetry t2 "
            "  WHERE t2.device_id = t.device_id "
            "  ORDER BY t2.recorded_at DESC, t2.rowid DESC LIMIT 1"
            ") "
            "ORDER BY t.device_id");

        if (!query.exec(sql)) {
            result.error = QStringLiteral("查询最新设备数据失败：%1")
                               .arg(query.lastError().text());
            return result;
        }

        while (query.next()) {
            TelemetryRecord record;
            record.deviceId = query.value(0).toString();
            record.name = query.value(1).toString();
            record.status = telemetryStatusFromString(query.value(2).toString());
            record.temperature = query.value(3).toDouble();
            record.pressure = query.value(4).toDouble();
            record.speed = query.value(5).toDouble();
            record.voltage = query.value(6).toDouble();
            record.updatedAt = TimeUtils::fromIso8601(query.value(7).toString());
            result.records.append(record);
        }

        result.success = true;
        return result;
    }

    HeartbeatRecordsResult latestHeartbeatRecords()
    {
        HeartbeatRecordsResult result;
        QSqlQuery query(m_database);
        if (!query.exec(QStringLiteral(
                "SELECT device_id, online, collecting, heartbeat_at, latency_ms "
                "FROM device_status ORDER BY device_id"))) {
            result.error = QStringLiteral("查询最新心跳失败：%1")
                               .arg(query.lastError().text());
            return result;
        }

        while (query.next()) {
            HeartbeatRecord record;
            record.deviceId = query.value(0).toString();
            record.online = query.value(1).toInt() != 0;
            record.collecting = query.value(2).toInt() != 0;
            record.heartbeatAt = TimeUtils::fromIso8601(query.value(3).toString());
            record.latencyMs = query.value(4).toInt();
            result.records.append(record);
        }

        result.success = true;
        return result;
    }

    TelemetryRecordsResult telemetryBetween(const QDateTime &start,
                                            const QDateTime &end)
    {
        TelemetryRecordsResult result;
        if (!start.isValid() || !end.isValid() || start > end) {
            result.error = QStringLiteral("时间范围无效");
            return result;
        }

        QSqlQuery query(m_database);
        query.prepare(QStringLiteral(
            "SELECT device_id, name, status, temperature, pressure, speed, voltage, recorded_at "
            "FROM telemetry WHERE recorded_at >= ? AND recorded_at <= ? "
            "ORDER BY recorded_at, device_id"));
        query.addBindValue(TimeUtils::toUtcIso8601(start));
        query.addBindValue(TimeUtils::toUtcIso8601(end));

        if (!query.exec()) {
            result.error = QStringLiteral("按时间范围查询遥测数据失败：%1")
                               .arg(query.lastError().text());
            return result;
        }

        while (query.next()) {
            TelemetryRecord record;
            record.deviceId = query.value(0).toString();
            record.name = query.value(1).toString();
            record.status = telemetryStatusFromString(query.value(2).toString());
            record.temperature = query.value(3).toDouble();
            record.pressure = query.value(4).toDouble();
            record.speed = query.value(5).toDouble();
            record.voltage = query.value(6).toDouble();
            record.updatedAt = TimeUtils::fromIso8601(query.value(7).toString());
            result.records.append(record);
        }

        result.success = true;
        return result;
    }

    TelemetryCountResult telemetryRecordCount()
    {
        TelemetryCountResult result;
        QSqlQuery query(m_database);
        if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM telemetry")) || !query.next()) {
            result.error = QStringLiteral("统计遥测记录数量失败：%1")
                               .arg(query.lastError().text());
            return result;
        }

        result.count = query.value(0).toLongLong();
        result.success = true;
        return result;
    }

private:
    QString m_connectionName;
    QSqlDatabase m_database;
};

bool validOptions(const TelemetryRepositoryOptions &options, QString *errorMessage)
{
    if (options.batchSize <= 0) {
        return fail(errorMessage, QStringLiteral("batchSize 必须大于 0"));
    }
    if (options.queueCapacity <= 0) {
        return fail(errorMessage, QStringLiteral("queueCapacity 必须大于 0"));
    }
    if (options.busyTimeoutMs < 0) {
        return fail(errorMessage, QStringLiteral("busyTimeoutMs 不能小于 0"));
    }
    return true;
}

} // namespace

class SqliteTelemetryRepositoryPrivate
{
public:
    SqliteTelemetryRepositoryPrivate(SqliteTelemetryRepository *owner,
                                     QString databasePath,
                                     TelemetryRepositoryOptions options)
        : q(owner)
        , m_databasePath(std::move(databasePath))
        , m_options(options)
    {
    }

    ~SqliteTelemetryRepositoryPrivate()
    {
        shutdown();
    }

    bool start(QString *errorMessage)
    {
        if (errorMessage) {
            errorMessage->clear();
        }
        if (!validOptions(m_options, errorMessage)) {
            return false;
        }

        QMutexLocker locker(&m_mutex);
        if (m_running) {
            return true;
        }
        if (m_thread || m_worker) {
            return fail(errorMessage, QStringLiteral("数据库线程状态异常，拒绝重复启动"));
        }

        QString resolvedPath = m_databasePath;
        if (resolvedPath.isEmpty()) {
            QString resolveError;
            const DataDirectory::Paths paths = DataDirectory::resolve(
                QCoreApplication::arguments(), QCoreApplication::applicationDirPath(),
                &resolveError);
            if (paths.root.isEmpty()) {
                return fail(errorMessage,
                            resolveError.isEmpty() ? QStringLiteral("无法解析数据目录")
                                                   : resolveError);
            }

            QString layoutError;
            if (!DataDirectory::ensureLayout(paths, &layoutError)) {
                return fail(errorMessage, layoutError);
            }
            resolvedPath = QDir(paths.database).filePath(QStringLiteral("mu-monitor.db"));
        }

        if (!QFileInfo(resolvedPath).isFile()) {
            return fail(errorMessage,
                        QStringLiteral("数据库文件不存在，请先完成数据库初始化：%1")
                            .arg(resolvedPath));
        }

        m_databasePath = QFileInfo(resolvedPath).absoluteFilePath();
        m_thread = new QThread;
        m_thread->setObjectName(QStringLiteral("Mu-Monitor telemetry database"));
        m_worker = new DatabaseWorker;
        m_worker->moveToThread(m_thread);

        QObject::connect(m_thread, &QThread::finished, m_worker,
                         [worker = m_worker]() { worker->close(); },
                         Qt::DirectConnection);
        QObject::connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

        QThread *thread = m_thread;
        DatabaseWorker *worker = m_worker;
        const QString databasePath = m_databasePath;
        const int busyTimeoutMs = m_options.busyTimeoutMs;
        locker.unlock();

        thread->start();
        bool opened = false;
        QString openError;
        const bool invoked = QMetaObject::invokeMethod(
            worker,
            [worker, databasePath, busyTimeoutMs, &opened, &openError]() {
                opened = worker->open(databasePath, busyTimeoutMs, &openError);
            },
            Qt::BlockingQueuedConnection);

        if (!invoked || !opened || !thread->isRunning()) {
            if (invoked) {
                QMetaObject::invokeMethod(worker, [worker]() { worker->close(); },
                                          Qt::BlockingQueuedConnection);
            }
            thread->quit();
            thread->wait();
            delete thread;

            locker.relock();
            m_thread = nullptr;
            m_worker = nullptr;
            m_running = false;
            m_accepting = false;
            m_wakeupScheduled = false;
            return fail(errorMessage,
                        openError.isEmpty() ? QStringLiteral("无法启动数据库工作线程")
                                            : openError);
        }

        locker.relock();
        m_running = true;
        m_accepting = true;
        m_wakeupScheduled = false;
        locker.unlock();

        emit q->databaseThreadStarted(worker->thread());
        emit q->queueDepthChanged(0, m_options.queueCapacity);
        return true;
    }

    void shutdown()
    {
        QThread *thread = nullptr;
        DatabaseWorker *worker = nullptr;
        {
            QMutexLocker locker(&m_mutex);
            if (!m_thread && !m_running) {
                return;
            }
            m_accepting = false;
            thread = m_thread;
            worker = m_worker;
        }

        if (thread && thread->isRunning() && worker) {
            const bool posted = QMetaObject::invokeMethod(
                worker,
                [this]() {
                    processQueue();
                    if (QThread *current = QThread::currentThread()) {
                        current->quit();
                    }
                },
                Qt::QueuedConnection);
            if (!posted) {
                thread->quit();
            }
            thread->wait();
        }

        delete thread;
        {
            QMutexLocker locker(&m_mutex);
            m_thread = nullptr;
            m_worker = nullptr;
            m_running = false;
            m_accepting = false;
            m_wakeupScheduled = false;
            m_tasks.clear();
            m_nextRequestId = 1;
        }
        emit q->queueDepthChanged(0, m_options.queueCapacity);
    }

    bool isRunning() const
    {
        QMutexLocker locker(&m_mutex);
        return m_running;
    }

    TelemetryRepositoryOptions options() const
    {
        QMutexLocker locker(&m_mutex);
        return m_options;
    }

    quint64 submitTelemetryBatch(const QList<TelemetryRecord> &records)
    {
        if (records.isEmpty()) {
            return 0;
        }

        const int batchSize = m_options.batchSize;
        return enqueueTask(
            [this, batchSize, records](DatabaseWorker &worker, quint64 requestId) {
                int insertedCount = 0;
                QString errorMessage;
                worker.insertTelemetry(records, batchSize, &insertedCount, &errorMessage);
                emit q->telemetryBatchCompleted(requestId, insertedCount, errorMessage);
                if (!errorMessage.isEmpty()) {
                    emit q->errorOccurred(
                        QStringLiteral("遥测批量写入失败：%1").arg(errorMessage));
                }
            });
    }

    quint64 submitHeartbeatBatch(const QList<HeartbeatRecord> &records)
    {
        if (records.isEmpty()) {
            return 0;
        }

        const int batchSize = m_options.batchSize;
        return enqueueTask(
            [this, batchSize, records](DatabaseWorker &worker, quint64 requestId) {
                int insertedCount = 0;
                QString errorMessage;
                worker.insertHeartbeats(records, batchSize, &insertedCount, &errorMessage);
                emit q->heartbeatBatchCompleted(requestId, insertedCount, errorMessage);
                if (!errorMessage.isEmpty()) {
                    emit q->errorOccurred(
                        QStringLiteral("心跳批量写入失败：%1").arg(errorMessage));
                }
            });
    }

    QFuture<TelemetryRecordsResult> latestDeviceRecords()
    {
        auto promise = QSharedPointer<QPromise<TelemetryRecordsResult>>::create();
        promise->start();
        QFuture<TelemetryRecordsResult> future = promise->future();
        const quint64 requestId = enqueueTask(
            [promise](DatabaseWorker &worker, quint64) {
                resolvePromise(promise, worker.latestDeviceRecords());
            });
        if (requestId == 0) {
            resolvePromise(promise, rejectedTelemetryRecords(
                QStringLiteral("数据库任务队列已关闭或已满")));
        }
        return future;
    }

    QFuture<HeartbeatRecordsResult> latestHeartbeatRecords()
    {
        auto promise = QSharedPointer<QPromise<HeartbeatRecordsResult>>::create();
        promise->start();
        QFuture<HeartbeatRecordsResult> future = promise->future();
        const quint64 requestId = enqueueTask(
            [promise](DatabaseWorker &worker, quint64) {
                resolvePromise(promise, worker.latestHeartbeatRecords());
            });
        if (requestId == 0) {
            HeartbeatRecordsResult result;
            result.error = QStringLiteral("数据库任务队列已关闭或已满");
            resolvePromise(promise, std::move(result));
        }
        return future;
    }

    QFuture<TelemetryRecordsResult> telemetryBetween(const QDateTime &start,
                                                     const QDateTime &end)
    {
        if (!start.isValid() || !end.isValid() || start > end) {
            return rejectedFuture<TelemetryRecordsResult>(QStringLiteral("时间范围无效"));
        }

        auto promise = QSharedPointer<QPromise<TelemetryRecordsResult>>::create();
        promise->start();
        QFuture<TelemetryRecordsResult> future = promise->future();
        const quint64 requestId = enqueueTask(
            [promise, start, end](DatabaseWorker &worker, quint64) {
                resolvePromise(promise, worker.telemetryBetween(start, end));
            });
        if (requestId == 0) {
            resolvePromise(promise, rejectedTelemetryRecords(
                QStringLiteral("数据库任务队列已关闭或已满")));
        }
        return future;
    }

    QFuture<TelemetryCountResult> telemetryRecordCount()
    {
        auto promise = QSharedPointer<QPromise<TelemetryCountResult>>::create();
        promise->start();
        QFuture<TelemetryCountResult> future = promise->future();
        const quint64 requestId = enqueueTask(
            [promise](DatabaseWorker &worker, quint64) {
                resolvePromise(promise, worker.telemetryRecordCount());
            });
        if (requestId == 0) {
            TelemetryCountResult result;
            result.error = QStringLiteral("数据库任务队列已关闭或已满");
            resolvePromise(promise, std::move(result));
        }
        return future;
    }

private:
    static TelemetryRecordsResult rejectedTelemetryRecords(const QString &errorMessage)
    {
        TelemetryRecordsResult result;
        result.error = errorMessage;
        return result;
    }

    quint64 enqueueTask(Task task)
    {
        QString reason;
        quint64 requestId = 0;
        int depth = 0;
        {
            QMutexLocker locker(&m_mutex);
            requestId = m_nextRequestId++;
            if (!m_running || !m_accepting || !m_thread || !m_worker) {
                reason = QStringLiteral("数据库工作线程未运行或正在关闭");
            } else if (static_cast<int>(m_tasks.size()) >= m_options.queueCapacity) {
                reason = QStringLiteral("数据库任务队列已满（容量 %1）")
                             .arg(m_options.queueCapacity);
            } else {
                m_tasks.push_back({requestId, std::move(task)});
                depth = static_cast<int>(m_tasks.size());
            }
        }

        if (!reason.isEmpty()) {
            emit q->requestRejected(requestId, reason);
            return 0;
        }

        emit q->queueDepthChanged(depth, m_options.queueCapacity);
        scheduleWakeup();
        return requestId;
    }

    void scheduleWakeup()
    {
        bool shouldPost = false;
        {
            QMutexLocker locker(&m_mutex);
            if (m_running && m_accepting && m_worker && !m_wakeupScheduled) {
                m_wakeupScheduled = true;
                shouldPost = true;
            }
        }

        if (shouldPost) {
            QMetaObject::invokeMethod(
                m_worker, [this]() { processQueue(); }, Qt::QueuedConnection);
        }
    }

    void processQueue()
    {
        {
            QMutexLocker locker(&m_mutex);
            m_wakeupScheduled = false;
        }

        while (true) {
            Task task;
            quint64 requestId = 0;
            int depth = 0;
            {
                QMutexLocker locker(&m_mutex);
                if (m_tasks.empty()) {
                    break;
                }
                requestId = m_tasks.front().requestId;
                task = std::move(m_tasks.front().task);
                m_tasks.pop_front();
                depth = static_cast<int>(m_tasks.size());
            }

            emit q->queueDepthChanged(depth, m_options.queueCapacity);
            try {
                if (task) {
                    task(*m_worker, requestId);
                }
            } catch (const std::exception &exception) {
                emit q->errorOccurred(
                    QStringLiteral("数据库任务执行异常：%1")
                        .arg(QString::fromUtf8(exception.what())));
            } catch (...) {
                emit q->errorOccurred(QStringLiteral("数据库任务执行未知异常"));
            }
        }

        bool shouldPost = false;
        {
            QMutexLocker locker(&m_mutex);
            if (!m_tasks.empty() && !m_wakeupScheduled) {
                m_wakeupScheduled = true;
                shouldPost = true;
            }
        }
        if (shouldPost) {
            QMetaObject::invokeMethod(
                m_worker, [this]() { processQueue(); }, Qt::QueuedConnection);
        }
    }

    SqliteTelemetryRepository *q = nullptr;
    mutable QMutex m_mutex;
    QString m_databasePath;
    TelemetryRepositoryOptions m_options;
    QThread *m_thread = nullptr;
    DatabaseWorker *m_worker = nullptr;
    std::deque<QueueEntry> m_tasks;
    quint64 m_nextRequestId = 1;
    bool m_running = false;
    bool m_accepting = false;
    bool m_wakeupScheduled = false;
};



TelemetryRepository::TelemetryRepository(QObject *parent)
    : QObject(parent)
{
}

TelemetryRepository::~TelemetryRepository() = default;

SqliteTelemetryRepository::SqliteTelemetryRepository(
    const QString &databasePath, const TelemetryRepositoryOptions &options,
    QObject *parent)
    : TelemetryRepository(parent)
    , d(std::make_unique<SqliteTelemetryRepositoryPrivate>(this, databasePath, options))
{
}

SqliteTelemetryRepository::~SqliteTelemetryRepository() = default;

bool SqliteTelemetryRepository::start(QString *errorMessage)
{
    return d->start(errorMessage);
}

void SqliteTelemetryRepository::shutdown()
{
    d->shutdown();
}

bool SqliteTelemetryRepository::isRunning() const
{
    return d->isRunning();
}

TelemetryRepositoryOptions SqliteTelemetryRepository::options() const
{
    return d->options();
}

quint64 SqliteTelemetryRepository::submitTelemetryBatch(
    const QList<TelemetryRecord> &records)
{
    return d->submitTelemetryBatch(records);
}

quint64 SqliteTelemetryRepository::submitHeartbeatBatch(
    const QList<HeartbeatRecord> &records)
{
    return d->submitHeartbeatBatch(records);
}

QFuture<TelemetryRecordsResult> SqliteTelemetryRepository::latestDeviceRecords()
{
    return d->latestDeviceRecords();
}

QFuture<HeartbeatRecordsResult> SqliteTelemetryRepository::latestHeartbeatRecords()
{
    return d->latestHeartbeatRecords();
}

QFuture<TelemetryRecordsResult> SqliteTelemetryRepository::telemetryBetween(
    const QDateTime &start, const QDateTime &end)
{
    return d->telemetryBetween(start, end);
}

QFuture<TelemetryCountResult> SqliteTelemetryRepository::telemetryRecordCount()
{
    return d->telemetryRecordCount();
}
