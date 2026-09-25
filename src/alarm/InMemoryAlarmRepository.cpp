#include "alarm/InMemoryAlarmRepository.h"

namespace {
void clearError(QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }
}

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}
}

bool InMemoryAlarmRepository::saveEvent(const AlarmEvent &event, QString *errorMessage)
{
    if (event.eventId.trimmed().isEmpty() || event.alarmKey.trimmed().isEmpty()) {
        setError(errorMessage, QStringLiteral("告警事件缺少事件 ID 或告警键"));
        return false;
    }

    const auto existingKey = m_keyByEventId.constFind(event.eventId);
    if (existingKey != m_keyByEventId.cend() && existingKey.value() != event.alarmKey) {
        setError(errorMessage, QStringLiteral("同一个告警事件 ID 不能映射到不同告警键"));
        return false;
    }

    QList<AlarmEvent> &events = m_eventsByKey[event.alarmKey];
    for (AlarmEvent &stored : events) {
        if (stored.eventId == event.eventId) {
            stored = event;
            m_keyByEventId.insert(event.eventId, event.alarmKey);
            clearError(errorMessage);
            return true;
        }
    }

    events.append(event);
    m_keyByEventId.insert(event.eventId, event.alarmKey);
    clearError(errorMessage);
    return true;
}

std::optional<AlarmEvent> InMemoryAlarmRepository::latestEvent(
    const QString &alarmKey, QString *errorMessage) const
{
    clearError(errorMessage);
    const auto it = m_eventsByKey.constFind(alarmKey);
    if (it == m_eventsByKey.cend() || it.value().isEmpty()) {
        return std::nullopt;
    }
    return it.value().constLast();
}

QList<AlarmEvent> InMemoryAlarmRepository::allEvents(QString *errorMessage) const
{
    clearError(errorMessage);
    QList<AlarmEvent> result;
    for (auto it = m_eventsByKey.cbegin(); it != m_eventsByKey.cend(); ++it) {
        result.append(it.value());
    }
    return result;
}

QList<AlarmEvent> InMemoryAlarmRepository::activeEvents(QString *errorMessage) const
{
    clearError(errorMessage);
    QList<AlarmEvent> result;
    for (auto it = m_eventsByKey.cbegin(); it != m_eventsByKey.cend(); ++it) {
        for (const AlarmEvent &event : it.value()) {
            if (event.state == AlarmState::Active
                || event.state == AlarmState::Acknowledged) {
                result.append(event);
            }
        }
    }
    return result;
}
