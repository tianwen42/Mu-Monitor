#include "alarm/AlarmEngine.h"

#include "alarm/InMemoryAlarmRepository.h"
#include "core/Device.h"

#include <QUuid>

#include <utility>

namespace {
void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

QString formatNumber(double value)
{
    return QString::number(value, 'f', 3);
}
}

AlarmEngine::AlarmEngine(QObject *parent)
    : AlarmEngine(nullptr, parent)
{
}

AlarmEngine::AlarmEngine(IAlarmRepository *repository, QObject *parent)
    : QObject(parent)
    , m_clock([]() { return QDateTime::currentDateTimeUtc(); })
{
    setRepository(repository);
}

AlarmEngine::~AlarmEngine() = default;

void AlarmEngine::setClock(Clock clock)
{
    m_clock = clock ? std::move(clock)
                    : Clock([]() { return QDateTime::currentDateTimeUtc(); });
}

QDateTime AlarmEngine::currentTime() const
{
    QDateTime now = m_clock ? m_clock() : QDateTime::currentDateTimeUtc();
    if (!now.isValid()) {
        return QDateTime::currentDateTimeUtc();
    }
    return now.toUTC();
}

void AlarmEngine::setRepository(IAlarmRepository *repository)
{
    m_ownedRepository.reset();
    m_repository = repository;
    if (!m_repository) {
        m_ownedRepository = std::make_unique<InMemoryAlarmRepository>();
        m_repository = m_ownedRepository.get();
    }

    QString errorMessage;
    if (!restore(&errorMessage)) {
        emit errorOccurred(QStringLiteral("恢复告警状态失败：%1").arg(errorMessage));
    }
}

IAlarmRepository *AlarmEngine::repository() const
{
    return m_repository;
}

void AlarmEngine::setRules(const QList<AlarmRule> &rules)
{
    m_rules.clear();
    m_candidates.clear();

    for (const AlarmRule &rule : rules) {
        QString errorMessage;
        if (!rule.isValid(&errorMessage)) {
            emit errorOccurred(
                QStringLiteral("忽略无效告警规则 %1：%2").arg(rule.ruleId, errorMessage));
            continue;
        }

        bool replaced = false;
        for (AlarmRule &stored : m_rules) {
            if (stored.ruleId == rule.ruleId) {
                stored = rule;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            m_rules.append(rule);
        }
    }
}

bool AlarmEngine::addRule(const AlarmRule &rule, QString *errorMessage)
{
    if (!rule.isValid(errorMessage)) {
        return false;
    }

    for (AlarmRule &stored : m_rules) {
        if (stored.ruleId == rule.ruleId) {
            stored = rule;
            if (errorMessage) {
                errorMessage->clear();
            }
            return true;
        }
    }

    m_rules.append(rule);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

QList<AlarmRule> AlarmEngine::rules() const
{
    return m_rules;
}

void AlarmEngine::clearRules()
{
    m_rules.clear();
    m_candidates.clear();
}

bool AlarmEngine::restore(QString *errorMessage)
{
    if (!m_repository) {
        setError(errorMessage, QStringLiteral("告警仓储未配置"));
        return false;
    }

    QString repositoryError;
    const QList<AlarmEvent> events = m_repository->allEvents(&repositoryError);
    if (!repositoryError.isEmpty()) {
        setError(errorMessage, repositoryError);
        return false;
    }

    m_activeEvents.clear();
    m_lastEvents.clear();
    m_candidates.clear();

    QHash<QString, AlarmEvent> latestEvents;
    for (const AlarmEvent &event : events) {
        if (event.alarmKey.trimmed().isEmpty()) {
            continue;
        }

        const auto existing = latestEvents.constFind(event.alarmKey);
        if (existing == latestEvents.cend()
            || existing.value().updatedAt < event.updatedAt) {
            latestEvents.insert(event.alarmKey, event);
        }
    }

    for (auto it = latestEvents.cbegin(); it != latestEvents.cend(); ++it) {
        const AlarmEvent &event = it.value();
        m_lastEvents.insert(event.alarmKey, event);
        if (event.state == AlarmState::Active
            || event.state == AlarmState::Acknowledged) {
            m_activeEvents.insert(event.alarmKey, event);
        }
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

void AlarmEngine::processTelemetry(const TelemetrySample &sample)
{
    QString errorMessage;
    if (!sample.isValid(&errorMessage)) {
        emit errorOccurred(QStringLiteral("处理遥测告警失败：%1").arg(errorMessage));
        return;
    }

    const QDateTime at = currentTime();
    for (const AlarmRule &rule : m_rules) {
        if (!rule.enabled || rule.type == AlarmRuleType::Offline
            || !rule.matchesDevice(sample.deviceId)) {
            continue;
        }

        const MeasurementValue *measurement = sample.measurement(rule.measurement);
        if (!measurement || measurement->quality != QualityCode::Good) {
            continue;
        }

        const double value = measurement->value;
        evaluateRuleState(rule, sample.deviceId, rule.conditionMet(value),
                          rule.recoveryMet(value), value, at);
    }
}

void AlarmEngine::processHeartbeat(const QString &deviceId, bool online,
                                   const QDateTime &receivedAt)
{
    if (!isValidDeviceId(deviceId)) {
        emit errorOccurred(QStringLiteral("处理心跳失败：设备 ID 无效"));
        return;
    }

    QDateTime at = receivedAt.isValid() ? receivedAt.toUTC() : currentTime();
    m_lastHeartbeat.insert(deviceId, at);
    m_deviceOnline.insert(deviceId, online);

    for (const AlarmRule &rule : m_rules) {
        if (!rule.enabled || rule.type != AlarmRuleType::Offline
            || !rule.matchesDevice(deviceId)) {
            continue;
        }

        evaluateRuleState(rule, deviceId, !online, online, std::nullopt, at);
    }

    updateOfflineStates(at);
}

void AlarmEngine::updateOfflineStates(const QDateTime &at)
{
    const QDateTime checkAt = at.isValid() ? at.toUTC() : currentTime();

    for (const AlarmRule &rule : m_rules) {
        if (!rule.enabled || rule.type != AlarmRuleType::Offline) {
            continue;
        }

        for (auto it = m_lastHeartbeat.cbegin(); it != m_lastHeartbeat.cend(); ++it) {
            const QString &deviceId = it.key();
            if (!rule.matchesDevice(deviceId)) {
                continue;
            }

            const bool online = m_deviceOnline.value(deviceId, false);
            bool offline = !online;
            if (online && it.value().isValid() && it.value() <= checkAt
                && it.value().msecsTo(checkAt) >= rule.offlineTimeout.count()) {
                offline = true;
            }

            evaluateRuleState(rule, deviceId, offline, !offline,
                              std::nullopt, checkAt);
        }
    }
}

bool AlarmEngine::acknowledge(const QString &eventId, const QString &operatorId,
                              const QDateTime &at)
{
    if (eventId.trimmed().isEmpty() || operatorId.trimmed().isEmpty()) {
        return false;
    }

    auto it = m_activeEvents.begin();
    for (; it != m_activeEvents.end(); ++it) {
        if (it.value().eventId == eventId) {
            break;
        }
    }
    if (it == m_activeEvents.end() || it.value().state != AlarmState::Active) {
        return false;
    }

    AlarmEvent updated = it.value();
    const AlarmState previous = updated.state;
    const QDateTime acknowledgedAt = at.isValid() ? at.toUTC() : currentTime();
    if (!isAlarmTransitionAllowed(previous, AlarmState::Acknowledged)) {
        return false;
    }

    updated.state = AlarmState::Acknowledged;
    updated.acknowledgedAt = acknowledgedAt;
    updated.updatedAt = acknowledgedAt;
    updated.acknowledgedBy = operatorId.trimmed();

    QString errorMessage;
    if (!m_repository->saveEvent(updated, &errorMessage)) {
        emit errorOccurred(QStringLiteral("保存告警确认状态失败：%1").arg(errorMessage));
        return false;
    }

    m_activeEvents.insert(it.key(), updated);
    m_lastEvents.insert(it.key(), updated);
    emit alarmStateChanged(updated, previous, AlarmState::Acknowledged);
    emit alarmAcknowledged(updated);
    return true;
}

QList<AlarmEvent> AlarmEngine::activeEvents() const
{
    QList<AlarmEvent> events;
    events.reserve(m_activeEvents.size());
    for (auto it = m_activeEvents.cbegin(); it != m_activeEvents.cend(); ++it) {
        events.append(it.value());
    }
    return events;
}

std::optional<AlarmEvent> AlarmEngine::eventById(const QString &eventId) const
{
    for (auto it = m_activeEvents.cbegin(); it != m_activeEvents.cend(); ++it) {
        if (it.value().eventId == eventId) {
            return it.value();
        }
    }
    for (auto it = m_lastEvents.cbegin(); it != m_lastEvents.cend(); ++it) {
        if (it.value().eventId == eventId) {
            return it.value();
        }
    }
    return std::nullopt;
}

std::optional<AlarmEvent> AlarmEngine::lastEvent(const QString &alarmKey) const
{
    const auto it = m_lastEvents.constFind(alarmKey);
    if (it == m_lastEvents.cend()) {
        return std::nullopt;
    }
    return it.value();
}

AlarmState AlarmEngine::stateFor(const QString &alarmKey) const
{
    const auto it = m_activeEvents.constFind(alarmKey);
    if (it != m_activeEvents.cend()) {
        return it.value().state;
    }
    return AlarmState::Normal;
}

void AlarmEngine::evaluateRuleState(const AlarmRule &rule, const QString &deviceId,
                                    bool condition, bool recovery,
                                    std::optional<double> observedValue,
                                    const QDateTime &at)
{
    const QString alarmKey = alarmKeyFor(deviceId, rule.ruleId);
    auto activeIt = m_activeEvents.find(alarmKey);
    if (activeIt != m_activeEvents.end()) {
        AlarmEvent &event = activeIt.value();
        event.updatedAt = at;
        if (observedValue) {
            event.hasObservedValue = true;
            event.observedValue = *observedValue;
        }
        m_lastEvents.insert(alarmKey, event);

        if (condition) {
            m_candidates.remove(alarmKey);
            return;
        }

        if (recovery || rule.type == AlarmRuleType::Offline) {
            clearAlarm(alarmKey, at);
        }
        return;
    }

    if (!condition) {
        m_candidates.remove(alarmKey);
        return;
    }

    const qint64 delayMs = rule.activationDelay.count();
    if (delayMs <= 0) {
        m_candidates.remove(alarmKey);
        raiseAlarm(rule, deviceId, observedValue, at);
        return;
    }

    auto candidateIt = m_candidates.find(alarmKey);
    if (candidateIt == m_candidates.end() || candidateIt.value() > at) {
        m_candidates.insert(alarmKey, at);
        return;
    }

    if (candidateIt.value().msecsTo(at) >= delayMs) {
        m_candidates.remove(alarmKey);
        raiseAlarm(rule, deviceId, observedValue, at);
    }
}

void AlarmEngine::raiseAlarm(const AlarmRule &rule, const QString &deviceId,
                             std::optional<double> observedValue,
                             const QDateTime &at)
{
    const QString alarmKey = alarmKeyFor(deviceId, rule.ruleId);
    if (m_activeEvents.contains(alarmKey)) {
        return;
    }

    AlarmEvent event;
    event.eventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    event.alarmKey = alarmKey;
    event.ruleId = rule.ruleId;
    event.deviceId = deviceId;
    event.severity = rule.severity;
    event.state = AlarmState::Active;
    event.message = formatMessage(rule, deviceId, observedValue);
    event.hasObservedValue = observedValue.has_value();
    event.observedValue = observedValue.value_or(0.0);
    event.threshold = rule.type == AlarmRuleType::Offline
        ? static_cast<double>(rule.offlineTimeout.count()) / 1000.0
        : rule.threshold;
    event.activatedAt = at;
    event.updatedAt = at;

    persistEvent(event);
    m_activeEvents.insert(alarmKey, event);
    m_lastEvents.insert(alarmKey, event);

    emit alarmStateChanged(event, AlarmState::Normal, AlarmState::Active);
    emit alarmRaised(event);
}

void AlarmEngine::clearAlarm(const QString &alarmKey, const QDateTime &at)
{
    const auto activeIt = m_activeEvents.constFind(alarmKey);
    if (activeIt == m_activeEvents.cend()) {
        return;
    }

    AlarmEvent cleared = activeIt.value();
    const AlarmState previous = cleared.state;
    if (!isAlarmTransitionAllowed(previous, AlarmState::Cleared)) {
        return;
    }

    cleared.state = AlarmState::Cleared;
    cleared.clearedAt = at;
    cleared.updatedAt = at;

    persistEvent(cleared);
    m_activeEvents.remove(alarmKey);
    m_lastEvents.insert(alarmKey, cleared);
    m_candidates.remove(alarmKey);

    emit alarmStateChanged(cleared, previous, AlarmState::Cleared);
    emit alarmCleared(cleared);
    emit alarmStateChanged(cleared, AlarmState::Cleared, AlarmState::Normal);
}

void AlarmEngine::persistEvent(const AlarmEvent &event)
{
    if (!m_repository) {
        return;
    }

    QString errorMessage;
    if (!m_repository->saveEvent(event, &errorMessage)) {
        emit errorOccurred(QStringLiteral("保存告警事件失败：%1").arg(errorMessage));
    }
}

QString AlarmEngine::formatMessage(const AlarmRule &rule,
                                   const QString &deviceId,
                                   std::optional<double> observedValue) const
{
    QString message;
    if (rule.type == AlarmRuleType::Offline) {
        message = QStringLiteral("设备 %1 离线，超时阈值 %2 秒")
                      .arg(deviceId, formatNumber(
                           static_cast<double>(rule.offlineTimeout.count()) / 1000.0));
    } else if (rule.type == AlarmRuleType::HighThreshold) {
        message = QStringLiteral("设备 %1 的 %2 高于阈值 %3，当前值 %4")
                      .arg(deviceId,
                           measurementTypeName(rule.measurement),
                           formatNumber(rule.threshold),
                           observedValue ? formatNumber(*observedValue)
                                         : QStringLiteral("未知"));
    } else {
        message = QStringLiteral("设备 %1 的 %2 低于阈值 %3，当前值 %4")
                      .arg(deviceId,
                           measurementTypeName(rule.measurement),
                           formatNumber(rule.threshold),
                           observedValue ? formatNumber(*observedValue)
                                         : QStringLiteral("未知"));
    }

    if (!rule.messageTemplate.trimmed().isEmpty()) {
        message = rule.messageTemplate;
        message.replace(QStringLiteral("{device}"), deviceId);
        message.replace(QStringLiteral("{measurement}"),
                        measurementTypeName(rule.measurement));
        message.replace(QStringLiteral("{value}"),
                        observedValue ? formatNumber(*observedValue)
                                      : QStringLiteral("未知"));
        message.replace(QStringLiteral("{threshold}"),
                        formatNumber(rule.type == AlarmRuleType::Offline
                                         ? static_cast<double>(rule.offlineTimeout.count()) / 1000.0
                                         : rule.threshold));
    }
    return message;
}
