#pragma once

#include "core/TelemetryRecord.h"

#include <QList>
#include <QString>

class ExcelExporter
{
public:
    static bool exportRecords(const QString &filePath,
                              const QList<TelemetryRecord> &records,
                              QString *errorMessage = nullptr);
};
