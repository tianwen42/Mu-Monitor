#pragma once

#include "config/DataSourceConfig.h"

class IDeviceDataSource;
class QObject;
class QString;

class DeviceDataSourceFactory final
{
public:
    /**
     * Creates a data source from the supplied configuration.
     *
     * Ownership rule:
     * - When parent is not null, the returned object is a child of parent.
     * - When parent is null, the caller owns the returned object and must delete
     *   it or adopt it into a QObject parent.
     *
     * The factory does not cache instances and does not retain ownership.
     */
    static IDeviceDataSource *create(const DataSourceConfig &config,
                                     QObject *parent,
                                     QString *errorMessage = nullptr);

private:
    DeviceDataSourceFactory() = delete;
};