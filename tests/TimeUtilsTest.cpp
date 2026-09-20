#include "utils/TimeUtils.h"

#include <QTimeZone>
#include <QtTest>

class TimeUtilsTest : public QObject
{
    Q_OBJECT

private slots:
    void formatsUtcWithMilliseconds();
    void parsesIso8601ToSameInstant();
    void formatsFileTimestamp();
    void rejectsInvalidTimestamp();
};

void TimeUtilsTest::formatsUtcWithMilliseconds()
{
    const QDateTime value(QDate(2026, 9, 20), QTime(12, 34, 56, 789), QTimeZone::UTC);
    QCOMPARE(TimeUtils::toUtcIso8601(value),
             QStringLiteral("2026-09-20T12:34:56.789Z"));
}

void TimeUtilsTest::parsesIso8601ToSameInstant()
{
    const QDateTime expected(QDate(2026, 9, 20), QTime(12, 34, 56, 789), QTimeZone::UTC);
    const QDateTime parsed = TimeUtils::fromIso8601(QStringLiteral("2026-09-20T12:34:56.789Z"));
    QVERIFY(parsed.isValid());
    QCOMPARE(parsed.toUTC(), expected);

    const QDateTime withoutMilliseconds =
        TimeUtils::fromIso8601(QStringLiteral("2026-09-20T12:34:56Z"));
    QVERIFY(withoutMilliseconds.isValid());
    QCOMPARE(withoutMilliseconds.toUTC(), expected.addMSecs(-789));
}

void TimeUtilsTest::formatsFileTimestamp()
{
    const QDateTime value(QDate(2026, 9, 20), QTime(12, 34, 56, 789), QTimeZone::UTC);
    QCOMPARE(TimeUtils::toFileTimestamp(value),
             QStringLiteral("20260920T123456.789Z"));
}

void TimeUtilsTest::rejectsInvalidTimestamp()
{
    QVERIFY(!TimeUtils::fromIso8601(QStringLiteral("not-a-timestamp")).isValid());
    QVERIFY(!TimeUtils::fromIso8601(QString()).isValid());
}

QTEST_MAIN(TimeUtilsTest)

#include "TimeUtilsTest.moc"
