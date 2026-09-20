#include "DeviceSimulatorWindow.h"

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QColor>
#include <QDateTime>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>

DeviceSimulatorWindow::DeviceSimulatorWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    setupConnections();
    updateServerState(false);
    updateDeviceTable();
    updateScenarioButtons();
}

void DeviceSimulatorWindow::setupUi()
{
    setWindowTitle(QStringLiteral("Mu-Monitor 设备模拟器"));
    setMinimumSize(780, 620);
    resize(920, 700);

    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(12);

    auto *serverGroup = new QGroupBox(QStringLiteral("TCP Server"), central);
    auto *serverLayout = new QHBoxLayout(serverGroup);
    serverLayout->setSpacing(10);

    serverLayout->addWidget(new QLabel(QStringLiteral("监听地址"), serverGroup));
    m_hostEdit = new QLineEdit(serverGroup);
    m_hostEdit->setMaximumWidth(150);

    serverLayout->addWidget(new QLabel(QStringLiteral("端口"), serverGroup));
    m_portSpin = new QSpinBox(serverGroup);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setMaximumWidth(100);

    m_startButton = new QPushButton(serverGroup);
    m_startButton->setMinimumWidth(110);
    serverLayout->addWidget(m_startButton);
    serverLayout->addStretch();

    m_clientCountLabel = new QLabel(serverGroup);
    m_clientCountLabel->setObjectName(QStringLiteral("clientCountLabel"));
    serverLayout->addWidget(m_clientCountLabel);
    rootLayout->addWidget(serverGroup);

    auto *deviceGroup = new QGroupBox(QStringLiteral("模拟设备"), central);
    auto *deviceLayout = new QVBoxLayout(deviceGroup);
    deviceLayout->setSpacing(10);

    m_deviceTable = new QTableWidget(0, 6, deviceGroup);
    m_deviceTable->setHorizontalHeaderLabels({
        QStringLiteral("设备编号"),
        QStringLiteral("设备名称"),
        QStringLiteral("场景"),
        QStringLiteral("温度"),
        QStringLiteral("压力"),
        QStringLiteral("转速"),
    });
    m_deviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deviceTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_deviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_deviceTable->setAlternatingRowColors(true);
    m_deviceTable->verticalHeader()->setVisible(false);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    deviceLayout->addWidget(m_deviceTable);

    auto *scenarioLayout = new QHBoxLayout;
    scenarioLayout->addWidget(new QLabel(QStringLiteral("选中设备场景"), deviceGroup));
    m_highTemperatureButton = new QPushButton(QStringLiteral("高温"), deviceGroup);
    m_highPressureButton = new QPushButton(QStringLiteral("高压"), deviceGroup);
    m_offlineButton = new QPushButton(QStringLiteral("离线"), deviceGroup);
    m_normalButton = new QPushButton(QStringLiteral("恢复正常"), deviceGroup);
    scenarioLayout->addWidget(m_highTemperatureButton);
    scenarioLayout->addWidget(m_highPressureButton);
    scenarioLayout->addWidget(m_offlineButton);
    scenarioLayout->addWidget(m_normalButton);
    scenarioLayout->addStretch();
    deviceLayout->addLayout(scenarioLayout);
    rootLayout->addWidget(deviceGroup, 1);

    auto *logGroup = new QGroupBox(QStringLiteral("发送日志"), central);
    auto *logLayout = new QVBoxLayout(logGroup);
    m_logOutput = new QPlainTextEdit(logGroup);
    m_logOutput->setReadOnly(true);
    m_logOutput->setMaximumBlockCount(2000);
    m_logOutput->setPlaceholderText(QStringLiteral("启动服务后，连接与发送日志会显示在这里。"));
    logLayout->addWidget(m_logOutput);
    rootLayout->addWidget(logGroup, 1);

    setCentralWidget(central);

    QSettings settings;
    m_hostEdit->setText(
        settings.value(QStringLiteral("simulator/host"), QStringLiteral("127.0.0.1")).toString());
    m_portSpin->setValue(
        settings.value(QStringLiteral("simulator/port"), 45454).toInt());

    setStyleSheet(QStringLiteral(
        "QWidget { background:#0b1220; color:#e5e7eb; font-family:'Microsoft YaHei','Segoe UI'; font-size:13px; }"
        "QGroupBox { background:#111c2e; border:1px solid #24324a; border-radius:8px; margin-top:10px; padding:12px 10px 10px 10px; font-weight:600; }"
        "QGroupBox::title { subcontrol-origin:margin; left:12px; padding:0 5px; color:#f8fafc; }"
        "QLineEdit,QSpinBox,QTableWidget,QPlainTextEdit { background:#0f172a; color:#e5e7eb; border:1px solid #26354f; border-radius:5px; padding:5px; }"
        "QHeaderView::section { background:#17243a; color:#cbd5e1; border:0; border-right:1px solid #26354f; padding:7px; }"
        "QPushButton { background:#1f6feb; color:white; border:0; border-radius:5px; padding:7px 12px; font-weight:600; }"
        "QPushButton:hover { background:#388bfd; }"
        "QPushButton:disabled { background:#334155; color:#94a3b8; }"
        "QLabel#clientCountLabel { color:#22c55e; font-weight:700; padding:4px 8px; }"));
}

void DeviceSimulatorWindow::setupConnections()
{
    connect(m_startButton, &QPushButton::clicked,
            this, &DeviceSimulatorWindow::toggleServer);
    connect(m_deviceTable, &QTableWidget::itemSelectionChanged,
            this, &DeviceSimulatorWindow::updateScenarioButtons);
    connect(m_highTemperatureButton, &QPushButton::clicked, this, [this]() {
        setScenario(DeviceSimulatorServer::Scenario::HighTemperature);
    });
    connect(m_highPressureButton, &QPushButton::clicked, this, [this]() {
        setScenario(DeviceSimulatorServer::Scenario::HighPressure);
    });
    connect(m_offlineButton, &QPushButton::clicked, this, [this]() {
        setScenario(DeviceSimulatorServer::Scenario::Offline);
    });
    connect(m_normalButton, &QPushButton::clicked, this, [this]() {
        setScenario(DeviceSimulatorServer::Scenario::Normal);
    });

    connect(&m_server, &DeviceSimulatorServer::runningChanged,
            this, &DeviceSimulatorWindow::updateServerState);
    connect(&m_server, &DeviceSimulatorServer::clientCountChanged,
            this, &DeviceSimulatorWindow::updateClientCount);
    connect(&m_server, &DeviceSimulatorServer::devicesChanged,
            this, &DeviceSimulatorWindow::updateDeviceTable);
    connect(&m_server, &DeviceSimulatorServer::logMessage,
            this, &DeviceSimulatorWindow::appendLog);
}

void DeviceSimulatorWindow::closeEvent(QCloseEvent *event)
{
    m_server.stop();
    event->accept();
}

void DeviceSimulatorWindow::toggleServer()
{
    if (m_server.isRunning()) {
        m_server.stop();
        return;
    }

    const QString hostText = m_hostEdit->text().trimmed();
    const QHostAddress address(hostText);
    if (address.isNull()) {
        QMessageBox::warning(
            this,
            QStringLiteral("监听地址无效"),
            QStringLiteral("请输入有效的 IPv4 或 IPv6 地址，例如 127.0.0.1 或 0.0.0.0。"));
        return;
    }

    QSettings settings;
    settings.setValue(QStringLiteral("simulator/host"), hostText);
    settings.setValue(QStringLiteral("simulator/port"), m_portSpin->value());

    QString errorMessage;
    if (!m_server.start(address, static_cast<quint16>(m_portSpin->value()), &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("启动失败"), errorMessage);
    }
}

void DeviceSimulatorWindow::updateServerState(bool running)
{
    m_startButton->setText(running ? QStringLiteral("停止服务")
                                   : QStringLiteral("启动服务"));
    m_hostEdit->setEnabled(!running);
    m_portSpin->setEnabled(!running);
    statusBar()->showMessage(
        running ? QStringLiteral("TCP Server 正在连续发送模拟数据")
                : QStringLiteral("TCP Server 已停止"));
}

void DeviceSimulatorWindow::updateClientCount(int count)
{
    m_clientCountLabel->setText(QStringLiteral("已连接客户端：%1").arg(count));
}

void DeviceSimulatorWindow::updateDeviceTable()
{
    const QString selected = selectedDeviceId();
    const QList<DeviceSimulatorServer::Device> devices = m_server.devices();
    m_deviceTable->setRowCount(devices.size());

    for (int row = 0; row < devices.size(); ++row) {
        const DeviceSimulatorServer::Device &device = devices.at(row);
        const QString scenario = m_server.scenarioText(device.id);

        auto *idItem = new QTableWidgetItem(device.id);
        idItem->setData(Qt::UserRole, device.id);
        m_deviceTable->setItem(row, 0, idItem);
        m_deviceTable->setItem(row, 1, new QTableWidgetItem(device.name));
        m_deviceTable->setItem(row, 2, new QTableWidgetItem(scenario));
        m_deviceTable->setItem(
            row, 3, new QTableWidgetItem(QStringLiteral("%1 °C").arg(device.temperature, 0, 'f', 1)));
        m_deviceTable->setItem(
            row, 4, new QTableWidgetItem(QStringLiteral("%1 MPa").arg(device.pressure, 0, 'f', 2)));
        m_deviceTable->setItem(
            row, 5, new QTableWidgetItem(QStringLiteral("%1 rpm").arg(device.speed, 0, 'f', 0)));

        QColor color(QStringLiteral("#cbd5e1"));
        if (device.scenario == DeviceSimulatorServer::Scenario::Offline) {
            color = QColor(QStringLiteral("#94a3b8"));
        } else if (device.scenario != DeviceSimulatorServer::Scenario::Normal) {
            color = QColor(QStringLiteral("#f87171"));
        } else {
            color = QColor(QStringLiteral("#22c55e"));
        }
        m_deviceTable->item(row, 2)->setForeground(color);

        if (device.id == selected) {
            m_deviceTable->selectRow(row);
        }
    }

    if (!selected.isEmpty() && !m_deviceTable->selectedItems().isEmpty()) {
        updateScenarioButtons();
    } else if (m_deviceTable->rowCount() > 0) {
        m_deviceTable->selectRow(0);
    }
}

void DeviceSimulatorWindow::updateScenarioButtons()
{
    const bool hasSelection = !selectedDeviceId().isEmpty();
    m_highTemperatureButton->setEnabled(hasSelection);
    m_highPressureButton->setEnabled(hasSelection);
    m_offlineButton->setEnabled(hasSelection);
    m_normalButton->setEnabled(hasSelection);
}

void DeviceSimulatorWindow::appendLog(const QString &message)
{
    m_logOutput->appendPlainText(
        QStringLiteral("[%1] %2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
                 message));
}

void DeviceSimulatorWindow::setScenario(DeviceSimulatorServer::Scenario scenario)
{
    const QString deviceId = selectedDeviceId();
    if (deviceId.isEmpty()) {
        return;
    }
    m_server.setScenario(deviceId, scenario);
}

QString DeviceSimulatorWindow::selectedDeviceId() const
{
    const int row = m_deviceTable->currentRow();
    if (row < 0 || !m_deviceTable->item(row, 0)) {
        return QString();
    }
    return m_deviceTable->item(row, 0)->data(Qt::UserRole).toString();
}
