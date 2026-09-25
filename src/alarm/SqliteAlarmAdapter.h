#pragma once

#include "alarm/AlarmEvent.h"

#include <QList>
#include <QString>

#include <optional>

// 预留的 SQLite 持久化边界。后续实现可以放在 database 层，
// 由 IAlarmRepository 的实现调用，而不需要让 AlarmEngine 依赖数据库。
class IAlarmSqliteAdapter
{
public:
    virtual ~IAlarmSqliteAdapter() = default;

    virtual bool upsertEvent(
        const AlarmEvent &event, QString *errorMessage = nullptr) = 0;
    virtual std::optional<AlarmEvent> loadLatestEvent(
        const QString &alarmKey, QString *errorMessage = nullptr) const = 0;
    virtual QList<AlarmEvent> loadAllEvents(
        QString *errorMessage = nullptr) const = 0;
    virtual QList<AlarmEvent> loadActiveEvents(
        QString *errorMessage = nullptr) const = 0;
};
