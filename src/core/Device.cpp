#include "core/Device.h"

#include <QRegularExpression>

namespace {
void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}
}

QString protocolTypeName(ProtocolType protocol)
{
    switch (protocol) {
    case ProtocolType::Tcp:
        return QStringLiteral("TCP");
    case ProtocolType::ModbusTcp:
        return QStringLiteral("Modbus TCP");
    case ProtocolType::Mqtt:
        return QStringLiteral("MQTT");
    case ProtocolType::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}

bool isValidDeviceId(const QString &deviceId)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    return pattern.match(deviceId.trimmed()).hasMatch();
}

bool isValidProtocolType(ProtocolType protocol)
{
    switch (protocol) {
    case ProtocolType::Tcp:
    case ProtocolType::ModbusTcp:
    case ProtocolType::Mqtt:
        return true;
    case ProtocolType::Unknown:
        return false;
    }
    return false;
}

bool Device::isValid(QString *errorMessage) const
{
    if (!isValidDeviceId(id)) {
        setError(errorMessage, QStringLiteral("设备 ID 必须为 1-64 位字母、数字、点、下划线或连字符"));
        return false;
    }
    if (name.trimmed().isEmpty()) {
        setError(errorMessage, QStringLiteral("设备名称不能为空"));
        return false;
    }
    if (address.trimmed().isEmpty()) {
        setError(errorMessage, QStringLiteral("设备地址不能为空"));
        return false;
    }
    if (port == 0) {
        setError(errorMessage, QStringLiteral("设备端口必须在 1-65535 范围内"));
        return false;
    }
    if (!isValidProtocolType(protocol)) {
        setError(errorMessage, QStringLiteral("设备协议类型无效"));
        return false;
    }
    return true;
}
