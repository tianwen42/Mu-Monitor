#pragma once

#include "alarm/AlarmEvent.h"
#include "core/BusinessStates.h"
#include "core/DeviceInfo.h"
#include "core/HeartbeatRecord.h"
#include "core/TelemetryRecord.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QStringList>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class AppController;
class QAction;
class QComboBox;
class QCloseEvent;
class QDockWidget;
class QDateTimeEdit;
class QLabel;
class QLineEdit;
class QListWidgetItem;
class QTableWidget;
class QResizeEvent;
class QShowEvent;
class QPlainTextEdit;
class QPushButton;
class QSystemTrayIcon;
class TelemetryTableModel;
class TrendChartWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(AppController *controller, const QString &currentUser,
                        QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onConnectClicked();
    void onStartClicked();
    void onClearAlarmsClicked();
    void onAcknowledgeAlarmClicked();
    void onAlarmActivated(QListWidgetItem *item);
    void onDeviceSelectionChanged(int row);
    void onStartSelectedDevice();
    void onStopSelectedDevice();
    void onDevicesChanged(const QList<DeviceInfo> &devices);
    void onTelemetryBatchReceived(const QList<TelemetryRecord> &records);
    void onHeartbeatBatchReceived(const QList<HeartbeatRecord> &heartbeats);
    void onConnectionStateChanged(ConnectionState state);
    void onCollectionStateChanged(CollectionState state);
    void onDeviceStateChanged(const QString &deviceId, bool online, bool collecting);
    void onOnlineDeviceCountChanged(int count);
    void onAlarmRaised(const AlarmEvent &event);
    void onAlarmAcknowledged(const AlarmEvent &event);
    void onAlarmCleared(const AlarmEvent &event);
    void onControllerError(const QString &message);

private:
    void setupUi();
    void setupDocks();
    void setupToolBar();
    void setupTrayIcon();
    void setupConnections();
    void setupDeviceList();
    void setupHistoryPage();
    void rebuildHistoryDeviceCombo();
    void applyDeviceFilter();
    void refreshDataSourceStatus();
    void appendLog(const QString &message);
    void toggleLogExpanded();
    void toggleLogPaused();
    void clearLog();
    void copyLog();
    bool isDeviceOnline(int index) const;
    void showStatusMessage(const QString &message, int timeout = 0);
    void updateKpi();
    void startDeviceCollection(int index);
    void stopDeviceCollection(int index);
    void updateSelectedChart();
    void updateDeviceControlState();
    void updateDeviceListItem(int index, bool online, bool collecting);
    TelemetryStatus currentStatusForDevice(const QString &deviceId) const;
    void syncTelemetryStatus(const QString &deviceId);
    void appendAlarm(const AlarmEvent &event);
    void updateAlarmItem(QListWidgetItem *item, const AlarmEvent &event);
    QListWidgetItem *findAlarmItem(const QString &eventId) const;
    QListWidgetItem *findOverviewAlarmItem(const QString &eventId) const;
    void editDeviceInfo(int index);
    void showDeviceAlarmHistory(int index);
    void loadHistoryData(bool useRange);
    void restorePersistedState();
    void applyResponsiveLayout();

    Ui::MainWindow *ui = nullptr;
    AppController *m_controller = nullptr;
    TelemetryTableModel *m_model = nullptr;
    QLabel *m_userStatusLabel = nullptr;
    QLabel *m_dataSourceStatusLabel = nullptr;
    QLabel *m_deviceFilterCountLabel = nullptr;
    QLabel *m_overviewDeviceNameLabel = nullptr;
    QLabel *m_overviewDeviceStateLabel = nullptr;
    QLabel *m_overviewDeviceMetaLabel = nullptr;
    QLabel *m_overviewDeviceMetricsLabel = nullptr;
    QComboBox *m_historyDeviceCombo = nullptr;
    QComboBox *m_deviceFilterCombo = nullptr;
    QLineEdit *m_deviceSearchEdit = nullptr;
    QDateTimeEdit *m_historyStartEdit = nullptr;
    QDateTimeEdit *m_historyEndEdit = nullptr;
    QLabel *m_historyCountLabel = nullptr;
    QTableWidget *m_historyTable = nullptr;
    TrendChartWidget *m_trendChart = nullptr;
    QPlainTextEdit *m_logOutput = nullptr;
    QDockWidget *m_deviceDock = nullptr;
    QDockWidget *m_logDock = nullptr;
    QPushButton *m_logExpandButton = nullptr;
    QPushButton *m_logPauseButton = nullptr;
    QPushButton *m_logClearButton = nullptr;
    QPushButton *m_logCopyButton = nullptr;
    QAction *m_collectAction = nullptr;
    QAction *m_deviceDockAction = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    bool m_forceQuit = false;
    bool m_canControlCollection = false;
    bool m_trayMessageShown = false;
    bool m_logExpanded = false;
    bool m_logPaused = false;
    QStringList m_deviceIds;
    QStringList m_deviceNames;
    QHash<QString, DeviceInfo> m_deviceInfos;
    QString m_currentUser;
    QString m_runtimeDataSourceStatus;
    QDateTime m_loginTime;
    QString m_selectedDeviceId;
    int m_selectedDeviceIndex = 0;
    QHash<QString, QList<double>> m_temperatureHistory;
    QHash<QString, QList<double>> m_pressureHistory;
    QHash<QString, bool> m_deviceCollecting;
    QHash<QString, bool> m_deviceOnline;
    ConnectionState m_connectionState = ConnectionState::Disconnected;
    CollectionState m_collectionState = CollectionState::Stopped;
    qint64 m_dataPoints = 0;
    int m_onlineDeviceCount = 0;
    double m_averageTemperature = 0.0;
};
