#include "core/TelemetrySample.h"

#include "core/Device.h"

#include <QSet>

#include <cmath>

namespace {
void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}
}

QString measurementTypeName(MeasurementType type)
{
    switch (type) {
    case MeasurementType::Temperature:
        return QStringLiteral("Temperature");
    case MeasurementType::Pressure:
        return QStringLiteral("Pressure");
    case MeasurementType::Speed:
        return QStringLiteral("Speed");
    case MeasurementType::Voltage:
        return QStringLiteral("Voltage");
    }
    return QStringLiteral("Unknown");
}

QString qualityCodeName(QualityCode quality)
{
    switch (quality) {
    case QualityCode::Good:
        return QStringLiteral("Good");
    case QualityCode::Uncertain:
        return QStringLiteral("Uncertain");
    case QualityCode::Bad:
        return QStringLiteral("Bad");
    case QualityCode::Stale:
        return QStringLiteral("Stale");
    }
    return QStringLiteral("Unknown");
}

bool isValidMeasurementType(MeasurementType type)
{
    switch (type) {
    case MeasurementType::Temperature:
    case MeasurementType::Pressure:
    case MeasurementType::Speed:
    case MeasurementType::Voltage:
        return true;
    }
    return false;
}

bool isValidQualityCode(QualityCode quality)
{
    switch (quality) {
    case QualityCode::Good:
    case QualityCode::Uncertain:
    case QualityCode::Bad:
    case QualityCode::Stale:
        return true;
    }
    return false;
}

bool TelemetrySample::isValid(QString *errorMessage) const
{
    if (!isValidDeviceId(deviceId)) {
        setError(errorMessage, QStringLiteral("遥测样本的设备 ID 无效"));
        return false;
    }
    if (!collectedAt.isValid() || collectedAt.timeSpec() != Qt::UTC) {
        setError(errorMessage, QStringLiteral("遥测采集时间必须为有效的 UTC 时间"));
        return false;
    }
    if (measurements.isEmpty()) {
        setError(errorMessage, QStringLiteral("遥测样本至少需要一个测点"));
        return false;
    }

    QSet<int> seenTypes;
    for (const MeasurementValue &measurement : measurements) {
        if (!isValidMeasurementType(measurement.type)) {
            setError(errorMessage, QStringLiteral("测点类型无效"));
            return false;
        }
        const int typeKey = static_cast<int>(measurement.type);
        if (seenTypes.contains(typeKey)) {
            setError(errorMessage, QStringLiteral("同一遥测样本中不能包含重复测点"));
            return false;
        }
        seenTypes.insert(typeKey);

        if (!isValidQualityCode(measurement.quality)) {
            setError(errorMessage, QStringLiteral("数据质量码无效"));
            return false;
        }
        if (!std::isfinite(measurement.value)) {
            setError(errorMessage, QStringLiteral("测点值必须为有限数值"));
            return false;
        }
    }

    return true;
}

const MeasurementValue *TelemetrySample::measurement(MeasurementType type) const
{
    for (const MeasurementValue &measurement : measurements) {
        if (measurement.type == type) {
            return &measurement;
        }
    }
    return nullptr;
}
