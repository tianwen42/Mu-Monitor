#include "config/DataSourceConfig.h"
#include "network/DeviceDataSourceFactory.h"
#include "network/IDeviceDataSource.h"
#include "network/SimulationDataSource.h"
#include "network/TcpDeviceDataSource.h"
#include "network/TcpDeviceDataSourceAdapter.h"

#include <QObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

class DeviceDataSourceFactoryTest : public QObject
{
    Q_OBJECT

private slots:
    void createsDefaultSimulationSource();
    void createsTcpSourceWithConfiguration();
    void rejectsInvalidConfiguration();
    void repeatedCreateReturnsIndependentInstances();
    void adoptsReturnedSourceIntoParent();
    void readsAndWritesUnifiedSettings();
};

void DeviceDataSourceFactoryTest::createsDefaultSimulationSource()
{
    const DataSourceConfig config;
    QString errorMessage;
    std::unique_ptr<IDeviceDataSource> source(
        DeviceDataSourceFactory::create(config, nullptr, &errorMessage));

    QVERIFY2(source != nullptr, qPrintable(errorMessage));
    QVERIFY(errorMessage.isEmpty());

    auto *simulation = dynamic_cast<SimulationDataSource *>(source.get());
    QVERIFY(simulation != nullptr);
    QCOMPARE(simulation->samplingInterval(), 1000);
    QCOMPARE(simulation->heartbeatInterval(), 3000);
}

void DeviceDataSourceFactoryTest::createsTcpSourceWithConfiguration()
{
    DataSourceConfig config;
    config.type = DataSourceType::Tcp;
    config.host = QStringLiteral("localhost");
    config.port = 45678;
    config.reconnectEnabled = false;
    config.reconnectDelayMs = 250;
    config.reconnectMaxDelayMs = 5000;
    config.connectTimeoutMs = 1500;
    config.readTimeoutMs = 2500;

    QString errorMessage;
    std::unique_ptr<IDeviceDataSource> source(
        DeviceDataSourceFactory::create(config, nullptr, &errorMessage));

    QVERIFY2(source != nullptr, qPrintable(errorMessage));
    auto *adapter = dynamic_cast<TcpDeviceDataSourceAdapter *>(source.get());
    QVERIFY(adapter != nullptr);
    QCOMPARE(adapter->host(), QStringLiteral("localhost"));
    QCOMPARE(adapter->port(), static_cast<quint16>(45678));

    TcpDeviceDataSource *tcpSource = adapter->tcpDeviceDataSource();
    QVERIFY(tcpSource != nullptr);
    QCOMPARE(tcpSource->isReconnectEnabled(), false);
    QCOMPARE(tcpSource->reconnectDelayMs(), 250);
    QCOMPARE(tcpSource->reconnectMaxDelayMs(), 5000);
    QCOMPARE(tcpSource->connectTimeoutMs(), 1500);
    QCOMPARE(tcpSource->readTimeoutMs(), 2500);
}

void DeviceDataSourceFactoryTest::rejectsInvalidConfiguration()
{
    DataSourceConfig invalidType;
    invalidType.type = DataSourceType::Invalid;
    QString errorMessage;
    QVERIFY(DeviceDataSourceFactory::create(invalidType, nullptr, &errorMessage) == nullptr);
    QVERIFY(!errorMessage.isEmpty());

    DataSourceConfig invalidTcp;
    invalidTcp.type = DataSourceType::Tcp;
    invalidTcp.host.clear();
    errorMessage.clear();
    QVERIFY(DeviceDataSourceFactory::create(invalidTcp, nullptr, &errorMessage) == nullptr);
    QVERIFY(errorMessage.contains(QStringLiteral("TCP")));

    DataSourceConfig invalidInterval;
    invalidInterval.samplingIntervalMs = 0;
    errorMessage.clear();
    QVERIFY(DeviceDataSourceFactory::create(invalidInterval, nullptr, &errorMessage) == nullptr);
    QVERIFY(!errorMessage.isEmpty());
}

void DeviceDataSourceFactoryTest::repeatedCreateReturnsIndependentInstances()
{
    const DataSourceConfig config;
    QString errorMessage;

    std::unique_ptr<IDeviceDataSource> first(
        DeviceDataSourceFactory::create(config, nullptr, &errorMessage));
    QVERIFY2(first != nullptr, qPrintable(errorMessage));
    std::unique_ptr<IDeviceDataSource> second(
        DeviceDataSourceFactory::create(config, nullptr, &errorMessage));
    QVERIFY2(second != nullptr, qPrintable(errorMessage));

    QVERIFY(first.get() != second.get());
    auto *firstSimulation = dynamic_cast<SimulationDataSource *>(first.get());
    auto *secondSimulation = dynamic_cast<SimulationDataSource *>(second.get());
    QVERIFY(firstSimulation != nullptr);
    QVERIFY(secondSimulation != nullptr);
    QVERIFY(firstSimulation != secondSimulation);
}

void DeviceDataSourceFactoryTest::adoptsReturnedSourceIntoParent()
{
    QObject owner;
    const DataSourceConfig config;
    QString errorMessage;

    IDeviceDataSource *first =
        DeviceDataSourceFactory::create(config, &owner, &errorMessage);
    QVERIFY2(first != nullptr, qPrintable(errorMessage));
    QCOMPARE(first->parent(), &owner);

    IDeviceDataSource *second =
        DeviceDataSourceFactory::create(config, &owner, &errorMessage);
    QVERIFY2(second != nullptr, qPrintable(errorMessage));
    QCOMPARE(second->parent(), &owner);
    QCOMPARE(owner.children().size(), 2);
    QVERIFY(first != second);
}

void DeviceDataSourceFactoryTest::readsAndWritesUnifiedSettings()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString settingsPath = tempDir.filePath(QStringLiteral("application.ini"));
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("dataSource/type"), QStringLiteral("tcp"));
    settings.setValue(QStringLiteral("dataSource/host"), QStringLiteral("10.0.0.8"));
    settings.setValue(QStringLiteral("dataSource/port"), 6001);
    settings.setValue(QStringLiteral("dataSource/samplingIntervalMs"), 750);
    settings.setValue(QStringLiteral("dataSource/heartbeatIntervalMs"), 2500);
    settings.setValue(QStringLiteral("dataSource/reconnect/enabled"), false);
    settings.setValue(QStringLiteral("dataSource/reconnect/delayMs"), 300);
    settings.setValue(QStringLiteral("dataSource/reconnect/maxDelayMs"), 9000);
    settings.setValue(QStringLiteral("dataSource/reconnect/connectTimeoutMs"), 1200);
    settings.setValue(QStringLiteral("dataSource/reconnect/readTimeoutMs"), 2200);
    settings.sync();

    const ApplicationConfig loaded = ApplicationConfig::fromSettings(settings);
    QCOMPARE(static_cast<int>(loaded.dataSource.type),
             static_cast<int>(DataSourceType::Tcp));
    QCOMPARE(loaded.dataSource.host, QStringLiteral("10.0.0.8"));
    QCOMPARE(loaded.dataSource.port, static_cast<quint16>(6001));
    QCOMPARE(loaded.dataSource.samplingIntervalMs, 750);
    QCOMPARE(loaded.dataSource.heartbeatIntervalMs, 2500);
    QCOMPARE(loaded.dataSource.reconnectEnabled, false);
    QCOMPARE(loaded.dataSource.reconnectDelayMs, 300);
    QCOMPARE(loaded.dataSource.reconnectMaxDelayMs, 9000);
    QCOMPARE(loaded.dataSource.connectTimeoutMs, 1200);
    QCOMPARE(loaded.dataSource.readTimeoutMs, 2200);

    ApplicationConfig saved;
    saved.dataSource.type = DataSourceType::Simulation;
    saved.dataSource.samplingIntervalMs = 1250;
    saved.save(settings);

    const ApplicationConfig roundTrip = ApplicationConfig::fromSettings(settings);
    QCOMPARE(static_cast<int>(roundTrip.dataSource.type),
             static_cast<int>(DataSourceType::Simulation));
    QCOMPARE(roundTrip.dataSource.samplingIntervalMs, 1250);
}

QTEST_MAIN(DeviceDataSourceFactoryTest)

#include "DeviceDataSourceFactoryTest.moc"