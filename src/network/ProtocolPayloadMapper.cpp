#include "network/ProtocolPayloadMapper.h"

#include "core/Device.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTimeZone>

#include <cmath>

namespace {

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

bool parseObject(const QByteArray &payload, QJsonObject *object, QString *errorMessage)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage,
                 QStringLiteral("协议负载不是合法 JSON 对象：%1")
                     .arg(parseError.errorString()));
        return false;
    }
    if (object) {
        *object = document.object();
    }
    return true;
}

bool resolveTimestamp(const QJsonObject &object, qint64 timestampUtcMs,
                      QDateTime *timestamp, QString *errorMessage)
{
    if (timestampUtcMs > 0) {
        const QDateTime resolved =
            QDateTime::fromMSecsSinceEpoch(timestampUtcMs, QTimeZone::UTC);
        if (!resolved.isValid()) {
            setError(errorMessage, QStringLiteral("协议帧时间戳无效"));
            return false;
        }
        if (timestamp) {
            *timestamp = resolved;
        }
        return true;
    }

    const QString text = object.value(QStringLiteral("timestamp")).toString();
    QDateTime resolved = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!resolved.isValid()) {
        resolved = QDateTime::fromString(text, Qt::ISODate);
    }
    if (!resolved.isValid()) {
        setError(errorMessage, QStringLiteral("协议负载缺少有效时间戳"));
        return false;
    }
    if (timestamp) {
        *timestamp = resolved.toUTC();
    }
    return true;
}

bool readFiniteNumber(const QJsonObject &object, const QString &name,
                      double *value, QString *errorMessage)
{
    const QJsonValue jsonValue = object.value(name);
    if (!jsonValue.isDouble()) {
        setError(errorMessage,
                 QStringLiteral("协议负载缺少数值字段：%1").arg(name));
        return false;
    }
    const double number = jsonValue.toDouble();
    if (!std::isfinite(number)) {
        setError(errorMessage,
                 QStringLiteral("协议负载字段不是有限数值：%1").arg(name));
        return false;
    }
    if (value) {
        *value = number;
    }
    return true;
}

} // namespace

bool ProtocolPayloadMapper::toTelemetrySample(
    const QString &deviceId, qint64 timestampUtcMs, const QByteArray &payload,
    TelemetrySample *sample, QString *errorMessage)
{
    if (!sample) {
        setError(errorMessage, QStringLiteral("遥测输出对象为空"));
        return false;
    }
    if (!isValidDeviceId(deviceId)) {
        setError(errorMessage, QStringLiteral("协议帧设备 ID 无效"));
        return false;
    }

    QJsonObject object;
    if (!parseObject(payload, &object, errorMessage)) {
        return false;
    }
    if (object.contains(QStringLiteral("online"))
        && !object.value(QStringLiteral("online")).toBool(true)) {
        setError(errorMessage, QStringLiteral("设备离线，跳过遥测样本"));
        return false;
    }
    if (object.contains(QStringLiteral("collecting"))
        && !object.value(QStringLiteral("collecting")).toBool(true)) {
        setError(errorMessage, QStringLiteral("设备未采集，跳过遥测样本"));
        return false;
    }

    QDateTime timestamp;
    if (!resolveTimestamp(object, timestampUtcMs, &timestamp, errorMessage)) {
        return false;
    }

    double temperature = 0.0;
    double pressure = 0.0;
    double speed = 0.0;
    double voltage = 0.0;
    if (!readFiniteNumber(object, QStringLiteral("temperature"), &temperature, errorMessage)
        || !readFiniteNumber(object, QStringLiteral("pressure"), &pressure, errorMessage)
        || !readFiniteNumber(object, QStringLiteral("speed"), &speed, errorMessage)
        || !readFiniteNumber(object, QStringLiteral("voltage"), &voltage, errorMessage)) {
        return false;
    }

    TelemetrySample mapped;
    mapped.deviceId = deviceId;
    mapped.collectedAt = timestamp;
    mapped.measurements = {
        {MeasurementType::Temperature, temperature, QualityCode::Good},
        {MeasurementType::Pressure, pressure, QualityCode::Good},
        {MeasurementType::Speed, speed, QualityCode::Good},
        {MeasurementType::Voltage, voltage, QualityCode::Good},
    };

    QString validationError;
    if (!mapped.isValid(&validationError)) {
        setError(errorMessage, validationError);
        return false;
    }

    *sample = mapped;
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool ProtocolPayloadMapper::toHeartbeatRecord(
    const QString &deviceId, qint64 timestampUtcMs, const QByteArray &payload,
    HeartbeatRecord *record, QString *errorMessage)
{
    if (!record) {
        setError(errorMessage, QStringLiteral("心跳输出对象为空"));
        return false;
    }
    if (!isValidDeviceId(deviceId)) {
        setError(errorMessage, QStringLiteral("协议帧设备 ID 无效"));
        return false;
    }

    QJsonObject object;
    if (!payload.trimmed().isEmpty() && !parseObject(payload, &object, errorMessage)) {
        return false;
    }

    QDateTime timestamp;
    if (!resolveTimestamp(object, timestampUtcMs, &timestamp, errorMessage)) {
        return false;
    }

    const bool online = object.value(QStringLiteral("online")).toBool(true);
    const bool collecting = object.value(QStringLiteral("collecting")).toBool(online);
    const int latencyMs = object.value(QStringLiteral("latencyMs")).toInt(-1);

    HeartbeatRecord mapped;
    mapped.deviceId = deviceId;
    mapped.heartbeatAt = timestamp;
    mapped.online = online;
    mapped.collecting = collecting;
    mapped.latencyMs = online ? latencyMs : -1;

    *record = mapped;
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}