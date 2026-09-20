#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

enum class MeasurementType {
    Temperature,
    Pressure,
    Speed,
    Voltage,
};

enum class QualityCode {
    Good,
    Uncertain,
    Bad,
    Stale,
};

struct MeasurementValue
{
    MeasurementType type = MeasurementType::Temperature;
    double value = 0.0;
    QualityCode quality = QualityCode::Good;
};

struct TelemetrySample
{
    QString deviceId;
    QDateTime collectedAt;
    QList<MeasurementValue> measurements;

    bool isValid(QString *errorMessage = nullptr) const;
    const MeasurementValue *measurement(MeasurementType type) const;
};

QString measurementTypeName(MeasurementType type);
QString qualityCodeName(QualityCode quality);
bool isValidMeasurementType(MeasurementType type);
bool isValidQualityCode(QualityCode quality);
