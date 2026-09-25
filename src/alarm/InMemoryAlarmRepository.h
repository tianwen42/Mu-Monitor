#pragma once

#include "alarm/AlarmRepository.h"

#include <QHash>
#include <QList>

class InMemoryAlarmRepository final : public IAlarmRepository
{
public:
    bool saveEvent(const AlarmEvent &event, QString *errorMessage = nullptr) override;
    std::optional<AlarmEvent> latestEvent(
        const QString &alarmKey, QString *errorMessage = nullptr) const override;
    QList<AlarmEvent> allEvents(QString *errorMessage = nullptr) const override;
    QList<AlarmEvent> activeEvents(QString *errorMessage = nullptr) const override;

private:
    QHash<QString, QList<AlarmEvent>> m_eventsByKey;
    QHash<QString, QString> m_keyByEventId;
};
