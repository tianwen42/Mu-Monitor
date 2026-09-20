#include "utils/ExcelExporter.h"

#include "utils/TimeUtils.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QVariant>
#include <xlsxdocument.h>
#include <xlsxformat.h>

namespace {
QXlsx::Format headerFormat()
{
    QXlsx::Format format;
    format.setFontBold(true);
    format.setFontSize(11);
    format.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    format.setPatternBackgroundColor(QColor(QStringLiteral("#DCE9F7")));
    format.setBorderStyle(QXlsx::Format::BorderThin);
    return format;
}

QXlsx::Format dataFormat()
{
    QXlsx::Format format;
    format.setBorderStyle(QXlsx::Format::BorderThin);
    return format;
}
}

bool ExcelExporter::exportRecords(const QString &filePath,
                                  const QList<TelemetryRecord> &records,
                                  QString *errorMessage)
{
    if (filePath.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("导出路径为空");
        return false;
    }

    QFileInfo fileInfo(filePath);
    const QDir directory = fileInfo.absoluteDir();
    if (!directory.exists() && !QDir().mkpath(directory.absolutePath())) {
        if (errorMessage) *errorMessage = QStringLiteral("无法创建导出目录：%1").arg(directory.absolutePath());
        return false;
    }

    QXlsx::Document workbook;
    workbook.renameSheet(QStringLiteral("Sheet1"), QStringLiteral("设备数据"));

    const QStringList headers = {
        QStringLiteral("设备编号"),
        QStringLiteral("设备名称"),
        QStringLiteral("状态"),
        QStringLiteral("温度(℃)"),
        QStringLiteral("压力(MPa)"),
        QStringLiteral("转速(rpm)"),
        QStringLiteral("电压(V)"),
        QStringLiteral("记录时间 (ISO 8601)"),
    };

    const QXlsx::Format header = headerFormat();
    const QXlsx::Format data = dataFormat();

    for (int column = 0; column < headers.size(); ++column) {
        workbook.write(1, column + 1, headers.at(column), header);
    }

    for (int row = 0; row < records.size(); ++row) {
        const TelemetryRecord &record = records.at(row);
        const int excelRow = row + 2;

        workbook.write(excelRow, 1, record.deviceId, data);
        workbook.write(excelRow, 2, record.name, data);
        workbook.write(excelRow, 3, telemetryStatusDisplayName(record.status), data);
        workbook.write(excelRow, 4, record.temperature, data);
        workbook.write(excelRow, 5, record.pressure, data);
        workbook.write(excelRow, 6, record.speed, data);
        workbook.write(excelRow, 7, record.voltage, data);
        workbook.write(excelRow, 8, TimeUtils::toLocalIso8601(record.updatedAt), data);
    }

    const QList<double> widths = {14, 22, 10, 13, 13, 13, 12, 30};
    for (int i = 0; i < widths.size(); ++i) {
        workbook.setColumnWidth(i + 1, widths.at(i));
    }

    if (!workbook.saveAs(filePath)) {
        if (errorMessage) *errorMessage = QStringLiteral("保存 Excel 文件失败：%1").arg(filePath);
        return false;
    }

    return true;
}
