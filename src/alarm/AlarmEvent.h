#pragma once

#include "core/BusinessStates.h"

#include <QDateTime>
#include <QMetaType>
#include <QString>

enum class AlarmState {
    Normal,
    Active,
    Acknowledged,
    Cleared,
};

QString alarmStateName(AlarmState state);
bool isAlarmTransitionAllowed(AlarmState from, AlarmState to);

struct AlarmEvent
{
    QString eventId;
    QString alarmKey;
    QString ruleId;
    QString deviceId;
    AlarmSeverity severity = AlarmSeverity::Warning;
    AlarmState state = AlarmState::Normal;
    QString message;
    bool hasObservedValue = false;
    double observedValue = 0.0;
    double threshold = 0.0;
    QDateTime activatedAt;
    QDateTime acknowledgedAt;
    QDateTime clearedAt;
    QDateTime updatedAt;
    QString acknowledgedBy;
    int occurrenceCount = 1;
};

Q_DECLARE_METATYPE(AlarmState)
Q_DECLARE_METATYPE(AlarmEvent)
