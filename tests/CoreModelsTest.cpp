#include "core/BusinessStates.h"
#include "core/Device.h"
#include "core/TelemetrySample.h"

#include <QTimeZone>
#include <QtTest>

#include <limits>

class CoreModelsTest : public QObject
{
    Q_OBJECT

private slots:
    void validatesDevice();
    void validatesTelemetrySample();
    void validatesStateTransitions();
    void namesBusinessStates();
    void mapsTelemetryStatus();
};

void CoreModelsTest::validatesDevice()
{
    Device device;
    device.id = QStringLiteral("DEV-001");
    device.name = QStringLiteral("一号设备");
    device.address = QStringLiteral("127.0.0.1");
    device.port = 45454;
    device.protocol = ProtocolType::Tcp;
    device.enabled = true;

    QString errorMessage;
    QVERIFY2(device.isValid(&errorMessage), qPrintable(errorMessage));
    QVERIFY(isValidDeviceId(QStringLiteral("device.01")));
    QVERIFY(!isValidDeviceId(QStringLiteral("-invalid")));
    QVERIFY(!isValidDeviceId(QString(65, QLatin1Char('A'))));

    device.name.clear();
    QVERIFY(!device.isValid(&errorMessage));
    QVERIFY(!errorMessage.isEmpty());

    device.name = QStringLiteral("一号设备");
    device.port = 0;
    QVERIFY(!device.isValid(&errorMessage));

    device.port = 45454;
    device.protocol = ProtocolType::Unknown;
    QVERIFY(!device.isValid(&errorMessage));
}

void CoreModelsTest::validatesTelemetrySample()
{
    TelemetrySample sample;
    sample.deviceId = QStringLiteral("DEV-001");
    sample.collectedAt = QDateTime(QDate(2026, 9, 20), QTime(12, 0), QTimeZone::UTC);
    sample.measurements = {
        {MeasurementType::Temperature, 62.5, QualityCode::Good},
        {MeasurementType::Pressure, 1.25, QualityCode::Good},
    };

    QString errorMessage;
    QVERIFY2(sample.isValid(&errorMessage), qPrintable(errorMessage));
    QVERIFY(sample.measurement(MeasurementType::Temperature) != nullptr);
    QCOMPARE(sample.measurement(MeasurementType::Temperature)->value, 62.5);
    QCOMPARE(sample.measurement(MeasurementType::Speed), nullptr);

    sample.measurements.append(
        {MeasurementType::Temperature, 63.0, QualityCode::Good});
    QVERIFY(!sample.isValid(&errorMessage));

    sample.measurements = {
        {MeasurementType::Voltage, std::numeric_limits<double>::quiet_NaN(), QualityCode::Good},
    };
    QVERIFY(!sample.isValid(&errorMessage));

    sample.measurements = {};
    QVERIFY(!sample.isValid(&errorMessage));

    sample.measurements = {
        {MeasurementType::Temperature, 62.5, QualityCode::Good},
    };
    sample.collectedAt = QDateTime::currentDateTime();
    QVERIFY(!sample.isValid(&errorMessage));
}

void CoreModelsTest::validatesStateTransitions()
{
    QVERIFY(isConnectionTransitionAllowed(
        ConnectionState::Disconnected, ConnectionState::Connecting));
    QVERIFY(isConnectionTransitionAllowed(
        ConnectionState::Connected, ConnectionState::Reconnecting));
    QVERIFY(isConnectionTransitionAllowed(
        ConnectionState::Stopping, ConnectionState::Disconnected));
    QVERIFY(!isConnectionTransitionAllowed(
        ConnectionState::Disconnected, ConnectionState::Connected));
    QVERIFY(!isConnectionTransitionAllowed(
        ConnectionState::Connected, ConnectionState::Connected));

    QVERIFY(isCollectionTransitionAllowed(
        CollectionState::Stopped, CollectionState::Running));
    QVERIFY(isCollectionTransitionAllowed(
        CollectionState::Running, CollectionState::Paused));
    QVERIFY(isCollectionTransitionAllowed(
        CollectionState::Faulted, CollectionState::Stopped));
    QVERIFY(!isCollectionTransitionAllowed(
        CollectionState::Stopped, CollectionState::Paused));
    QVERIFY(!isCollectionTransitionAllowed(
        CollectionState::Running, CollectionState::Running));
}

void CoreModelsTest::namesBusinessStates()
{
    QCOMPARE(connectionStateName(ConnectionState::Connected), QStringLiteral("Connected"));
    QCOMPARE(collectionStateName(CollectionState::Paused), QStringLiteral("Paused"));
    QCOMPARE(alarmSeverityName(AlarmSeverity::Critical), QStringLiteral("Critical"));
    QCOMPARE(protocolTypeName(ProtocolType::ModbusTcp), QStringLiteral("Modbus TCP"));
    QCOMPARE(measurementTypeName(MeasurementType::Temperature), QStringLiteral("Temperature"));
    QCOMPARE(qualityCodeName(QualityCode::Uncertain), QStringLiteral("Uncertain"));
    QVERIFY(isValidAlarmSeverity(AlarmSeverity::Warning));
}

void CoreModelsTest::mapsTelemetryStatus()
{
    QCOMPARE(telemetryStatusCode(TelemetryStatus::Offline), QStringLiteral("offline"));
    QCOMPARE(telemetryStatusCode(TelemetryStatus::Online), QStringLiteral("online"));
    QCOMPARE(telemetryStatusCode(TelemetryStatus::Stopped), QStringLiteral("stopped"));
    QCOMPARE(telemetryStatusCode(TelemetryStatus::Alarm), QStringLiteral("alarm"));

    QCOMPARE(telemetryStatusDisplayName(TelemetryStatus::Offline), QStringLiteral("离线"));
    QCOMPARE(telemetryStatusDisplayName(TelemetryStatus::Online), QStringLiteral("在线"));
    QCOMPARE(telemetryStatusDisplayName(TelemetryStatus::Stopped), QStringLiteral("已停止"));
    QCOMPARE(telemetryStatusDisplayName(TelemetryStatus::Alarm), QStringLiteral("报警"));

    bool ok = false;
    QCOMPARE(telemetryStatusFromString(QStringLiteral("online"), &ok), TelemetryStatus::Online);
    QVERIFY(ok);
    QCOMPARE(telemetryStatusFromString(QStringLiteral("离线"), &ok), TelemetryStatus::Offline);
    QVERIFY(ok);
    QCOMPARE(telemetryStatusFromString(QStringLiteral("已停止"), &ok), TelemetryStatus::Stopped);
    QVERIFY(ok);
    QCOMPARE(telemetryStatusFromString(QStringLiteral("报警"), &ok), TelemetryStatus::Alarm);
    QVERIFY(ok);

    QCOMPARE(telemetryStatusFromString(QStringLiteral("未知状态"), &ok), TelemetryStatus::Offline);
    QVERIFY(!ok);
}
QTEST_MAIN(CoreModelsTest)

#include "CoreModelsTest.moc"
