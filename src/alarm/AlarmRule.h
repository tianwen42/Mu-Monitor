#pragma once

#include "core/BusinessStates.h"
#include "core/TelemetrySample.h"

#include <QString>

#include <chrono>

enum class AlarmRuleType {
    HighThreshold,
    LowThreshold,
    Offline,
};

QString alarmRuleTypeName(AlarmRuleType type);

struct AlarmRule
{
    QString ruleId;
    QString deviceId;
    AlarmRuleType type = AlarmRuleType::HighThreshold;
    MeasurementType measurement = MeasurementType::Temperature;
    double threshold = 0.0;
    double hysteresis = 0.0;
    std::chrono::milliseconds activationDelay{0};
    std::chrono::milliseconds offlineTimeout{0};
    AlarmSeverity severity = AlarmSeverity::Warning;
    bool enabled = true;
    QString messageTemplate;

    bool isValid(QString *errorMessage = nullptr) const;
    bool matchesDevice(const QString &candidateDeviceId) const;
    bool conditionMet(double value) const;
    bool recoveryMet(double value) const;
};

QString alarmKeyFor(const QString &deviceId, const QString &ruleId);
