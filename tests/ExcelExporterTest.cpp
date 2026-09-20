#include "utils/ExcelExporter.h"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>
#include <xlsxdocument.h>

class ExcelExporterTest : public QObject
{
    Q_OBJECT

private slots:
    void exportsHeadersAndRecords();
    void rejectsEmptyPath();
    void exportsEmptyWorkbookWithHeaders();
};

void ExcelExporterTest::exportsHeadersAndRecords()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    TelemetryRecord record;
    record.deviceId = QStringLiteral("DEV-001");
    record.name = QStringLiteral("一号测试设备");
    record.status = TelemetryStatus::Alarm;
    record.temperature = 86.5;
    record.pressure = 2.08;
    record.speed = 1888.0;
    record.voltage = 219.8;
    record.updatedAt = QDateTime(QDate(2026, 9, 20), QTime(8, 30, 15, 123), QTimeZone::UTC);

    const QString path = directory.filePath(QStringLiteral("nested/telemetry.xlsx"));
    QString errorMessage;
    QVERIFY2(ExcelExporter::exportRecords(path, {record}, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY(QFileInfo::exists(path));
    QVERIFY(QFileInfo(path).size() > 0);

    QXlsx::Document workbook(path);
    QVERIFY(workbook.load());
    QCOMPARE(workbook.read(1, 1).toString(), QStringLiteral("设备编号"));
    QCOMPARE(workbook.read(1, 8).toString(), QStringLiteral("记录时间 (ISO 8601)"));
    QCOMPARE(workbook.read(2, 1).toString(), QStringLiteral("DEV-001"));
    QCOMPARE(workbook.read(2, 2).toString(), QStringLiteral("一号测试设备"));
    QCOMPARE(workbook.read(2, 3).toString(), QStringLiteral("报警"));
    QCOMPARE(workbook.read(2, 4).toDouble(), 86.5);
    QCOMPARE(workbook.read(2, 5).toDouble(), 2.08);
    QVERIFY(workbook.read(2, 8).toString().contains(QStringLiteral("2026-09-20T")));
}

void ExcelExporterTest::rejectsEmptyPath()
{
    QString errorMessage;
    QVERIFY(!ExcelExporter::exportRecords(QString(), {}, &errorMessage));
    QVERIFY(!errorMessage.isEmpty());
}

void ExcelExporterTest::exportsEmptyWorkbookWithHeaders()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString path = directory.filePath(QStringLiteral("empty.xlsx"));
    QString errorMessage;
    QVERIFY2(ExcelExporter::exportRecords(path, {}, &errorMessage),
             qPrintable(errorMessage));

    QXlsx::Document workbook(path);
    QVERIFY(workbook.load());
    QCOMPARE(workbook.read(1, 1).toString(), QStringLiteral("设备编号"));
    QVERIFY(workbook.read(2, 1).toString().isEmpty());
}

QTEST_MAIN(ExcelExporterTest)

#include "ExcelExporterTest.moc"
