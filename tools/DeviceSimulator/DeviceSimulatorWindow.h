#pragma once

#include "DeviceSimulatorServer.h"

#include <QMainWindow>

class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

class DeviceSimulatorWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit DeviceSimulatorWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void toggleServer();
    void updateServerState(bool running);
    void updateClientCount(int count);
    void updateDeviceTable();
    void updateScenarioButtons();

private:
    void setupUi();
    void setupConnections();
    void appendLog(const QString &message);
    void setScenario(DeviceSimulatorServer::Scenario scenario);
    QString selectedDeviceId() const;

    DeviceSimulatorServer m_server;
    QLineEdit *m_hostEdit = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QPushButton *m_startButton = nullptr;
    QLabel *m_clientCountLabel = nullptr;
    QTableWidget *m_deviceTable = nullptr;
    QPushButton *m_highTemperatureButton = nullptr;
    QPushButton *m_highPressureButton = nullptr;
    QPushButton *m_offlineButton = nullptr;
    QPushButton *m_badCrcButton = nullptr;
    QPushButton *m_disconnectButton = nullptr;
    QPushButton *m_normalButton = nullptr;
    QComboBox *m_wireFormatCombo = nullptr;
    QPlainTextEdit *m_logOutput = nullptr;
};
