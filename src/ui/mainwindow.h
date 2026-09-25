#pragma once

#include "core/DeviceInfo.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QStringList>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QComboBox;
class QCloseEvent;
class QDateTimeEdit;
class QLabel;
class QLineEdit;
class QListWidgetItem;
class QModelIndex;
class QPoint;
class QTableWidget;
class QResizeEvent;
class QShowEvent;
class QPlainTextEdit;
class QSystemTrayIcon;
class QTimer;
class TelemetryTableModel;
class TrendChartWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &currentUser, QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onConnectClicked();
    void onStartClicked();
    void onClearAlarmsClicked();
    void onAlarmActivated(QListWidgetItem *item);
    void onDeviceSelectionChanged(int row);
    void filterDeviceList(const QString &text);
    void onTelemetryTableSelectionChanged(const QModelIndex &current,
                                           const QModelIndex &previous);
    void onStartSelectedDevice();
    void onStopSelectedDevice();
    void updateDemoData();
    void updateHeartbeat();

private:
    void setupUi();
    void setupDocks();
    void setupToolBar();
    void setupTrayIcon();
    void setupConnections();
    void setupDemoDevices();
    void setupHistoryPage();
    void setConnectionState(bool connected);
    bool isDeviceOnline(int index) const;
    void showStatusMessage(const QString &message, int timeout = 0);
    void updateKpi();
    void startDeviceCollection(int index);
    void stopDeviceCollection(int index);
    void updateSelectedChart();
    void selectDeviceById(const QString &deviceId);
    void showDeviceContextMenu(const QString &deviceId, const QPoint &globalPosition);
    void updateDeviceControlState();
    void updateDeviceListItem(int index, bool online, bool collecting);
    void appendAlarm(const QString &deviceId, const QString &message);
    void editDeviceInfo(int index);
    void showDeviceAlarmHistory(int index);
    QDateTime historyStartDateTime() const;
    QDateTime historyEndDateTime() const;
    void setHistoryRange(const QDateTime &start, const QDateTime &end);
    void loadHistoryData(bool useRange);
    void restorePersistedState();
    void applyResponsiveLayout();

    Ui::MainWindow *ui = nullptr;
    TelemetryTableModel *m_model = nullptr;
    QLineEdit *m_deviceSearchEdit = nullptr;
    QLabel *m_userStatusLabel = nullptr;
    QLabel *m_overviewDeviceNameLabel = nullptr;
    QLabel *m_overviewDeviceStateLabel = nullptr;
    QLabel *m_overviewDeviceMetaLabel = nullptr;
    QLabel *m_overviewDeviceMetricsLabel = nullptr;
    QComboBox *m_historyDeviceCombo = nullptr;
    QDateTimeEdit *m_historyStartDateEdit = nullptr;
    QDateTimeEdit *m_historyStartTimeEdit = nullptr;
    QDateTimeEdit *m_historyEndDateEdit = nullptr;
    QDateTimeEdit *m_historyEndTimeEdit = nullptr;
    QLabel *m_historyCountLabel = nullptr;
    QTableWidget *m_historyTable = nullptr;
    TrendChartWidget *m_trendChart = nullptr;
    QTimer *m_timer = nullptr;
    QTimer *m_heartbeatTimer = nullptr;
    QPlainTextEdit *m_logOutput = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    bool m_forceQuit = false;
    bool m_trayMessageShown = false;
    QStringList m_deviceIds;
    QStringList m_deviceNames;
    QHash<QString, DeviceInfo> m_deviceInfos;
    QString m_currentUser;
    QDateTime m_loginTime;
    QString m_selectedDeviceId;
    int m_selectedDeviceIndex = 0;
    QHash<QString, QList<double>> m_temperatureHistory;
    QHash<QString, QList<double>> m_pressureHistory;
    QHash<QString, bool> m_deviceCollecting;
    QHash<QString, bool> m_deviceOnline;
    bool m_connected = false;
    int m_tick = 0;
    qint64 m_dataPoints = 0;
    int m_onlineDeviceCount = 0;
    double m_averageTemperature = 0.0;
};
