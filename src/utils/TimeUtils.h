#pragma once

#include <QDateTime>
#include <QString>

namespace TimeUtils {

QString toUtcIso8601(const QDateTime &value = QDateTime::currentDateTimeUtc());
QString toLocalIso8601(const QDateTime &value = QDateTime::currentDateTime());
QDateTime fromIso8601(const QString &value);
QString toFileTimestamp(const QDateTime &value = QDateTime::currentDateTimeUtc());

} // namespace TimeUtils
