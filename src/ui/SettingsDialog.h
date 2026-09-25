#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QStackedWidget;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private:
    void setupUi();
    void setupPages();
    void addPage(const QString &title, const QIcon &icon, QWidget *page);
    bool saveGeneralSettings();
    bool saveAlarmSettings();
    bool saveDataSourceSettings();
    void updateDataSourceControls();

    QListWidget *m_categoryList = nullptr;
    QStackedWidget *m_pageStack = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;
    QLabel *m_statusLabel = nullptr;
    QComboBox *m_startPageCombo = nullptr;
    QCheckBox *m_autoStartCheck = nullptr;
    QCheckBox *m_confirmExitCheck = nullptr;
    QCheckBox *m_alarmSoundCheck = nullptr;
    QCheckBox *m_alarmPopupCheck = nullptr;
    QDoubleSpinBox *m_temperatureThresholdSpin = nullptr;
    QDoubleSpinBox *m_pressureThresholdSpin = nullptr;
    QSpinBox *m_offlineTimeoutSpin = nullptr;
    QSpinBox *m_activationDelaySpin = nullptr;
    QComboBox *m_dataSourceTypeCombo = nullptr;
    QLineEdit *m_hostEdit = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QSpinBox *m_samplingIntervalSpin = nullptr;
    QSpinBox *m_heartbeatIntervalSpin = nullptr;
    QCheckBox *m_reconnectCheck = nullptr;
    QSpinBox *m_reconnectDelaySpin = nullptr;
    QSpinBox *m_maxReconnectDelaySpin = nullptr;
    QSpinBox *m_connectTimeoutSpin = nullptr;
    QSpinBox *m_readTimeoutSpin = nullptr;
};
