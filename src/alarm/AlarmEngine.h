#pragma once

#include "alarm/AlarmEvent.h"
#include "alarm/AlarmRepository.h"
#include "alarm/AlarmRule.h"
#include "core/TelemetrySample.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>

#include <functional>
#include <memory>
#include <optional>

class AlarmEngine : public QObject
{
    Q_OBJECT

public:
    using Clock = std::function<QDateTime()>;

    explicit AlarmEngine(QObject *parent = nullptr);
    AlarmEngine(IAlarmRepository *repository, QObject *parent = nullptr);
    ~AlarmEngine() override;

    void setClock(Clock clock);
    QDateTime currentTime() const;

    void setRepository(IAlarmRepository *repository);
    IAlarmRepository *repository() const;

    void setRules(const QList<AlarmRule> &rules);
    bool addRule(const AlarmRule &rule, QString *errorMessage = nullptr);
    QList<AlarmRule> rules() const;
    void clearRules();

    bool restore(QString *errorMessage = nullptr);

    void processTelemetry(const TelemetrySample &sample);
    void processHeartbeat(const QString &deviceId, bool online,
                          const QDateTime &receivedAt = QDateTime());
    void updateOfflineStates(const QDateTime &at = QDateTime());

    bool acknowledge(const QString &eventId, const QString &operatorId,
                     const QDateTime &at = QDateTime());

    QList<AlarmEvent> activeEvents() const;
    std::optional<AlarmEvent> eventById(const QString &eventId) const;
    std::optional<AlarmEvent> lastEvent(const QString &alarmKey) const;
    AlarmState stateFor(const QString &alarmKey) const;

signals:
    void alarmRaised(const AlarmEvent &event);
    void alarmAcknowledged(const AlarmEvent &event);
    void alarmCleared(const AlarmEvent &event);
    void alarmStateChanged(const AlarmEvent &event, AlarmState previous,
                           AlarmState current);
    void errorOccurred(const QString &message);

private:
    void evaluateRuleState(const AlarmRule &rule, const QString &deviceId,
                           bool condition, bool recovery,
                           std::optional<double> observedValue,
                           const QDateTime &at);
    void raiseAlarm(const AlarmRule &rule, const QString &deviceId,
                    std::optional<double> observedValue,
                    const QDateTime &at);
    void clearAlarm(const QString &alarmKey, const QDateTime &at);
    void persistEvent(const AlarmEvent &event);
    QString formatMessage(const AlarmRule &rule, const QString &deviceId,
                          std::optional<double> observedValue) const;

    Clock m_clock;
    IAlarmRepository *m_repository = nullptr;
    std::unique_ptr<IAlarmRepository> m_ownedRepository;
    QList<AlarmRule> m_rules;
    QHash<QString, AlarmEvent> m_activeEvents;
    QHash<QString, AlarmEvent> m_lastEvents;
    QHash<QString, QDateTime> m_candidates;
    QHash<QString, QDateTime> m_lastHeartbeat;
    QHash<QString, bool> m_deviceOnline;
};
