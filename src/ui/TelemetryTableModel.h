#pragma once

#include "core/TelemetryRecord.h"

#include <QAbstractTableModel>
#include <QList>

class TelemetryTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        DeviceId = 0,
        Name,
        Status,
        Temperature,
        Pressure,
        Speed,
        Voltage,
        UpdatedAt,
        ColumnCount
    };

    explicit TelemetryTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void upsertRecord(const TelemetryRecord &record);
    int recordCount() const;

private:
    int findRow(const QString &deviceId) const;

    QList<TelemetryRecord> m_records;
};
