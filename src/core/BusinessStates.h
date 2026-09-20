#pragma once

#include <QString>

enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Stopping,
};

enum class CollectionState {
    Stopped,
    Running,
    Paused,
    Faulted,
};

enum class AlarmSeverity {
    Info,
    Warning,
    Critical,
};

enum class TelemetryStatus {
    Offline,
    Online,
    Stopped,
    Alarm,
};

QString connectionStateName(ConnectionState state);
QString collectionStateName(CollectionState state);
QString alarmSeverityName(AlarmSeverity severity);

bool isConnectionTransitionAllowed(ConnectionState from, ConnectionState to);
bool isCollectionTransitionAllowed(CollectionState from, CollectionState to);
bool isValidAlarmSeverity(AlarmSeverity severity);
QString telemetryStatusCode(TelemetryStatus status);
QString telemetryStatusDisplayName(TelemetryStatus status);
TelemetryStatus telemetryStatusFromString(const QString &value, bool *ok = nullptr);
