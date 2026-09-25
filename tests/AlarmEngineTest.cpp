#include "alarm/AlarmEngine.h"
#include "alarm/InMemoryAlarmRepository.h"

#include <QDateTime>
#include <QObject>
#include <QTest>

#include <chrono>

namespace {
QDateTime utc(const QString &value)
{
    return QDateTime::fromString(value, Qt::ISODate).toUTC();
}

AlarmRule highTemperatureRule(double threshold, double hysteresis = 0.0,
                              std::chrono::milliseconds delay = std::chrono::milliseconds{0})
{
    AlarmRule rule;
    rule.ruleId = QStringLiteral("temperature-high");
    rule.deviceId = QStringLiteral("DEV-001");
    rule.type = AlarmRuleType::HighThreshold;
    rule.measurement = MeasurementType::Temperature;
    rule.threshold = threshold;
    rule.hysteresis = hysteresis;
    rule.activationDelay = delay;
    rule.severity = AlarmSeverity::Critical;
    return rule;
}

AlarmRule offlineRule(std::chrono::milliseconds timeout)
{
    AlarmRule rule;
    rule.ruleId = QStringLiteral("device-offline");
    rule.deviceId = QStringLiteral("DEV-001");
    rule.type = AlarmRuleType::Offline;
    rule.offlineTimeout = timeout;
    rule.severity = AlarmSeverity::Warning;
    return rule;
}

TelemetrySample temperatureSample(const QString &deviceId, double value,
                                  const QDateTime &at)
{
    TelemetrySample sample;
    sample.deviceId = deviceId;
    sample.collectedAt = at.toUTC();
    MeasurementValue measurement;
    measurement.type = MeasurementType::Temperature;
    measurement.value = value;
    measurement.quality = QualityCode::Good;
    sample.measurements.append(measurement);
    return sample;
}
}

class AlarmEngineTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<AlarmEvent>("AlarmEvent");
        qRegisterMetaType<AlarmState>("AlarmState");
    }

    void thresholdTrigger()
    {
        QDateTime now = utc(QStringLiteral("2026-09-25T10:00:00Z"));
        AlarmEngine engine;
        engine.setClock([&now]() { return now; });
        QVERIFY(engine.addRule(highTemperatureRule(80.0)));

        int raised = 0;
        QObject::connect(&engine, &AlarmEngine::alarmRaised,
                         [&raised](const AlarmEvent &) { ++raised; });

        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 79.9, now));
        QCOMPARE(raised, 0);
        QCOMPARE(static_cast<int>(engine.stateFor(
                     alarmKeyFor(QStringLiteral("DEV-001"),
                                 QStringLiteral("temperature-high")))),
                 static_cast<int>(AlarmState::Normal));

        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 80.1, now));
        QCOMPARE(raised, 1);
        QCOMPARE(engine.activeEvents().size(), 1);
        const AlarmEvent event = engine.activeEvents().constFirst();
        QCOMPARE(event.deviceId, QStringLiteral("DEV-001"));
        QCOMPARE(event.ruleId, QStringLiteral("temperature-high"));
        QCOMPARE(event.threshold, 80.0);
        QCOMPARE(event.observedValue, 80.1);
        QCOMPARE(static_cast<int>(event.severity), static_cast<int>(AlarmSeverity::Critical));
        QCOMPARE(static_cast<int>(event.state), static_cast<int>(AlarmState::Active));
    }

    void durationFilter()
    {
        QDateTime now = utc(QStringLiteral("2026-09-25T10:00:00Z"));
        AlarmEngine engine;
        engine.setClock([&now]() { return now; });
        QVERIFY(engine.addRule(highTemperatureRule(
            80.0, 0.0, std::chrono::milliseconds{5000})));

        int raised = 0;
        QObject::connect(&engine, &AlarmEngine::alarmRaised,
                         [&raised](const AlarmEvent &) { ++raised; });

        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 85.0, now));
        QCOMPARE(raised, 0);

        now = now.addMSecs(4999);
        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 85.0, now));
        QCOMPARE(raised, 0);

        now = now.addMSecs(1);
        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 85.0, now));
        QCOMPARE(raised, 1);

        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 70.0, now));
        QCOMPARE(engine.activeEvents().size(), 0);

        now = now.addMSecs(1000);
        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 85.0, now));
        now = now.addMSecs(1000);
        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 70.0, now));
        now = now.addMSecs(1000);
        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 85.0, now));
        now = now.addMSecs(4999);
        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 85.0, now));
        QCOMPARE(raised, 1);
        now = now.addMSecs(1);
        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 85.0, now));
        QCOMPARE(raised, 2);
    }

    void hysteresis()
    {
        QDateTime now = utc(QStringLiteral("2026-09-25T10:00:00Z"));
        AlarmEngine engine;
        engine.setClock([&now]() { return now; });
        QVERIFY(engine.addRule(highTemperatureRule(80.0, 5.0)));

        int cleared = 0;
        QObject::connect(&engine, &AlarmEngine::alarmCleared,
                         [&cleared](const AlarmEvent &) { ++cleared; });

        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 85.0, now));
        QCOMPARE(engine.activeEvents().size(), 1);

        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 76.0, now));
        QCOMPARE(cleared, 0);
        QCOMPARE(engine.activeEvents().size(), 1);

        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 75.0, now));
        QCOMPARE(cleared, 1);
        QCOMPARE(engine.activeEvents().size(), 0);
        QCOMPARE(static_cast<int>(engine.lastEvent(
                     alarmKeyFor(QStringLiteral("DEV-001"),
                                 QStringLiteral("temperature-high")))->state),
                 static_cast<int>(AlarmState::Cleared));
    }

    void duplicateSuppression()
    {
        QDateTime now = utc(QStringLiteral("2026-09-25T10:00:00Z"));
        AlarmEngine engine;
        engine.setClock([&now]() { return now; });
        QVERIFY(engine.addRule(highTemperatureRule(80.0)));

        int raised = 0;
        QObject::connect(&engine, &AlarmEngine::alarmRaised,
                         [&raised](const AlarmEvent &) { ++raised; });

        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 85.0, now));
        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 86.0, now));
        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 87.0, now));

        QCOMPARE(raised, 1);
        QCOMPARE(engine.activeEvents().size(), 1);
        QCOMPARE(engine.activeEvents().constFirst().observedValue, 87.0);
    }

    void acknowledgement()
    {
        QDateTime now = utc(QStringLiteral("2026-09-25T10:00:00Z"));
        AlarmEngine engine;
        engine.setClock([&now]() { return now; });
        QVERIFY(engine.addRule(highTemperatureRule(80.0)));

        int acknowledged = 0;
        QObject::connect(&engine, &AlarmEngine::alarmAcknowledged,
                         [&acknowledged](const AlarmEvent &) { ++acknowledged; });

        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 85.0, now));
        const AlarmEvent active = engine.activeEvents().constFirst();

        now = now.addMSecs(1000);
        QVERIFY(engine.acknowledge(active.eventId, QStringLiteral("operator"), now));
        QCOMPARE(acknowledged, 1);
        QCOMPARE(static_cast<int>(engine.activeEvents().constFirst().state),
                 static_cast<int>(AlarmState::Acknowledged));
        QCOMPARE(engine.activeEvents().constFirst().acknowledgedBy,
                 QStringLiteral("operator"));
        QVERIFY(!engine.acknowledge(active.eventId, QStringLiteral("operator"), now));
    }

    void recovery()
    {
        QDateTime now = utc(QStringLiteral("2026-09-25T10:00:00Z"));
        AlarmEngine engine;
        engine.setClock([&now]() { return now; });
        QVERIFY(engine.addRule(highTemperatureRule(80.0, 2.0)));

        QList<QPair<AlarmState, AlarmState>> transitions;
        QObject::connect(&engine, &AlarmEngine::alarmStateChanged,
                         [&transitions](const AlarmEvent &, AlarmState previous,
                                        AlarmState current) {
                             transitions.append(qMakePair(previous, current));
                         });

        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 85.0, now));
        const AlarmEvent active = engine.activeEvents().constFirst();
        now = now.addMSecs(1000);
        QVERIFY(engine.acknowledge(active.eventId, QStringLiteral("admin"), now));
        now = now.addMSecs(1000);
        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 78.0, now));

        QCOMPARE(transitions.size(), 4);
        QCOMPARE(static_cast<int>(transitions.at(0).first), static_cast<int>(AlarmState::Normal));
        QCOMPARE(static_cast<int>(transitions.at(0).second), static_cast<int>(AlarmState::Active));
        QCOMPARE(static_cast<int>(transitions.at(1).first), static_cast<int>(AlarmState::Active));
        QCOMPARE(static_cast<int>(transitions.at(1).second), static_cast<int>(AlarmState::Acknowledged));
        QCOMPARE(static_cast<int>(transitions.at(2).first), static_cast<int>(AlarmState::Acknowledged));
        QCOMPARE(static_cast<int>(transitions.at(2).second), static_cast<int>(AlarmState::Cleared));
        QCOMPARE(static_cast<int>(transitions.at(3).first), static_cast<int>(AlarmState::Cleared));
        QCOMPARE(static_cast<int>(transitions.at(3).second), static_cast<int>(AlarmState::Normal));
        QCOMPARE(engine.activeEvents().size(), 0);
    }

    void offlineAlarm()
    {
        QDateTime now = utc(QStringLiteral("2026-09-25T10:00:00Z"));
        AlarmEngine engine;
        engine.setClock([&now]() { return now; });
        QVERIFY(engine.addRule(offlineRule(std::chrono::milliseconds{3000})));

        int raised = 0;
        int cleared = 0;
        QObject::connect(&engine, &AlarmEngine::alarmRaised,
                         [&raised](const AlarmEvent &) { ++raised; });
        QObject::connect(&engine, &AlarmEngine::alarmCleared,
                         [&cleared](const AlarmEvent &) { ++cleared; });

        const QString key = alarmKeyFor(QStringLiteral("DEV-001"),
                                        QStringLiteral("device-offline"));
        engine.processHeartbeat(QStringLiteral("DEV-001"), true, now);
        now = now.addMSecs(2999);
        engine.updateOfflineStates(now);
        QCOMPARE(raised, 0);

        now = now.addMSecs(1);
        engine.updateOfflineStates(now);
        QCOMPARE(raised, 1);
        QCOMPARE(static_cast<int>(engine.stateFor(key)), static_cast<int>(AlarmState::Active));

        now = now.addMSecs(1000);
        engine.processHeartbeat(QStringLiteral("DEV-001"), true, now);
        QCOMPARE(cleared, 1);
        QCOMPARE(static_cast<int>(engine.stateFor(key)), static_cast<int>(AlarmState::Normal));

        engine.processHeartbeat(QStringLiteral("DEV-001"), false, now);
        QCOMPARE(raised, 2);
        QCOMPARE(static_cast<int>(engine.stateFor(key)), static_cast<int>(AlarmState::Active));
    }

    void lastStateAndRestore()
    {
        QDateTime now = utc(QStringLiteral("2026-09-25T10:00:00Z"));
        InMemoryAlarmRepository repository;
        AlarmEngine engine(&repository);
        engine.setClock([&now]() { return now; });
        QVERIFY(engine.addRule(highTemperatureRule(80.0)));

        const QString key = alarmKeyFor(QStringLiteral("DEV-001"),
                                        QStringLiteral("temperature-high"));
        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 85.0, now));
        const AlarmEvent active = engine.activeEvents().constFirst();
        QVERIFY(engine.acknowledge(active.eventId, QStringLiteral("admin"),
                                   now.addMSecs(1000)));
        AlarmEngine activeRestored(&repository);
        QCOMPARE(activeRestored.activeEvents().size(), 1);
        QCOMPARE(static_cast<int>(activeRestored.activeEvents().constFirst().state),
                 static_cast<int>(AlarmState::Acknowledged));
        now = now.addMSecs(2000);
        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 70.0, now));

        const auto last = engine.lastEvent(key);
        QVERIFY(last.has_value());
        QCOMPARE(static_cast<int>(last->state), static_cast<int>(AlarmState::Cleared));
        QCOMPARE(static_cast<int>(engine.stateFor(key)), static_cast<int>(AlarmState::Normal));

        AlarmEngine restored(&repository);
        const auto restoredLast = restored.lastEvent(key);
        QVERIFY(restoredLast.has_value());
        QCOMPARE(static_cast<int>(restoredLast->state), static_cast<int>(AlarmState::Cleared));
        QCOMPARE(restoredLast->acknowledgedBy, QStringLiteral("admin"));
        QCOMPARE(restored.activeEvents().size(), 0);
        QCOMPARE(static_cast<int>(restored.stateFor(key)), static_cast<int>(AlarmState::Normal));
    }

    void lowThreshold()
    {
        QDateTime now = utc(QStringLiteral("2026-09-25T10:00:00Z"));
        AlarmEngine engine;
        engine.setClock([&now]() { return now; });

        AlarmRule rule = highTemperatureRule(10.0, 1.0);
        rule.ruleId = QStringLiteral("temperature-low");
        rule.type = AlarmRuleType::LowThreshold;
        rule.threshold = 10.0;
        QVERIFY(engine.addRule(rule));

        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 9.0, now));
        QCOMPARE(engine.activeEvents().size(), 1);
        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 10.5, now));
        QCOMPARE(engine.activeEvents().size(), 1);
        engine.processTelemetry(temperatureSample(QStringLiteral("DEV-001"), 11.0, now));
        QCOMPARE(engine.activeEvents().size(), 0);
    }
};

QTEST_MAIN(AlarmEngineTest)
#include "AlarmEngineTest.moc"
