#include "ui/TelemetryTableModel.h"

#include <QColor>

TelemetryTableModel::TelemetryTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int TelemetryTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_records.size();
}

int TelemetryTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant TelemetryTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_records.size()) {
        return {};
    }

    const TelemetryRecord &record = m_records.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case DeviceId:    return record.deviceId;
        case Name:        return record.name;
        case Status:      return record.status;
        case Temperature: return QString::number(record.temperature, 'f', 1) + QStringLiteral(" °C");
        case Pressure:    return QString::number(record.pressure, 'f', 2) + QStringLiteral(" MPa");
        case Speed:       return QString::number(record.speed, 'f', 0) + QStringLiteral(" rpm");
        case Voltage:     return QString::number(record.voltage, 'f', 1) + QStringLiteral(" V");
        case UpdatedAt:   return record.updatedAt.toString(QStringLiteral("HH:mm:ss"));
        default:          return {};
        }
    }

    if (role == Qt::TextAlignmentRole) {
        if (index.column() == Name) {
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        }
        return int(Qt::AlignCenter);
    }

    if (role == Qt::ForegroundRole) {
        if (record.status == QStringLiteral("报警")) {
            return QColor(QStringLiteral("#f87171"));
        }
        if (record.status == QStringLiteral("离线")) {
            return QColor(QStringLiteral("#94a3b8"));
        }
    }

    return {};
}

QVariant TelemetryTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }

    switch (section) {
    case DeviceId:    return QStringLiteral("设备编号");
    case Name:        return QStringLiteral("设备名称");
    case Status:      return QStringLiteral("状态");
    case Temperature: return QStringLiteral("温度");
    case Pressure:    return QStringLiteral("压力");
    case Speed:       return QStringLiteral("转速");
    case Voltage:     return QStringLiteral("电压");
    case UpdatedAt:   return QStringLiteral("更新时间");
    default:          return {};
    }
}

void TelemetryTableModel::upsertRecord(const TelemetryRecord &record)
{
    const int row = findRow(record.deviceId);
    if (row >= 0) {
        m_records[row] = record;
        emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
        return;
    }

    beginInsertRows(QModelIndex(), m_records.size(), m_records.size());
    m_records.append(record);
    endInsertRows();
}

int TelemetryTableModel::recordCount() const
{
    return m_records.size();
}

int TelemetryTableModel::findRow(const QString &deviceId) const
{
    for (int i = 0; i < m_records.size(); ++i) {
        if (m_records.at(i).deviceId == deviceId) {
            return i;
        }
    }
    return -1;
}
