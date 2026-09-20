#pragma once

#include <QString>
#include <QtGlobal>

enum class ProtocolType {
    Unknown,
    Tcp,
    ModbusTcp,
    Mqtt,
};

struct Device
{
    QString id;
    QString name;
    QString address;
    quint16 port = 0;
    ProtocolType protocol = ProtocolType::Unknown;
    bool enabled = true;

    bool isValid(QString *errorMessage = nullptr) const;
};

QString protocolTypeName(ProtocolType protocol);
bool isValidDeviceId(const QString &deviceId);
bool isValidProtocolType(ProtocolType protocol);
