#include "alarm/AlarmRule.h"

#include "core/Device.h"

#include <cmath>

namespace {
void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

bool isThresholdRule(AlarmRuleType type)
{
    return type == AlarmRuleType::HighThreshold
        || type == AlarmRuleType::LowThreshold;
}
}

QString alarmRuleTypeName(AlarmRuleType type)
{
    switch (type) {
    case AlarmRuleType::HighThreshold:
        return QStringLiteral("HighThreshold");
    case AlarmRuleType::LowThreshold:
        return QStringLiteral("LowThreshold");
    case AlarmRuleType::Offline:
        return QStringLiteral("Offline");
    }
    return QStringLiteral("Unknown");
}

bool AlarmRule::isValid(QString *errorMessage) const
{
    if (ruleId.trimmed().isEmpty() || ruleId.size() > 128) {
        setError(errorMessage, QStringLiteral("告警规则 ID 必须为 1-128 个字符"));
        return false;
    }
    if (!deviceId.trimmed().isEmpty() && !isValidDeviceId(deviceId)) {
        setError(errorMessage, QStringLiteral("告警规则的设备 ID 无效"));
        return false;
    }
    if (!isValidAlarmSeverity(severity)) {
        setError(errorMessage, QStringLiteral("告警级别无效"));
        return false;
    }
    if (activationDelay.count() < 0) {
        setError(errorMessage, QStringLiteral("告警持续时间不能为负数"));
        return false;
    }
    if (offlineTimeout.count() < 0) {
        setError(errorMessage, QStringLiteral("离线超时时间不能为负数"));
        return false;
    }

    if (isThresholdRule(type)) {
        if (!isValidMeasurementType(measurement)) {
            setError(errorMessage, QStringLiteral("告警测点类型无效"));
            return false;
        }
        if (!std::isfinite(threshold)) {
            setError(errorMessage, QStringLiteral("告警阈值必须为有限数值"));
            return false;
        }
        if (!std::isfinite(hysteresis) || hysteresis < 0.0) {
            setError(errorMessage, QStringLiteral("告警回差必须为非负有限数值"));
            return false;
        }
    }

    if (type == AlarmRuleType::Offline && offlineTimeout.count() == 0) {
        setError(errorMessage, QStringLiteral("离线告警超时时间必须大于 0"));
        return false;
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool AlarmRule::matchesDevice(const QString &candidateDeviceId) const
{
    return deviceId.trimmed().isEmpty() || deviceId == candidateDeviceId;
}

bool AlarmRule::conditionMet(double value) const
{
    switch (type) {
    case AlarmRuleType::HighThreshold:
        return value > threshold;
    case AlarmRuleType::LowThreshold:
        return value < threshold;
    case AlarmRuleType::Offline:
        return false;
    }
    return false;
}

bool AlarmRule::recoveryMet(double value) const
{
    switch (type) {
    case AlarmRuleType::HighThreshold:
        return value <= threshold - hysteresis;
    case AlarmRuleType::LowThreshold:
        return value >= threshold + hysteresis;
    case AlarmRuleType::Offline:
        return true;
    }
    return false;
}

QString alarmKeyFor(const QString &deviceId, const QString &ruleId)
{
    return deviceId + QLatin1Char('/') + ruleId;
}
