#include "ui/mainwindow.h"
#include "ui_mainwindow.h"

#include "ui/TelemetryTableModel.h"
#include "ui/TrendChartWidget.h"
#include "ui/SettingsDialog.h"
#include "ui/AboutDialog.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDockWidget>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtGlobal>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setupUi();
    setupDocks();
    setupToolBar();
    setupConnections();
    setupDemoDevices();
    setConnectionState(false);
    updateKpi();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupUi()
{
    // Keep the standard menu actions in code, but use only the toolbar as the visible top bar.
    menuBar()->hide();

    m_model = new TelemetryTableModel(this);
    ui->telemetryTable->setModel(m_model);
    ui->telemetryTable->setShowGrid(false);
    ui->telemetryTable->verticalHeader()->setVisible(false);
    ui->telemetryTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->telemetryTable->horizontalHeader()->setMinimumSectionSize(70);

    m_trendChart = new TrendChartWidget(ui->trendChartPlaceholder);
    auto *chartLayout = new QVBoxLayout(ui->trendChartPlaceholder);
    chartLayout->setContentsMargins(0, 0, 0, 0);
    chartLayout->addWidget(m_trendChart);

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);

}

void MainWindow::setupDocks()
{
    setDockOptions(QMainWindow::AnimatedDocks
                   | QMainWindow::AllowNestedDocks
                   | QMainWindow::AllowTabbedDocks);

    auto *deviceDock = new QDockWidget(QStringLiteral("设备列表"), this);
    deviceDock->setObjectName(QStringLiteral("deviceDock"));
    deviceDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    deviceDock->setFeatures(QDockWidget::DockWidgetMovable
                            | QDockWidget::DockWidgetFloatable
                            | QDockWidget::DockWidgetClosable);
    ui->devicePanel->setParent(nullptr);
    deviceDock->setWidget(ui->devicePanel);
    addDockWidget(Qt::LeftDockWidgetArea, deviceDock);

    auto *alarmDock = new QDockWidget(QStringLiteral("告警中心"), this);
    alarmDock->setObjectName(QStringLiteral("alarmDock"));
    alarmDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    alarmDock->setFeatures(QDockWidget::DockWidgetMovable
                           | QDockWidget::DockWidgetFloatable
                           | QDockWidget::DockWidgetClosable);
    ui->alarmPanel->setParent(nullptr);
    alarmDock->setWidget(ui->alarmPanel);
    addDockWidget(Qt::RightDockWidgetArea, alarmDock);

    auto *logDock = new QDockWidget(QStringLiteral("运行日志"), this);
    logDock->setObjectName(QStringLiteral("logDock"));
    logDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    logDock->setFeatures(QDockWidget::DockWidgetMovable
                         | QDockWidget::DockWidgetFloatable
                         | QDockWidget::DockWidgetClosable);
    m_logOutput = new QPlainTextEdit(logDock);
    m_logOutput->setObjectName(QStringLiteral("logOutput"));
    m_logOutput->setReadOnly(true);
    m_logOutput->setMaximumBlockCount(2000);
    m_logOutput->setPlaceholderText(QStringLiteral("系统运行日志将在这里显示..."));
    logDock->setWidget(m_logOutput);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);
    logDock->hide();

    const QList<QWidget *> oldTabs = {ui->devicesTab, ui->alarmTab, ui->settingsTab};
    for (QWidget *tab : oldTabs) {
        const int index = ui->mainTabs->indexOf(tab);
        if (index >= 0) {
            ui->mainTabs->removeTab(index);
        }
    }

    if (m_logOutput) {
        m_logOutput->appendPlainText(QStringLiteral("Mu-Monitor 界面初始化完成"));
    }
}

void MainWindow::setupToolBar()
{
    auto *toolBar = addToolBar(QStringLiteral("主工具栏"));
    toolBar->setObjectName(QStringLiteral("mainToolBar"));
    toolBar->setMovable(false);
    toolBar->setFloatable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    ui->connectButton->hide();
    ui->startButton->hide();
    ui->clearAlarmButton->hide();

    QAction *connectAction = new QAction(
        style()->standardIcon(QStyle::SP_ComputerIcon), QStringLiteral("连接设备"), this);
    connect(connectAction, &QAction::triggered, ui->connectButton, &QPushButton::click);
    toolBar->addAction(connectAction);

    QAction *collectAction = new QAction(
        style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("开始/暂停采集"), this);
    connect(collectAction, &QAction::triggered, ui->startButton, &QPushButton::click);
    toolBar->addAction(collectAction);

    QAction *refreshAction = new QAction(
        style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("刷新"), this);
    connect(refreshAction, &QAction::triggered, this, [this]() {
        statusBar()->showMessage(QStringLiteral("界面数据已刷新"), 2500);
    });
    toolBar->addAction(refreshAction);

    toolBar->addSeparator();

    QAction *clearAction = new QAction(
        style()->standardIcon(QStyle::SP_DialogResetButton), QStringLiteral("清空告警"), this);
    connect(clearAction, &QAction::triggered, ui->clearAlarmButton, &QPushButton::click);
    toolBar->addAction(clearAction);

    QAction *optionsAction = new QAction(
        style()->standardIcon(QStyle::SP_FileDialogDetailedView), QStringLiteral("设置"), this);
    optionsAction->setShortcut(QKeySequence::Preferences);
    connect(optionsAction, &QAction::triggered, this, [this]() {
        SettingsDialog dialog(this);
        dialog.exec();
    });
    toolBar->addAction(optionsAction);

    toolBar->addSeparator();

    QAction *aboutAction = new QAction(
        style()->standardIcon(QStyle::SP_MessageBoxInformation), QStringLiteral("关于"), this);
    connect(aboutAction, &QAction::triggered, this, [this]() {
        AboutDialog dialog(this);
        dialog.exec();
    });
    toolBar->addAction(aboutAction);
}

void MainWindow::setupMenus()
{
    auto showPlaceholder = [this](const QString &feature) {
        statusBar()->showMessage(feature + QStringLiteral("功能将在后续阶段接入"), 4000);
        QMessageBox::information(
            this,
            QStringLiteral("Mu-Monitor"),
            feature + QStringLiteral("功能将在配置持久化阶段接入。"));
    };

    auto invokeFocusMethod = [](const char *method) {
        if (QWidget *widget = QApplication::focusWidget()) {
            QMetaObject::invokeMethod(widget, method);
        }
    };

    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    QAction *newAction = fileMenu->addAction(QStringLiteral("新建配置"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, [showPlaceholder]() { showPlaceholder(QStringLiteral("新建配置")); });

    QAction *openAction = fileMenu->addAction(QStringLiteral("打开配置"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [showPlaceholder]() { showPlaceholder(QStringLiteral("打开配置")); });

    QAction *saveAction = fileMenu->addAction(QStringLiteral("保存配置"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this]() {
        statusBar()->showMessage(QStringLiteral("配置已暂存到界面内存"), 4000);
    });

    fileMenu->addSeparator();
    QAction *importAction = fileMenu->addAction(QStringLiteral("导入数据"));
    connect(importAction, &QAction::triggered, this, [showPlaceholder]() { showPlaceholder(QStringLiteral("导入数据")); });

    QAction *exportAction = fileMenu->addAction(QStringLiteral("导出数据"));
    connect(exportAction, &QAction::triggered, this, [showPlaceholder]() { showPlaceholder(QStringLiteral("导出数据")); });

    fileMenu->addSeparator();
    QAction *exitAction = fileMenu->addAction(QStringLiteral("退出"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    QMenu *editMenu = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
    QAction *undoAction = editMenu->addAction(QStringLiteral("撤销"));
    undoAction->setShortcut(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, this, [invokeFocusMethod]() { invokeFocusMethod("undo"); });

    QAction *redoAction = editMenu->addAction(QStringLiteral("重做"));
    redoAction->setShortcut(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, this, [invokeFocusMethod]() { invokeFocusMethod("redo"); });

    editMenu->addSeparator();
    QAction *cutAction = editMenu->addAction(QStringLiteral("剪切"));
    cutAction->setShortcut(QKeySequence::Cut);
    connect(cutAction, &QAction::triggered, this, [invokeFocusMethod]() { invokeFocusMethod("cut"); });

    QAction *copyAction = editMenu->addAction(QStringLiteral("复制"));
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, [invokeFocusMethod]() { invokeFocusMethod("copy"); });

    QAction *pasteAction = editMenu->addAction(QStringLiteral("粘贴"));
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, this, [invokeFocusMethod]() { invokeFocusMethod("paste"); });

    QAction *selectAllAction = editMenu->addAction(QStringLiteral("全选"));
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAction, &QAction::triggered, this, [invokeFocusMethod]() { invokeFocusMethod("selectAll"); });

    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    const QStringList tabNames = {
        QStringLiteral("总览"),
        QStringLiteral("实时监控"),
        QStringLiteral("历史数据"),
    };
    for (int i = 0; i < tabNames.size(); ++i) {
        QAction *action = viewMenu->addAction(tabNames.at(i));
        connect(action, &QAction::triggered, this, [this, i]() { ui->mainTabs->setCurrentIndex(i); });
    }

    QMenu *toolsMenu = menuBar()->addMenu(QStringLiteral("工具(&T)"));
    QAction *connectAction = toolsMenu->addAction(QStringLiteral("连接/断开设备"));
    connect(connectAction, &QAction::triggered, ui->connectButton, &QPushButton::click);

    QAction *collectAction = toolsMenu->addAction(QStringLiteral("开始/暂停采集"));
    connect(collectAction, &QAction::triggered, ui->startButton, &QPushButton::click);

    QAction *clearAction = toolsMenu->addAction(QStringLiteral("清空告警"));
    connect(clearAction, &QAction::triggered, ui->clearAlarmButton, &QPushButton::click);

    toolsMenu->addSeparator();
    QAction *optionsAction = toolsMenu->addAction(QStringLiteral("选项"));
    optionsAction->setShortcut(QKeySequence::Preferences);
    connect(optionsAction, &QAction::triggered, this, [this]() {
        SettingsDialog dialog(this);
        dialog.exec();
    });

    if (QToolBar *toolBar = findChild<QToolBar *>(QStringLiteral("mainToolBar"))) {
        viewMenu->addSeparator();
        viewMenu->addAction(toolBar->toggleViewAction());
    }

    const QList<QDockWidget *> docks = findChildren<QDockWidget *>();
    if (!docks.isEmpty()) {
        viewMenu->addSeparator();
        for (QDockWidget *dock : docks) {
            viewMenu->addAction(dock->toggleViewAction());
        }
    }

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    QAction *helpAction = helpMenu->addAction(QStringLiteral("使用说明"));
    connect(helpAction, &QAction::triggered, this, [this]() {
        QMessageBox::information(
            this,
            QStringLiteral("Mu-Monitor 使用说明"),
            QStringLiteral("1. 连接设备\n2. 开始采集\n3. 在实时监控页查看数据\n4. 在告警中心确认告警"));
    });

    QAction *aboutAction = helpMenu->addAction(QStringLiteral("关于 Mu-Monitor"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        AboutDialog dialog(this);
        dialog.exec();
    });
}

void MainWindow::setupConnections()
{
    connect(ui->connectButton, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(ui->clearAlarmButton, &QPushButton::clicked, this, &MainWindow::onClearAlarmsClicked);
    connect(ui->ackAlarmButton, &QPushButton::clicked, this, &MainWindow::onAckAlarmClicked);
    connect(ui->deviceSearch, &QLineEdit::textChanged, this, &MainWindow::onDeviceSearchChanged);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::updateDemoData);
}

void MainWindow::setupDemoDevices()
{
    struct DeviceSeed
    {
        const char *id;
        const char *name;
    };

    const DeviceSeed seeds[] = {
        {"DEV-001", "注塑机 A线"},
        {"DEV-002", "空压机 1号"},
        {"DEV-003", "焊接机器人"},
        {"DEV-004", "包装线"},
    };

    for (const DeviceSeed &seed : seeds) {
        const QString id = QString::fromUtf8(seed.id);
        const QString name = QString::fromUtf8(seed.name);
        m_deviceIds << id;
        m_deviceNames << name;

        auto *item = new QListWidgetItem(QStringLiteral("●  %1  %2").arg(id, name));
        item->setData(Qt::UserRole, id);
        item->setData(Qt::UserRole + 1, name);
        item->setForeground(QColor(QStringLiteral("#38bdf8")));
        ui->deviceList->addItem(item);
    }

    if (ui->deviceList->count() > 0) {
        ui->deviceList->setCurrentRow(0);
    }
}

void MainWindow::setConnectionState(bool connected)
{
    m_connected = connected;

    if (connected) {
        ui->connectionStatusLabel->setText(QStringLiteral("● 已连接（模拟）"));
        ui->connectionStatusLabel->setStyleSheet(QStringLiteral("color:#22c55e;"));
        ui->connectButton->setText(QStringLiteral("断开设备"));
        ui->startButton->setEnabled(true);
        ui->statusbar->showMessage(QStringLiteral("模拟设备已连接 | 等待采集"), 4000);
        if (m_logOutput) m_logOutput->appendPlainText(QStringLiteral("模拟设备已连接"));
    } else {
        ui->connectionStatusLabel->setText(QStringLiteral("● 未连接"));
        ui->connectionStatusLabel->setStyleSheet(QStringLiteral("color:#94a3b8;"));
        ui->connectButton->setText(QStringLiteral("连接设备"));
        ui->startButton->setEnabled(false);
        ui->startButton->setText(QStringLiteral("开始采集"));
        m_timer->stop();
        ui->statusbar->showMessage(QStringLiteral("系统就绪 | 模拟数据模式"), 4000);
        if (m_logOutput) m_logOutput->appendPlainText(QStringLiteral("设备连接已断开"));
    }
}

void MainWindow::onConnectClicked()
{
    setConnectionState(!m_connected);
}

void MainWindow::onStartClicked()
{
    if (!m_connected) {
        return;
    }

    if (m_timer->isActive()) {
        m_timer->stop();
        ui->startButton->setText(QStringLiteral("开始采集"));
        ui->statusbar->showMessage(QStringLiteral("采集已暂停"), 3000);
        if (m_logOutput) m_logOutput->appendPlainText(QStringLiteral("采集已暂停"));
    } else {
        m_timer->start();
        ui->startButton->setText(QStringLiteral("暂停采集"));
        ui->statusbar->showMessage(QStringLiteral("开始接收模拟设备数据"), 3000);
        if (m_logOutput) m_logOutput->appendPlainText(QStringLiteral("开始接收模拟设备数据"));
    }
}

void MainWindow::onClearAlarmsClicked()
{
    ui->alarmList->clear();
    ui->overviewAlarmList->clear();
    updateKpi();
}

void MainWindow::onAckAlarmClicked()
{
    const int row = ui->alarmList->currentRow();
    if (row < 0) {
        return;
    }

    QListWidgetItem *item = ui->alarmList->takeItem(row);
    if (item) {
        const QString text = item->text();
        delete item;

        for (int i = 0; i < ui->overviewAlarmList->count(); ++i) {
            if (ui->overviewAlarmList->item(i)->text() == text) {
                delete ui->overviewAlarmList->takeItem(i);
                break;
            }
        }
    }
    updateKpi();
}

void MainWindow::onDeviceSearchChanged(const QString &text)
{
    for (int i = 0; i < ui->deviceList->count(); ++i) {
        QListWidgetItem *item = ui->deviceList->item(i);
        item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
    }
}

void MainWindow::updateDemoData()
{
    ++m_tick;
    m_averageTemperature = 0.0;

    for (int i = 0; i < m_deviceIds.size(); ++i) {
        const double jitterTemp = (QRandomGenerator::global()->generateDouble() - 0.5) * 7.0;
        const double jitterPressure = (QRandomGenerator::global()->generateDouble() - 0.5) * 0.16;
        const double jitterSpeed = (QRandomGenerator::global()->generateDouble() - 0.5) * 120.0;
        const double jitterVoltage = (QRandomGenerator::global()->generateDouble() - 0.5) * 5.0;

        double temperature = 58.0 + i * 5.0 + jitterTemp;
        double pressure = 1.05 + i * 0.10 + jitterPressure;
        double speed = 1200.0 + i * 170.0 + jitterSpeed;
        double voltage = 220.0 + jitterVoltage;
        QString status = QStringLiteral("在线");
        QString alarmMessage;

        if (m_tick % 13 == 0 && i == 1) {
            pressure = 2.08;
            status = QStringLiteral("报警");
            alarmMessage = QStringLiteral("压力超过阈值 1.80 MPa");
        }

        if (m_tick % 17 == 0 && i == 2) {
            temperature = 86.5;
            status = QStringLiteral("报警");
            alarmMessage = QStringLiteral("温度超过阈值 80 °C");
        }

        TelemetryRecord record;
        record.deviceId = m_deviceIds.at(i);
        record.name = m_deviceNames.at(i);
        record.status = status;
        record.temperature = temperature;
        record.pressure = pressure;
        record.speed = speed;
        record.voltage = voltage;
        record.updatedAt = QDateTime::currentDateTime();

        m_model->upsertRecord(record);
        m_trendChart->addSample(temperature, pressure);
        m_averageTemperature += temperature;
        ++m_dataPoints;

        if (!alarmMessage.isEmpty()) {
            appendAlarm(record.deviceId, alarmMessage);
        }
    }

    if (!m_deviceIds.isEmpty()) {
        m_averageTemperature /= m_deviceIds.size();
    } else {
        m_averageTemperature = 0.0;
    }

    updateKpi();
}

void MainWindow::updateKpi()
{
    ui->onlineDevicesValue->setText(QString::number(ui->deviceList->count()));
    ui->activeAlarmsValue->setText(QString::number(ui->alarmList->count()));
    ui->avgTemperatureValue->setText(
        m_model->recordCount() == 0
            ? QStringLiteral("-- °C")
            : QString::number(m_averageTemperature, 'f', 1) + QStringLiteral(" °C"));
    ui->dataPointsValue->setText(QString::number(m_dataPoints));
}

void MainWindow::appendAlarm(const QString &deviceId, const QString &message)
{
    const QString time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString text = QStringLiteral("[%1]  %2  %3").arg(time, deviceId, message);

    auto *item = new QListWidgetItem(text);
    item->setForeground(QColor(QStringLiteral("#f87171")));
    ui->alarmList->insertItem(0, item);
    if (m_logOutput) {
        m_logOutput->appendPlainText(text);
    }

    auto *overviewItem = new QListWidgetItem(text);
    overviewItem->setForeground(QColor(QStringLiteral("#f87171")));
    ui->overviewAlarmList->insertItem(0, overviewItem);

    while (ui->alarmList->count() > 50) {
        delete ui->alarmList->takeItem(ui->alarmList->count() - 1);
    }
    while (ui->overviewAlarmList->count() > 8) {
        delete ui->overviewAlarmList->takeItem(ui->overviewAlarmList->count() - 1);
    }

    updateKpi();
}
