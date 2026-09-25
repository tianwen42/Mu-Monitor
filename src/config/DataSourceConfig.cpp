#include "config/DataSourceConfig.h"

#include <QSettings>
#include <QVariant>

namespace {

constexpr auto kDataSourceTypeKey = "dataSource/type";
constexpr auto kHostKey = "dataSource/host";
constexpr auto kPortKey = "dataSource/port";
constexpr auto kSamplingIntervalKey = "dataSource/samplingIntervalMs";
constexpr auto kHeartbeatIntervalKey = "dataSource/heartbeatIntervalMs";
constexpr auto kReconnectEnabledKey = "dataSource/reconnect/enabled";
constexpr auto kReconnectDelayKey = "dataSource/reconnect/delayMs";
constexpr auto kReconnectMaxDelayKey = "dataSource/reconnect/maxDelayMs";
constexpr auto kConnectTimeoutKey = "dataSource/reconnect/connectTimeoutMs";
constexpr auto kReadTimeoutKey = "dataSource/reconnect/readTimeoutMs";

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

quint16 readPort(const QSettings &settings, quint16 defaultValue)
{
    bool ok = false;
    const uint value = settings.value(QString::fromLatin1(kPortKey), defaultValue).toUInt(&ok);
    if (!ok || value > 65535U) {
        return 0;
    }
    return static_cast<quint16>(value);
}

} // namespace

QString dataSourceTypeToString(DataSourceType type)
{
    switch (type) {
    case DataSourceType::Simulation:
        return QStringLiteral("simulation");
    case DataSourceType::Tcp:
        return QStringLiteral("tcp");
    case DataSourceType::Invalid:
        return QStringLiteral("invalid");
    }
    return QStringLiteral("invalid");
}

DataSourceType dataSourceTypeFromString(const QString &value, bool *ok)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("simulation")
        || normalized == QStringLiteral("simulator")) {
        if (ok) {
            *ok = true;
        }
        return DataSourceType::Simulation;
    }
    if (normalized == QStringLiteral("tcp")
        || normalized == QStringLiteral("network")) {
        if (ok) {
            *ok = true;
        }
        return DataSourceType::Tcp;
    }

    if (ok) {
        *ok = false;
    }
    return DataSourceType::Invalid;
}

bool DataSourceConfig::isValid(QString *errorMessage) const
{
    if (type == DataSourceType::Invalid) {
        setError(errorMessage, QStringLiteral("数据源类型无效"));
        return false;
    }
    if (samplingIntervalMs <= 0) {
        setError(errorMessage, QStringLiteral("采样周期必须大于 0"));
        return false;
    }
    if (heartbeatIntervalMs <= 0) {
        setError(errorMessage, QStringLiteral("心跳周期必须大于 0"));
        return false;
    }

    if (type == DataSourceType::Simulation) {
        return true;
    }

    if (host.trimmed().isEmpty()) {
        setError(errorMessage, QStringLiteral("TCP 主机地址不能为空"));
        return false;
    }
    if (port == 0) {
        setError(errorMessage, QStringLiteral("TCP 端口必须在 1 到 65535 之间"));
        return false;
    }
    if (reconnectDelayMs < 0) {
        setError(errorMessage, QStringLiteral("重连初始延迟不能小于 0"));
        return false;
    }
    if (reconnectMaxDelayMs < reconnectDelayMs) {
        setError(errorMessage, QStringLiteral("重连最大延迟不能小于初始延迟"));
        return false;
    }
    if (connectTimeoutMs <= 0) {
        setError(errorMessage, QStringLiteral("连接超时时间必须大于 0"));
        return false;
    }
    if (readTimeoutMs <= 0) {
        setError(errorMessage, QStringLiteral("读取超时时间必须大于 0"));
        return false;
    }
    return true;
}

ApplicationConfig ApplicationConfig::fromSettings(const QSettings &settings)
{
    ApplicationConfig config;

    if (settings.contains(QString::fromLatin1(kDataSourceTypeKey))) {
        bool ok = false;
        config.dataSource.type = dataSourceTypeFromString(
            settings.value(QString::fromLatin1(kDataSourceTypeKey)).toString(), &ok);
        if (!ok) {
            config.dataSource.type = DataSourceType::Invalid;
        }
    }

    config.dataSource.host =
        settings.value(QString::fromLatin1(kHostKey), config.dataSource.host).toString();
    config.dataSource.port = readPort(settings, config.dataSource.port);
    config.dataSource.samplingIntervalMs =
        settings.value(QString::fromLatin1(kSamplingIntervalKey),
                       config.dataSource.samplingIntervalMs).toInt();
    config.dataSource.heartbeatIntervalMs =
        settings.value(QString::fromLatin1(kHeartbeatIntervalKey),
                       config.dataSource.heartbeatIntervalMs).toInt();
    config.dataSource.reconnectEnabled =
        settings.value(QString::fromLatin1(kReconnectEnabledKey),
                       config.dataSource.reconnectEnabled).toBool();
    config.dataSource.reconnectDelayMs =
        settings.value(QString::fromLatin1(kReconnectDelayKey),
                       config.dataSource.reconnectDelayMs).toInt();
    config.dataSource.reconnectMaxDelayMs =
        settings.value(QString::fromLatin1(kReconnectMaxDelayKey),
                       config.dataSource.reconnectMaxDelayMs).toInt();
    config.dataSource.connectTimeoutMs =
        settings.value(QString::fromLatin1(kConnectTimeoutKey),
                       config.dataSource.connectTimeoutMs).toInt();
    config.dataSource.readTimeoutMs =
        settings.value(QString::fromLatin1(kReadTimeoutKey),
                       config.dataSource.readTimeoutMs).toInt();

    return config;
}

void ApplicationConfig::save(QSettings &settings) const
{
    settings.setValue(QString::fromLatin1(kDataSourceTypeKey),
                      dataSourceTypeToString(dataSource.type));
    settings.setValue(QString::fromLatin1(kHostKey), dataSource.host);
    settings.setValue(QString::fromLatin1(kPortKey), dataSource.port);
    settings.setValue(QString::fromLatin1(kSamplingIntervalKey),
                      dataSource.samplingIntervalMs);
    settings.setValue(QString::fromLatin1(kHeartbeatIntervalKey),
                      dataSource.heartbeatIntervalMs);
    settings.setValue(QString::fromLatin1(kReconnectEnabledKey),
                      dataSource.reconnectEnabled);
    settings.setValue(QString::fromLatin1(kReconnectDelayKey),
                      dataSource.reconnectDelayMs);
    settings.setValue(QString::fromLatin1(kReconnectMaxDelayKey),
                      dataSource.reconnectMaxDelayMs);
    settings.setValue(QString::fromLatin1(kConnectTimeoutKey),
                      dataSource.connectTimeoutMs);
    settings.setValue(QString::fromLatin1(kReadTimeoutKey),
                      dataSource.readTimeoutMs);
}