#include "core/BusinessStates.h"

QString connectionStateName(ConnectionState state)
{
    switch (state) {
    case ConnectionState::Disconnected:
        return QStringLiteral("Disconnected");
    case ConnectionState::Connecting:
        return QStringLiteral("Connecting");
    case ConnectionState::Connected:
        return QStringLiteral("Connected");
    case ConnectionState::Reconnecting:
        return QStringLiteral("Reconnecting");
    case ConnectionState::Stopping:
        return QStringLiteral("Stopping");
    }
    return QStringLiteral("Unknown");
}

QString collectionStateName(CollectionState state)
{
    switch (state) {
    case CollectionState::Stopped:
        return QStringLiteral("Stopped");
    case CollectionState::Running:
        return QStringLiteral("Running");
    case CollectionState::Paused:
        return QStringLiteral("Paused");
    case CollectionState::Faulted:
        return QStringLiteral("Faulted");
    }
    return QStringLiteral("Unknown");
}

QString alarmSeverityName(AlarmSeverity severity)
{
    switch (severity) {
    case AlarmSeverity::Info:
        return QStringLiteral("Info");
    case AlarmSeverity::Warning:
        return QStringLiteral("Warning");
    case AlarmSeverity::Critical:
        return QStringLiteral("Critical");
    }
    return QStringLiteral("Unknown");
}

QString telemetryStatusCode(TelemetryStatus status)
{
    switch (status) {
    case TelemetryStatus::Offline:
        return QStringLiteral("offline");
    case TelemetryStatus::Online:
        return QStringLiteral("online");
    case TelemetryStatus::Stopped:
        return QStringLiteral("stopped");
    case TelemetryStatus::Alarm:
        return QStringLiteral("alarm");
    }
    return QStringLiteral("offline");
}

QString telemetryStatusDisplayName(TelemetryStatus status)
{
    switch (status) {
    case TelemetryStatus::Offline:
        return QStringLiteral("离线");
    case TelemetryStatus::Online:
        return QStringLiteral("在线");
    case TelemetryStatus::Stopped:
        return QStringLiteral("已停止");
    case TelemetryStatus::Alarm:
        return QStringLiteral("报警");
    }
    return QStringLiteral("离线");
}

TelemetryStatus telemetryStatusFromString(const QString &value, bool *ok)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("offline")
        || normalized == QStringLiteral("离线")
        || normalized == QStringLiteral("未连接")) {
        if (ok) *ok = true;
        return TelemetryStatus::Offline;
    }
    if (normalized == QStringLiteral("online")
        || normalized == QStringLiteral("在线")
        || normalized == QStringLiteral("采集中")) {
        if (ok) *ok = true;
        return TelemetryStatus::Online;
    }
    if (normalized == QStringLiteral("stopped")
        || normalized == QStringLiteral("已停止")
        || normalized == QStringLiteral("停止")) {
        if (ok) *ok = true;
        return TelemetryStatus::Stopped;
    }
    if (normalized == QStringLiteral("alarm")
        || normalized == QStringLiteral("报警")) {
        if (ok) *ok = true;
        return TelemetryStatus::Alarm;
    }

    if (ok) *ok = false;
    return TelemetryStatus::Offline;
}
bool isConnectionTransitionAllowed(ConnectionState from, ConnectionState to)
{
    switch (from) {
    case ConnectionState::Disconnected:
        return to == ConnectionState::Connecting;
    case ConnectionState::Connecting:
        return to == ConnectionState::Connected
            || to == ConnectionState::Reconnecting
            || to == ConnectionState::Disconnected;
    case ConnectionState::Connected:
        return to == ConnectionState::Reconnecting
            || to == ConnectionState::Stopping
            || to == ConnectionState::Disconnected;
    case ConnectionState::Reconnecting:
        return to == ConnectionState::Connecting
            || to == ConnectionState::Connected
            || to == ConnectionState::Disconnected;
    case ConnectionState::Stopping:
        return to == ConnectionState::Disconnected;
    }
    return false;
}

bool isCollectionTransitionAllowed(CollectionState from, CollectionState to)
{
    switch (from) {
    case CollectionState::Stopped:
        return to == CollectionState::Running;
    case CollectionState::Running:
        return to == CollectionState::Paused
            || to == CollectionState::Stopped
            || to == CollectionState::Faulted;
    case CollectionState::Paused:
        return to == CollectionState::Running
            || to == CollectionState::Stopped
            || to == CollectionState::Faulted;
    case CollectionState::Faulted:
        return to == CollectionState::Stopped
            || to == CollectionState::Running;
    }
    return false;
}

bool isValidAlarmSeverity(AlarmSeverity severity)
{
    switch (severity) {
    case AlarmSeverity::Info:
    case AlarmSeverity::Warning:
    case AlarmSeverity::Critical:
        return true;
    }
    return false;
}
