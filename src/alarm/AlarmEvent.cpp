#include "alarm/AlarmEvent.h"

QString alarmStateName(AlarmState state)
{
    switch (state) {
    case AlarmState::Normal:
        return QStringLiteral("Normal");
    case AlarmState::Active:
        return QStringLiteral("Active");
    case AlarmState::Acknowledged:
        return QStringLiteral("Acknowledged");
    case AlarmState::Cleared:
        return QStringLiteral("Cleared");
    }
    return QStringLiteral("Unknown");
}

bool isAlarmTransitionAllowed(AlarmState from, AlarmState to)
{
    switch (from) {
    case AlarmState::Normal:
        return to == AlarmState::Active;
    case AlarmState::Active:
        return to == AlarmState::Acknowledged
            || to == AlarmState::Cleared;
    case AlarmState::Acknowledged:
        return to == AlarmState::Cleared;
    case AlarmState::Cleared:
        return to == AlarmState::Normal;
    }
    return false;
}
