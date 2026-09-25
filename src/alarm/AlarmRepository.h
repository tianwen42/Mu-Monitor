#pragma once

#include "alarm/AlarmEvent.h"

#include <QList>
#include <QString>

#include <optional>

class IAlarmRepository
{
public:
    virtual ~IAlarmRepository() = default;

    virtual bool saveEvent(const AlarmEvent &event, QString *errorMessage = nullptr) = 0;
    virtual std::optional<AlarmEvent> latestEvent(
        const QString &alarmKey, QString *errorMessage = nullptr) const = 0;
    virtual QList<AlarmEvent> allEvents(QString *errorMessage = nullptr) const = 0;
    virtual QList<AlarmEvent> activeEvents(QString *errorMessage = nullptr) const = 0;
};
