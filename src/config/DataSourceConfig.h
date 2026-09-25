#pragma once

#include <QString>
#include <QtGlobal>

class QSettings;

enum class DataSourceType {
    Invalid,
    Simulation,
    Tcp,
};

QString dataSourceTypeToString(DataSourceType type);
DataSourceType dataSourceTypeFromString(const QString &value, bool *ok = nullptr);

struct DataSourceConfig {
    DataSourceType type = DataSourceType::Simulation;
    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 45454;
    int samplingIntervalMs = 1000;
    int heartbeatIntervalMs = 3000;
    bool reconnectEnabled = true;
    int reconnectDelayMs = 1000;
    int reconnectMaxDelayMs = 30000;
    int connectTimeoutMs = 5000;
    int readTimeoutMs = 15000;

    bool isValid(QString *errorMessage = nullptr) const;
};

struct ApplicationConfig {
    DataSourceConfig dataSource;

    static ApplicationConfig fromSettings(const QSettings &settings);
    void save(QSettings &settings) const;
};