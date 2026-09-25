#include "network/DeviceDataSourceFactory.h"

#include "network/IDeviceDataSource.h"
#include "network/SimulationDataSource.h"
#include "network/TcpDeviceDataSource.h"
#include "network/TcpDeviceDataSourceAdapter.h"

#include <QObject>
#include <QString>

namespace {

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

IDeviceDataSource *createSimulationSource(const DataSourceConfig &config,
                                          QObject *parent)
{
    auto *source = new SimulationDataSource(parent);
    source->setSamplingInterval(config.samplingIntervalMs);
    source->setHeartbeatInterval(config.heartbeatIntervalMs);
    return source;
}

IDeviceDataSource *createTcpSource(const DataSourceConfig &config, QObject *parent)
{
    auto *source = new TcpDeviceDataSourceAdapter(parent);
    source->setEndpoint(config.host.trimmed(), config.port);

    TcpDeviceDataSource *tcpSource = source->tcpDeviceDataSource();
    tcpSource->setReconnectEnabled(config.reconnectEnabled);
    tcpSource->setReconnectDelayMs(config.reconnectDelayMs);
    tcpSource->setReconnectMaxDelayMs(config.reconnectMaxDelayMs);
    tcpSource->setConnectTimeoutMs(config.connectTimeoutMs);
    tcpSource->setReadTimeoutMs(config.readTimeoutMs);
    return source;
}

} // namespace

IDeviceDataSource *DeviceDataSourceFactory::create(
    const DataSourceConfig &config, QObject *parent, QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    QString validationError;
    if (!config.isValid(&validationError)) {
        setError(errorMessage, validationError);
        return nullptr;
    }

    switch (config.type) {
    case DataSourceType::Simulation:
        return createSimulationSource(config, parent);
    case DataSourceType::Tcp:
        return createTcpSource(config, parent);
    case DataSourceType::Invalid:
        setError(errorMessage, QStringLiteral("数据源类型无效"));
        return nullptr;
    }

    setError(errorMessage, QStringLiteral("不支持的数据源类型"));
    return nullptr;
}