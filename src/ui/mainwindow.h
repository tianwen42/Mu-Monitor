#pragma once

#include <QMainWindow>
#include <QStringList>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QPlainTextEdit;
class QTimer;
class TelemetryTableModel;
class TrendChartWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onConnectClicked();
    void onStartClicked();
    void onClearAlarmsClicked();
    void onAckAlarmClicked();
    void onDeviceSearchChanged(const QString &text);
    void updateDemoData();

private:
    void setupUi();
    void setupDocks();
    void setupToolBar();
    void setupMenus();
    void setupConnections();
    void setupDemoDevices();
    void setConnectionState(bool connected);
    void updateKpi();
    void appendAlarm(const QString &deviceId, const QString &message);

    Ui::MainWindow *ui = nullptr;
    TelemetryTableModel *m_model = nullptr;
    TrendChartWidget *m_trendChart = nullptr;
    QTimer *m_timer = nullptr;
    QPlainTextEdit *m_logOutput = nullptr;
    QStringList m_deviceIds;
    QStringList m_deviceNames;
    bool m_connected = false;
    int m_tick = 0;
    int m_dataPoints = 0;
    double m_averageTemperature = 0.0;
};
