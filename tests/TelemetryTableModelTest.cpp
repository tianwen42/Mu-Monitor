#include "ui/TelemetryTableModel.h"

#include <QColor>
#include <QSignalSpy>
#include <QTimeZone>
#include <QtTest>

class TelemetryTableModelTest : public QObject
{
    Q_OBJECT

private slots:
    void insertsAndReplacesByDeviceId();
    void exposesFormattedColumns();
    void reportsStatusColors();
    void looksUpRecordByDeviceId();
    void retainsOnlyCurrentDevices();
};

namespace {
TelemetryRecord makeRecord(const QString &deviceId, double temperature)
{
    TelemetryRecord record;
    record.deviceId = deviceId;
    record.name = QStringLiteral("测试设备");
    record.status = TelemetryStatus::Online;
    record.temperature = temperature;
    record.pressure = 1.25;
    record.speed = 1500.0;
    record.voltage = 220.0;
    record.updatedAt = QDateTime(QDate(2026, 9, 20), QTime(12, 0), QTimeZone::UTC);
    return record;
}
}

void TelemetryTableModelTest::insertsAndReplacesByDeviceId()
{
    TelemetryTableModel model;
    QSignalSpy rowsInserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy dataChanged(&model, &QAbstractItemModel::dataChanged);

    model.upsertRecord(makeRecord(QStringLiteral("DEV-001"), 61.0));
    QCOMPARE(model.recordCount(), 1);
    QCOMPARE(rowsInserted.count(), 1);

    model.upsertRecord(makeRecord(QStringLiteral("DEV-001"), 72.5));
    QCOMPARE(model.recordCount(), 1);
    QCOMPARE(dataChanged.count(), 1);
    QCOMPARE(model.index(0, TelemetryTableModel::Temperature).data().toString(),
             QStringLiteral("72.5 °C"));

    model.upsertRecord(makeRecord(QStringLiteral("DEV-002"), 63.0));
    QCOMPARE(model.recordCount(), 2);
    QCOMPARE(rowsInserted.count(), 2);
}

void TelemetryTableModelTest::exposesFormattedColumns()
{
    TelemetryTableModel model;
    model.upsertRecord(makeRecord(QStringLiteral("DEV-001"), 61.25));

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.columnCount(), int(TelemetryTableModel::ColumnCount));
    QCOMPARE(model.headerData(TelemetryTableModel::DeviceId, Qt::Horizontal).toString(),
             QStringLiteral("设备编号"));
    QCOMPARE(model.index(0, TelemetryTableModel::Temperature).data().toString(),
             QStringLiteral("61.3 °C"));
    QCOMPARE(model.index(0, TelemetryTableModel::Pressure).data().toString(),
             QStringLiteral("1.25 MPa"));
    QVERIFY(!model.index(5, 0).isValid());
}

void TelemetryTableModelTest::retainsOnlyCurrentDevices()
{
    TelemetryTableModel model;
    model.upsertRecord(makeRecord(QStringLiteral("DEV-001"), 61.0));
    model.upsertRecord(makeRecord(QStringLiteral("DEV-002"), 62.0));
    model.upsertRecord(makeRecord(QStringLiteral("DEV-005"), 65.0));

    model.retainDevices({QStringLiteral("DEV-001"), QStringLiteral("DEV-002")});
    QCOMPARE(model.recordCount(), 2);
    QVERIFY(model.recordForDevice(QStringLiteral("DEV-001"), nullptr));
    QVERIFY(model.recordForDevice(QStringLiteral("DEV-002"), nullptr));
    QVERIFY(!model.recordForDevice(QStringLiteral("DEV-005"), nullptr));

    model.retainDevices({});
    QCOMPARE(model.recordCount(), 0);
}
void TelemetryTableModelTest::looksUpRecordByDeviceId()
{
    TelemetryTableModel model;
    model.upsertRecord(makeRecord(QStringLiteral("DEV-001"), 64.5));

    TelemetryRecord record;
    QVERIFY(model.recordForDevice(QStringLiteral("DEV-001"), &record));
    QCOMPARE(record.deviceId, QStringLiteral("DEV-001"));
    QCOMPARE(record.temperature, 64.5);
    QVERIFY(!model.recordForDevice(QStringLiteral("DEV-999"), &record));
}
void TelemetryTableModelTest::reportsStatusColors()
{
    TelemetryTableModel model;
    TelemetryRecord record = makeRecord(QStringLiteral("DEV-001"), 61.0);
    model.upsertRecord(record);

    QCOMPARE(model.index(0, TelemetryTableModel::Status).data(Qt::ForegroundRole).value<QColor>(),
             QColor(QStringLiteral("#2e7d32")));

    record.status = TelemetryStatus::Alarm;
    model.upsertRecord(record);
    QCOMPARE(model.index(0, TelemetryTableModel::Status).data(Qt::ForegroundRole).value<QColor>(),
             QColor(QStringLiteral("#b3261e")));
}

QTEST_MAIN(TelemetryTableModelTest)

#include "TelemetryTableModelTest.moc"
