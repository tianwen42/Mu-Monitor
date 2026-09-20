#include "utils/TimeUtils.h"

namespace TimeUtils {

QString toUtcIso8601(const QDateTime &value)
{
    const QDateTime timestamp = value.isValid() ? value : QDateTime::currentDateTimeUtc();
    return timestamp.toUTC().toString(Qt::ISODateWithMs);
}

QString toLocalIso8601(const QDateTime &value)
{
    const QDateTime timestamp = value.isValid() ? value : QDateTime::currentDateTime();
    return timestamp.toLocalTime().toString(Qt::ISODateWithMs);
}

QDateTime fromIso8601(const QString &value)
{
    QDateTime timestamp = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!timestamp.isValid()) {
        timestamp = QDateTime::fromString(value, Qt::ISODate);
    }
    return timestamp.isValid() ? timestamp.toLocalTime() : QDateTime();
}

QString toFileTimestamp(const QDateTime &value)
{
    const QDateTime timestamp = value.isValid() ? value : QDateTime::currentDateTimeUtc();
    return timestamp.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmss.zzz'Z'"));
}

} // namespace TimeUtils
