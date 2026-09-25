#include "ui/mainwindow.h"
#include "ui_mainwindow.h"

#include "ui/TelemetryTableModel.h"
#include "ui/TrendChartWidget.h"
#include "core/HeartbeatRecord.h"
#include "database/DatabaseManager.h"
#include "utils/TimeUtils.h"
#include "ui/SettingsDialog.h"
#include "ui/AboutDialog.h"

#include <QAction>
#include <QApplication>
#include <QAbstractSpinBox>
#include <QBoxLayout>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QResizeEvent>
#include <QPlainTextEdit>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QShowEvent>
#include <QSizePolicy>
#include <QWindow>
#include <QTabWidget>
#include <QTimeEdit>
#include <QTableView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtGlobal>

MainWindow::MainWindow(const QString &currentUser, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_currentUser(currentUser)
    , m_loginTime(QDateTime::currentDateTime())
{
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("Mu-Monitor - 工业设备监控与告警平台"));
    setupUi();
    setupDocks();
    setupToolBar();
    setupTrayIcon();
    setupConnections();
    setupDemoDevices();
    setupHistoryPage();
    restorePersistedState();
    setConnectionState(true);
    updateKpi();
}

MainWindow::~MainWindow()
{
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
    delete ui;
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

    const QIcon appIcon = QApplication::windowIcon();
    if (!appIcon.isNull()) {
        setWindowIcon(appIcon);
        if (windowHandle()) {
            windowHandle()->setIcon(appIcon);
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_forceQuit) {
        event->accept();
        return;
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        event->accept();
        m_forceQuit = true;
        qApp->quit();
        return;
    }

    event->ignore();
    hide();

    if (m_trayIcon && !m_trayMessageShown) {
        m_trayIcon->showMessage(
            QStringLiteral("Mu-Monitor"),
            QStringLiteral("程序已隐藏到系统托盘。可通过托盘菜单重新打开或退出。"),
            QSystemTrayIcon::Information,
            3500);
        m_trayMessageShown = true;
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    applyResponsiveLayout();
}

void MainWindow::applyResponsiveLayout()
{
    if (!ui || !ui->centralwidget) {
        return;
    }

    const int windowWidth = width();
    const bool compact = windowWidth < 1120;
    const int centralWidth = ui->centralwidget->width() > 0
        ? ui->centralwidget->width()
        : windowWidth;

    ui->appTitle->hide();
    ui->appSubtitle->hide();
    ui->currentUserLabel->hide();
    ui->headerFrame->hide();

    if (auto *deviceDock = findChild<QDockWidget *>(QStringLiteral("deviceDock"))) {
        const int minimumWidth = compact ? 190 : 220;
        const int maximumWidth = compact ? 270 : 360;
        deviceDock->setMinimumWidth(minimumWidth);
        deviceDock->setMaximumWidth(maximumWidth);
        ui->devicePanel->setMinimumWidth(minimumWidth);
        ui->devicePanel->setMaximumWidth(maximumWidth);
    }

    if (auto *lowerLayout = qobject_cast<QBoxLayout *>(ui->overviewLowerLayout)) {
        if (lowerLayout->direction() != QBoxLayout::TopToBottom) {
            lowerLayout->setDirection(QBoxLayout::TopToBottom);
        }
        lowerLayout->setStretch(0, 3);
        lowerLayout->setStretch(1, 2);
    }

    if (auto *kpiGrid = qobject_cast<QGridLayout *>(ui->kpiLayout)) {
        const int columns = centralWidth < 760 ? 2 : 4;
        if (kpiGrid->property("responsiveColumns").toInt() != columns) {
            const QList<QWidget *> cards = {
                ui->onlineDevicesCard,
                ui->activeAlarmsCard,
                ui->avgTemperatureCard,
                ui->dataPointsCard,
            };
            for (QWidget *card : cards) {
                kpiGrid->removeWidget(card);
            }
            for (int i = 0; i < cards.size(); ++i) {
                kpiGrid->addWidget(cards.at(i), i / columns, i % columns);
            }
            for (int column = 0; column < 4; ++column) {
                kpiGrid->setColumnStretch(column, column < columns ? 1 : 0);
            }
            kpiGrid->setHorizontalSpacing(compact ? 8 : 12);
            kpiGrid->setVerticalSpacing(compact ? 8 : 12);
            kpiGrid->setProperty("responsiveColumns", columns);
        }
    }

    ui->trendChartPlaceholder->setMinimumHeight(compact ? 120 : 180);
    ui->overviewAlarmPanel->setMinimumHeight(compact ? 100 : 140);

    if (m_overviewDeviceMetaLabel) {
        m_overviewDeviceMetaLabel->setMaximumWidth(compact ? 260 : QWIDGETSIZE_MAX);
    }
    if (m_overviewDeviceMetricsLabel) {
        m_overviewDeviceMetricsLabel->setWordWrap(compact);
    }
}

void MainWindow::setupUi()
{
    menuBar()->hide();
    ui->appTitle->hide();
    ui->appSubtitle->hide();
    ui->headerFrame->hide();
    ui->alarmTitle->setText(QStringLiteral("告警记录 · 点击告警可定位设备"));

    const QString role = DatabaseManager::instance().roleForUser(m_currentUser);
    const QString roleText = role == QStringLiteral("admin")
        ? QStringLiteral("管理员")
        : QStringLiteral("普通用户");
    const QString userText = QStringLiteral("用户：%1（%2）").arg(m_currentUser, roleText);
    ui->currentUserLabel->setText(userText);
    ui->currentUserLabel->hide();

    m_userStatusLabel = new QLabel(userText, this);
    m_userStatusLabel->setObjectName(QStringLiteral("userStatusLabel"));
    m_userStatusLabel->setToolTip(
        QStringLiteral("登录时间：%1\n数据库：%2")
            .arg(TimeUtils::toLocalIso8601(m_loginTime),
                 DatabaseManager::instance().databasePath()));
    statusBar()->addWidget(m_userStatusLabel, 1);
    auto *selectedDevicePanel = new QFrame(ui->overviewTab);
    selectedDevicePanel->setObjectName(QStringLiteral("overviewSelectedDevicePanel"));
    auto *selectedDeviceLayout = new QHBoxLayout(selectedDevicePanel);
    selectedDeviceLayout->setContentsMargins(16, 10, 16, 10);
    selectedDeviceLayout->setSpacing(18);

    auto *selectedIdentityLayout = new QVBoxLayout;
    selectedIdentityLayout->setSpacing(2);
    auto *selectedDeviceTitle = new QLabel(QStringLiteral("当前设备"), selectedDevicePanel);
    selectedDeviceTitle->setObjectName(QStringLiteral("overviewDeviceTitle"));
    m_overviewDeviceNameLabel = new QLabel(QStringLiteral("未选择设备"), selectedDevicePanel);
    m_overviewDeviceNameLabel->setObjectName(QStringLiteral("overviewDeviceNameLabel"));
    m_overviewDeviceMetaLabel = new QLabel(
        QStringLiteral("从左侧设备列表选择后显示设备资料"), selectedDevicePanel);
    m_overviewDeviceMetaLabel->setObjectName(QStringLiteral("overviewDeviceMetaLabel"));
    m_overviewDeviceMetaLabel->setWordWrap(true);
    selectedIdentityLayout->addWidget(selectedDeviceTitle);
    selectedIdentityLayout->addWidget(m_overviewDeviceNameLabel);
    selectedIdentityLayout->addWidget(m_overviewDeviceMetaLabel);
    selectedDeviceLayout->addLayout(selectedIdentityLayout, 1);

    auto *selectedValueLayout = new QVBoxLayout;
    selectedValueLayout->setSpacing(3);
    selectedValueLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_overviewDeviceStateLabel = new QLabel(QStringLiteral("请选择设备"), selectedDevicePanel);
    m_overviewDeviceStateLabel->setObjectName(QStringLiteral("overviewDeviceStateLabel"));
    m_overviewDeviceStateLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_overviewDeviceMetricsLabel = new QLabel(QStringLiteral("暂无遥测数据"), selectedDevicePanel);
    m_overviewDeviceMetricsLabel->setObjectName(QStringLiteral("overviewDeviceMetricsLabel"));
    m_overviewDeviceMetricsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    selectedValueLayout->addWidget(m_overviewDeviceStateLabel);
    selectedValueLayout->addWidget(m_overviewDeviceMetricsLabel);
    selectedDeviceLayout->addLayout(selectedValueLayout);

    ui->overviewLayout->insertWidget(0, selectedDevicePanel);
    auto *monitorKpiLayout = new QGridLayout(ui->monitorTab);
    monitorKpiLayout->setObjectName(QStringLiteral("kpiLayout"));
    monitorKpiLayout->setHorizontalSpacing(12);
    monitorKpiLayout->setVerticalSpacing(12);
    const QList<QWidget *> kpiCards = {
        ui->onlineDevicesCard,
        ui->activeAlarmsCard,
        ui->avgTemperatureCard,
        ui->dataPointsCard,
    };
    for (int i = 0; i < kpiCards.size(); ++i) {
        ui->kpiLayout->removeWidget(kpiCards.at(i));
        monitorKpiLayout->addWidget(kpiCards.at(i), 0, i);
    }
    ui->overviewLayout->removeItem(ui->kpiLayout);
    delete ui->kpiLayout;
    ui->kpiLayout = monitorKpiLayout;
    ui->monitorLayout->insertLayout(0, ui->kpiLayout);
    m_model = new TelemetryTableModel(this);
    ui->telemetryTable->setModel(m_model);
    ui->telemetryTable->setShowGrid(false);
    ui->telemetryTable->verticalHeader()->setVisible(false);
    ui->telemetryTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->telemetryTable->horizontalHeader()->setMinimumSectionSize(70);
    m_deviceSearchEdit = new QLineEdit(ui->devicePanel);
    m_deviceSearchEdit->setObjectName(QStringLiteral("deviceSearchEdit"));
    m_deviceSearchEdit->setPlaceholderText(QStringLiteral("搜索设备编号、名称、型号或位置"));
    m_deviceSearchEdit->setClearButtonEnabled(true);
    ui->deviceLayout->insertWidget(1, m_deviceSearchEdit);
    connect(m_deviceSearchEdit, &QLineEdit::textChanged,
            this, &MainWindow::filterDeviceList);
    ui->deviceList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->deviceList->setTextElideMode(Qt::ElideRight);
    ui->deviceList->setUniformItemSizes(true);

    m_trendChart = new TrendChartWidget(ui->trendChartPlaceholder);
    auto *chartLayout = new QVBoxLayout(ui->trendChartPlaceholder);
    chartLayout->setContentsMargins(0, 0, 0, 0);
    chartLayout->addWidget(m_trendChart);

    m_timer = new QTimer(this);
    auto *versionLabel = new QLabel(
        QStringLiteral("v%1").arg(QApplication::applicationVersion()), this);
    versionLabel->setObjectName(QStringLiteral("versionStatusLabel"));
    versionLabel->setToolTip(QStringLiteral("Mu-Monitor 当前版本"));
    statusBar()->addPermanentWidget(versionLabel);

    m_timer->setInterval(1000);

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(3000);

    ui->rootLayout->setStretch(0, 0);
    ui->rootLayout->setStretch(1, 1);
    ui->overviewLayout->setStretch(0, 0);
    ui->overviewLayout->setStretch(1, 1);
    ui->monitorLayout->setStretch(0, 0);
    ui->monitorLayout->setStretch(1, 0);
    ui->monitorLayout->setStretch(2, 1);
    ui->trendLayout->setStretch(1, 1);
    ui->overviewAlarmLayout->setStretch(1, 1);
    ui->tableLayout->setStretch(1, 1);
    ui->mainTabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->telemetryTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    const QList<QFrame *> kpiCardFrames = {
        ui->onlineDevicesCard,
        ui->activeAlarmsCard,
        ui->avgTemperatureCard,
        ui->dataPointsCard,
    };
    for (QFrame *card : kpiCardFrames) {
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    applyResponsiveLayout();
}

void MainWindow::setupDocks()
{
    setDockOptions(QMainWindow::AnimatedDocks
                   | QMainWindow::AllowNestedDocks
                   | QMainWindow::AllowTabbedDocks);

    auto *deviceDock = new QDockWidget(QStringLiteral("设备列表"), this);
    deviceDock->setObjectName(QStringLiteral("deviceDock"));
    deviceDock->setAllowedAreas(Qt::LeftDockWidgetArea);
    deviceDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto *deviceTitleBar = new QWidget(deviceDock);
    deviceTitleBar->setFixedHeight(0);
    deviceDock->setTitleBarWidget(deviceTitleBar);
    ui->devicePanel->setParent(nullptr);
    deviceDock->setWidget(ui->devicePanel);
    addDockWidget(Qt::LeftDockWidgetArea, deviceDock);

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

    const QList<QWidget *> oldTabs = {ui->devicesTab, ui->settingsTab};
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

    ui->headerLayout->removeWidget(ui->connectionStatusLabel);
    ui->connectionStatusLabel->setParent(toolBar);

    QAction *collectAction = new QAction(
        style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("开始/暂停采集"), this);
    connect(collectAction, &QAction::triggered, ui->startButton, &QPushButton::click);
    toolBar->addAction(collectAction);

    QAction *refreshAction = new QAction(
        style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("刷新"), this);
    refreshAction->setObjectName(QStringLiteral("refreshAction"));
    connect(refreshAction, &QAction::triggered, this, [this]() {
        restorePersistedState();
        showStatusMessage(QStringLiteral("历史数据与看板已刷新"), 2500);
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

    auto *toolbarSpacer = new QWidget(toolBar);
    toolbarSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar->addWidget(toolbarSpacer);
    toolBar->addWidget(ui->connectionStatusLabel);

    QAction *aboutAction = new QAction(
        style()->standardIcon(QStyle::SP_MessageBoxInformation),
        QStringLiteral("关于 Mu-Monitor"), this);
    aboutAction->setObjectName(QStringLiteral("aboutAction"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        AboutDialog dialog(this);
        dialog.exec();
    });
    toolBar->addAction(aboutAction);

}

void MainWindow::setupTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }

    QIcon trayIcon = QApplication::windowIcon();
    if (trayIcon.isNull()) {
        trayIcon = style()->standardIcon(QStyle::SP_ComputerIcon);
    }
    setWindowIcon(trayIcon);

    m_trayIcon = new QSystemTrayIcon(trayIcon, this);
    m_trayIcon->setToolTip(QStringLiteral("Mu-Monitor - 工业设备监控与告警平台"));

    auto *trayMenu = new QMenu(this);

    QAction *showAction = trayMenu->addAction(QStringLiteral("显示主界面"));
    connect(showAction, &QAction::triggered, this, [this]() {
        showNormal();
        raise();
        activateWindow();
    });

    QAction *collectAction = trayMenu->addAction(QStringLiteral("开始/暂停采集"));
    connect(collectAction, &QAction::triggered, ui->startButton, &QPushButton::click);

    trayMenu->addSeparator();

    QAction *exitAction = trayMenu->addAction(QStringLiteral("退出 Mu-Monitor"));
    connect(exitAction, &QAction::triggered, this, [this]() {
        m_forceQuit = true;
        qApp->quit();
    });

    m_trayIcon->setContextMenu(trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger
                    || reason == QSystemTrayIcon::DoubleClick) {
                    showNormal();
                    raise();
                    activateWindow();
                }
            });
    m_trayIcon->show();
}

void MainWindow::setupConnections()
{
    connect(ui->connectButton, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(ui->clearAlarmButton, &QPushButton::clicked, this, &MainWindow::onClearAlarmsClicked);
    connect(ui->alarmList, &QListWidget::itemClicked, this, &MainWindow::onAlarmActivated);
    connect(ui->overviewAlarmList, &QListWidget::itemClicked, this, &MainWindow::onAlarmActivated);
    connect(ui->deviceList, &QListWidget::currentRowChanged,
            this, &MainWindow::onDeviceSelectionChanged);
    connect(ui->startSelectedDeviceButton, &QPushButton::clicked,
            this, &MainWindow::onStartSelectedDevice);
    connect(ui->stopSelectedDeviceButton, &QPushButton::clicked,
            this, &MainWindow::onStopSelectedDevice);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::updateDemoData);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &MainWindow::updateHeartbeat);

    ui->deviceList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->deviceList, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint &position) {
                QListWidgetItem *item = ui->deviceList->itemAt(position);
                if (!item) return;
                showDeviceContextMenu(
                    item->data(Qt::UserRole).toString(),
                    ui->deviceList->viewport()->mapToGlobal(position));
            });
    connect(ui->telemetryTable->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &MainWindow::onTelemetryTableSelectionChanged);

    ui->telemetryTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->telemetryTable, &QTableView::customContextMenuRequested, this,
            [this](const QPoint &position) {
                const QModelIndex index = ui->telemetryTable->indexAt(position);
                if (!index.isValid() || !m_model) {
                    return;
                }
                const QString deviceId = m_model
                    ->index(index.row(), TelemetryTableModel::DeviceId)
                    .data()
                    .toString();
                showDeviceContextMenu(
                    deviceId,
                    ui->telemetryTable->viewport()->mapToGlobal(position));
            });
}

void MainWindow::setupHistoryPage()
{
    if (!ui->historyPanelLayout || m_historyTable) {
        return;
    }

    ui->historyHint->setText(
        QStringLiteral("按设备和时间范围查询 SQLite 历史数据；单次最多显示 2000 条。"));

    auto *filterLayout = new QVBoxLayout;
    filterLayout->setSpacing(8);

    auto *rangeLayout = new QHBoxLayout;
    rangeLayout->setSpacing(6);

    auto *deviceLabel = new QLabel(QStringLiteral("设备"), ui->historyPanel);
    m_historyDeviceCombo = new QComboBox(ui->historyPanel);
    m_historyDeviceCombo->setMinimumWidth(180);
    m_historyDeviceCombo->setMaximumWidth(260);
    m_historyDeviceCombo->addItem(QStringLiteral("全部设备"), QString());
    for (int i = 0; i < m_deviceIds.size(); ++i) {
        m_historyDeviceCombo->addItem(
            QStringLiteral("%1  %2").arg(m_deviceIds.at(i), m_deviceNames.at(i)),
            m_deviceIds.at(i));
    }

    auto configureDateEdit = [](QDateTimeEdit *edit) {
        edit->setCalendarPopup(true);
        edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
        edit->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        edit->setMinimumWidth(116);
    };
    auto configureTimeEdit = [](QDateTimeEdit *edit) {
        edit->setDisplayFormat(QStringLiteral("HH:mm:ss"));
        edit->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        edit->setWrapping(true);
        edit->setAccelerated(true);
        edit->setMinimumWidth(92);
    };

    auto *startLabel = new QLabel(QStringLiteral("开始时间"), ui->historyPanel);
    m_historyStartDateEdit = new QDateEdit(
        QDate::currentDate().addDays(-1), ui->historyPanel);
    m_historyStartDateEdit->setObjectName(QStringLiteral("historyStartDateEdit"));
    configureDateEdit(m_historyStartDateEdit);

    m_historyStartTimeEdit = new QTimeEdit(QTime::currentTime(), ui->historyPanel);
    m_historyStartTimeEdit->setObjectName(QStringLiteral("historyStartTimeEdit"));
    configureTimeEdit(m_historyStartTimeEdit);

    auto *endLabel = new QLabel(QStringLiteral("结束时间"), ui->historyPanel);
    m_historyEndDateEdit = new QDateEdit(QDate::currentDate(), ui->historyPanel);
    m_historyEndDateEdit->setObjectName(QStringLiteral("historyEndDateEdit"));
    configureDateEdit(m_historyEndDateEdit);

    m_historyEndTimeEdit = new QTimeEdit(QTime::currentTime(), ui->historyPanel);
    m_historyEndTimeEdit->setObjectName(QStringLiteral("historyEndTimeEdit"));
    configureTimeEdit(m_historyEndTimeEdit);

    rangeLayout->addWidget(deviceLabel);
    rangeLayout->addWidget(m_historyDeviceCombo);
    rangeLayout->addSpacing(8);
    rangeLayout->addWidget(startLabel);
    rangeLayout->addWidget(m_historyStartDateEdit);
    rangeLayout->addWidget(m_historyStartTimeEdit);
    rangeLayout->addSpacing(8);
    rangeLayout->addWidget(endLabel);
    rangeLayout->addWidget(m_historyEndDateEdit);
    rangeLayout->addWidget(m_historyEndTimeEdit);
    rangeLayout->addStretch();

    auto *quickRangeLayout = new QHBoxLayout;
    quickRangeLayout->setSpacing(6);
    auto createQuickRangeButton = [this](const QString &text, const QString &objectName) {
        auto *button = new QPushButton(text, ui->historyPanel);
        button->setObjectName(objectName);
        button->setProperty("secondary", true);
        return button;
    };

    auto *lastHourButton = createQuickRangeButton(
        QStringLiteral("最近 1 小时"), QStringLiteral("historyLastHourButton"));
    auto *todayButton = createQuickRangeButton(
        QStringLiteral("今天"), QStringLiteral("historyTodayButton"));
    auto *last24HoursButton = createQuickRangeButton(
        QStringLiteral("最近 24 小时"), QStringLiteral("historyLast24HoursButton"));
    auto *last7DaysButton = createQuickRangeButton(
        QStringLiteral("最近 7 天"), QStringLiteral("historyLast7DaysButton"));

    auto *queryButton = new QPushButton(QStringLiteral("查询"), ui->historyPanel);
    queryButton->setObjectName(QStringLiteral("historyQueryButton"));
    auto *recentButton = new QPushButton(QStringLiteral("最近 1000 条"), ui->historyPanel);
    recentButton->setProperty("secondary", true);
    m_historyCountLabel = new QLabel(QStringLiteral("暂无历史记录"), ui->historyPanel);
    m_historyCountLabel->setObjectName(QStringLiteral("historyCountLabel"));

    quickRangeLayout->addWidget(lastHourButton);
    quickRangeLayout->addWidget(todayButton);
    quickRangeLayout->addWidget(last24HoursButton);
    quickRangeLayout->addWidget(last7DaysButton);
    quickRangeLayout->addStretch();
    quickRangeLayout->addWidget(queryButton);
    quickRangeLayout->addWidget(recentButton);
    quickRangeLayout->addWidget(m_historyCountLabel);

    filterLayout->addLayout(rangeLayout);
    filterLayout->addLayout(quickRangeLayout);

    const QDateTime initialNow = QDateTime::currentDateTime();
    connect(lastHourButton, &QPushButton::clicked, this, [this, initialNow]() {
        setHistoryRange(initialNow.addSecs(-3600), initialNow);
        loadHistoryData(true);
    });
    connect(todayButton, &QPushButton::clicked, this, [this, initialNow]() {
        setHistoryRange(QDateTime(initialNow.date(), QTime(0, 0)), initialNow);
        loadHistoryData(true);
    });
    connect(last24HoursButton, &QPushButton::clicked, this, [this, initialNow]() {
        setHistoryRange(initialNow.addDays(-1), initialNow);
        loadHistoryData(true);
    });
    connect(last7DaysButton, &QPushButton::clicked, this, [this, initialNow]() {
        setHistoryRange(initialNow.addDays(-7), initialNow);
        loadHistoryData(true);
    });

    m_historyTable = new QTableWidget(0, 8, ui->historyPanel);
    m_historyTable->setObjectName(QStringLiteral("historyTable"));
    m_historyTable->setHorizontalHeaderLabels({
        QStringLiteral("设备编号"),
        QStringLiteral("设备名称"),
        QStringLiteral("状态"),
        QStringLiteral("温度(℃)"),
        QStringLiteral("压力(MPa)"),
        QStringLiteral("转速(rpm)"),
        QStringLiteral("电压(V)"),
        QStringLiteral("记录时间 (ISO 8601)"),
    });
    m_historyTable->setAlternatingRowColors(true);
    m_historyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_historyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_historyTable->setShowGrid(false);
    m_historyTable->verticalHeader()->setVisible(false);
    m_historyTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_historyTable->horizontalHeader()->setMinimumSectionSize(80);

    ui->historyPanelLayout->addLayout(filterLayout);
    ui->historyPanelLayout->addWidget(m_historyTable, 1);

    connect(queryButton, &QPushButton::clicked, this, [this]() {
        loadHistoryData(true);
    });
    connect(recentButton, &QPushButton::clicked, this, [this]() {
        loadHistoryData(false);
    });
    connect(m_historyDeviceCombo, &QComboBox::currentIndexChanged,
            this, [this](int) {
                loadHistoryData(false);
            });
}

void MainWindow::restorePersistedState()
{
    QString errorMessage;
    const QList<TelemetryRecord> latestRecords =
        DatabaseManager::instance().latestDeviceRecords(&errorMessage);

    double temperatureSum = 0.0;
    int temperatureCount = 0;
    for (const TelemetryRecord &record : latestRecords) {
        m_model->upsertRecord(record);
        if (record.temperature > 0.0) {
            temperatureSum += record.temperature;
            ++temperatureCount;
        }
    }

    if (temperatureCount > 0) {
        m_averageTemperature = temperatureSum / temperatureCount;
    }

    m_dataPoints = DatabaseManager::instance().telemetryRecordCount(&errorMessage);

    const QList<TelemetryRecord> chartRecords =
        DatabaseManager::instance().recentTelemetryRecords(12000, QString(), &errorMessage);
    for (const TelemetryRecord &record : chartRecords) {
        QList<double> &temperatures = m_temperatureHistory[record.deviceId];
        QList<double> &pressures = m_pressureHistory[record.deviceId];
        temperatures.prepend(record.temperature);
        pressures.prepend(record.pressure);
        while (temperatures.size() > 120) temperatures.removeLast();
        while (pressures.size() > 120) pressures.removeLast();
    }

    const QList<HeartbeatRecord> heartbeats =
        DatabaseManager::instance().latestHeartbeatRecords(&errorMessage);
    for (const HeartbeatRecord &heartbeat : heartbeats) {
        const int index = m_deviceIds.indexOf(heartbeat.deviceId);
        if (index < 0) {
            continue;
        }
        m_deviceOnline[heartbeat.deviceId] = heartbeat.online;
        m_deviceCollecting[heartbeat.deviceId] = heartbeat.collecting;
        updateDeviceListItem(index, heartbeat.online, heartbeat.collecting);
    }

    if (!errorMessage.isEmpty()) {
        showStatusMessage(
            QStringLiteral("恢复历史数据失败：%1").arg(errorMessage), 5000);
    }

    loadHistoryData(false);
    updateSelectedChart();
    updateDeviceControlState();
    updateKpi();
}

QDateTime MainWindow::historyStartDateTime() const
{
    if (!m_historyStartDateEdit || !m_historyStartTimeEdit) {
        return {};
    }
    return QDateTime(m_historyStartDateEdit->date(), m_historyStartTimeEdit->time());
}

QDateTime MainWindow::historyEndDateTime() const
{
    if (!m_historyEndDateEdit || !m_historyEndTimeEdit) {
        return {};
    }
    return QDateTime(m_historyEndDateEdit->date(), m_historyEndTimeEdit->time());
}

void MainWindow::setHistoryRange(const QDateTime &start, const QDateTime &end)
{
    if (m_historyStartDateEdit && m_historyStartTimeEdit) {
        m_historyStartDateEdit->setDate(start.date());
        m_historyStartTimeEdit->setTime(start.time());
    }
    if (m_historyEndDateEdit && m_historyEndTimeEdit) {
        m_historyEndDateEdit->setDate(end.date());
        m_historyEndTimeEdit->setTime(end.time());
    }
}
void MainWindow::loadHistoryData(bool useRange)
{
    if (!m_historyTable || !m_historyDeviceCombo) {
        return;
    }

    QString errorMessage;
    QList<TelemetryRecord> records;
    const QString deviceId = m_historyDeviceCombo->currentData().toString();

    if (useRange) {
        const QDateTime start = historyStartDateTime();
        const QDateTime end = historyEndDateTime();
        if (!start.isValid() || !end.isValid() || start > end) {
            QMessageBox::warning(
                this, QStringLiteral("时间范围错误"),
                QStringLiteral("开始时间必须早于或等于结束时间。"));
            return;
        }
        records = DatabaseManager::instance().telemetryHistory(
            start, end, deviceId, 2000, &errorMessage);
    } else {
        records = DatabaseManager::instance().recentTelemetryRecords(
            1000, deviceId, &errorMessage);
    }

    if (!errorMessage.isEmpty()) {
        m_historyCountLabel->setText(QStringLiteral("查询失败"));
        showStatusMessage(errorMessage, 5000);
        return;
    }

    m_historyTable->setUpdatesEnabled(false);
    m_historyTable->clearContents();
    m_historyTable->setRowCount(records.size());

    for (int row = 0; row < records.size(); ++row) {
        const TelemetryRecord &record = records.at(row);
        const QStringList values = {
            record.deviceId,
            record.name,
            telemetryStatusDisplayName(record.status),
            QString::number(record.temperature, 'f', 1),
            QString::number(record.pressure, 'f', 2),
            QString::number(record.speed, 'f', 0),
            QString::number(record.voltage, 'f', 1),
            TimeUtils::toLocalIso8601(record.updatedAt),
        };

        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values.at(column));
            item->setTextAlignment(Qt::AlignCenter);
            m_historyTable->setItem(row, column, item);
        }

        QColor statusColor(QStringLiteral("#5f6b7a"));
        switch (record.status) {
        case TelemetryStatus::Online:
            statusColor = QColor(QStringLiteral("#2e7d32"));
            break;
        case TelemetryStatus::Alarm:
        case TelemetryStatus::Offline:
            statusColor = QColor(QStringLiteral("#b3261e"));
            break;
        case TelemetryStatus::Stopped:
            break;
        }
        m_historyTable->item(row, 2)->setForeground(statusColor);
    }

    m_historyTable->setUpdatesEnabled(true);
    m_historyCountLabel->setText(
        QStringLiteral("当前显示 %1 条").arg(records.size()));
}
void MainWindow::setupDemoDevices()
{
    m_deviceIds.clear();
    m_deviceNames.clear();
    m_deviceInfos.clear();
    m_onlineDeviceCount = 0;
    m_deviceCollecting.clear();
    m_deviceOnline.clear();
    ui->deviceList->clear();

    QList<DeviceInfo> defaults;
    defaults.reserve(100);
    for (int i = 1; i <= 100; ++i) {
        DeviceInfo device;
        device.deviceId = QStringLiteral("DEV-%1").arg(i, 3, 10, QLatin1Char('0'));
        device.name = QStringLiteral("模拟设备 %1").arg(i, 3, 10, QLatin1Char('0'));
        device.model = QStringLiteral("MU-%1").arg(i, 3, 10, QLatin1Char('0'));
        device.location = QStringLiteral("产线 %1").arg(((i - 1) / 10) + 1);
        device.protocol = (i % 2 == 0) ? QStringLiteral("Modbus TCP") : QStringLiteral("TCP");
        defaults.append(device);
    }

    QString databaseError;
    if (!DatabaseManager::instance().ensureDeviceInfos(defaults, &databaseError)) {
        if (m_logOutput) {
            m_logOutput->appendPlainText(QStringLiteral("初始化设备信息失败：") + databaseError);
        }
        DatabaseManager::instance().insertLog(
            QStringLiteral("ERROR"), QStringLiteral("device"),
            QStringLiteral("初始化设备信息失败：%1").arg(databaseError));
    }

    QHash<QString, DeviceInfo> storedDevices;
    const QList<DeviceInfo> stored = DatabaseManager::instance().deviceInfos(&databaseError);
    DatabaseManager::instance().insertLog(
        QStringLiteral("INFO"), QStringLiteral("device"),
        QStringLiteral("设备资料初始化完成，读取到 %1 台设备").arg(stored.size()));

    for (const DeviceInfo &device : stored) {
        storedDevices.insert(device.deviceId, device);
    }

    for (const DeviceInfo &defaultDevice : defaults) {
        const DeviceInfo device = storedDevices.value(defaultDevice.deviceId, defaultDevice);
        const bool online = ((m_deviceIds.size() + 1) % 10) != 0;
        m_deviceIds << device.deviceId;
        m_deviceNames << device.name;
        m_deviceInfos.insert(device.deviceId, device);
        m_deviceCollecting.insert(device.deviceId, true);
        m_deviceOnline.insert(device.deviceId, online);

        auto *item = new QListWidgetItem(QStringLiteral("●  %1  %2").arg(device.deviceId, device.name));
        item->setData(Qt::UserRole, device.deviceId);
        item->setData(Qt::UserRole + 1, device.name);
        item->setForeground(online ? QColor(QStringLiteral("#2e7d32"))
                                   : QColor(QStringLiteral("#5f6b7a")));
        ui->deviceList->addItem(item);

        if (online) {
            ++m_onlineDeviceCount;
        }
    }

    if (ui->deviceList->count() > 0) {
        ui->deviceList->setCurrentRow(0);
        onDeviceSelectionChanged(0);
    }
}

void MainWindow::filterDeviceList(const QString &text)
{
    const QString query = text.trimmed();
    int firstVisibleRow = -1;

    for (int row = 0; row < ui->deviceList->count(); ++row) {
        QListWidgetItem *item = ui->deviceList->item(row);
        const QString deviceId = item->data(Qt::UserRole).toString();
        const QString deviceName = item->data(Qt::UserRole + 1).toString();
        const DeviceInfo info = m_deviceInfos.value(deviceId);
        const QString searchText = QStringList{
            deviceId,
            deviceName,
            info.model,
            info.location,
            info.ipAddress,
            info.protocol,
        }.join(QLatin1Char(' '));

        const bool visible = query.isEmpty()
            || searchText.contains(query, Qt::CaseInsensitive);
        item->setHidden(!visible);
        if (visible && firstVisibleRow < 0) {
            firstVisibleRow = row;
        }
    }

    const int currentRow = ui->deviceList->currentRow();
    if (currentRow >= 0 && ui->deviceList->item(currentRow)->isHidden()) {
        if (firstVisibleRow >= 0) {
            ui->deviceList->setCurrentRow(firstVisibleRow);
            ui->deviceList->scrollToItem(
                ui->deviceList->item(firstVisibleRow), QAbstractItemView::PositionAtCenter);
        } else {
            ui->deviceList->clearSelection();
        }
    }
}
void MainWindow::updateDeviceListItem(int index, bool online, bool collecting)
{
    if (index < 0 || index >= ui->deviceList->count()) {
        return;
    }

    QColor color;
    QString status;
    if (!online) {
        color = QColor(QStringLiteral("#5f6b7a"));
        status = QStringLiteral("未连接");
    } else if (!collecting) {
        color = QColor(QStringLiteral("#5f6b7a"));
        status = QStringLiteral("已停止");
    } else {
        color = QColor(QStringLiteral("#2e7d32"));
        status = QStringLiteral("采集中");
    }

    QListWidgetItem *item = ui->deviceList->item(index);
    const QString deviceId = item->data(Qt::UserRole).toString();
    const QString deviceName = item->data(Qt::UserRole + 1).toString();
    item->setText(QStringLiteral("●  %1  %2  [%3]").arg(deviceId, deviceName, status));
    item->setForeground(color);
    item->setToolTip(
        QStringLiteral("%1\n右键：编辑设备信息 / 查看历史报警").arg(status));
}

void MainWindow::onDeviceSelectionChanged(int row)
{
    if (row < 0 || row >= m_deviceIds.size()) return;
    m_selectedDeviceIndex = row;
    m_selectedDeviceId = m_deviceIds.at(row);
    updateDeviceControlState();
    updateSelectedChart();

    if (!m_model) return;
    for (int modelRow = 0; modelRow < m_model->rowCount(); ++modelRow) {
        const QModelIndex index = m_model->index(modelRow, TelemetryTableModel::DeviceId);
        if (m_model->data(index).toString() != m_selectedDeviceId) continue;
        ui->telemetryTable->selectRow(modelRow);
        ui->telemetryTable->scrollTo(index, QAbstractItemView::PositionAtCenter);
        break;
    }
}

void MainWindow::onTelemetryTableSelectionChanged(const QModelIndex &current,
                                                   const QModelIndex &previous)
{
    Q_UNUSED(previous)
    if (!current.isValid() || !m_model) return;
    const QString deviceId = m_model
        ->index(current.row(), TelemetryTableModel::DeviceId)
        .data()
        .toString();
    selectDeviceById(deviceId);
}

void MainWindow::selectDeviceById(const QString &deviceId)
{
    const int row = m_deviceIds.indexOf(deviceId);
    if (row < 0) return;

    if (ui->deviceList->currentRow() == row) {
        m_selectedDeviceIndex = row;
        m_selectedDeviceId = deviceId;
        updateDeviceControlState();
        updateSelectedChart();
        return;
    }

    ui->deviceList->setCurrentRow(row);
    ui->deviceList->scrollToItem(ui->deviceList->item(row), QAbstractItemView::PositionAtCenter);
}

void MainWindow::showDeviceContextMenu(const QString &deviceId,
                                       const QPoint &globalPosition)
{
    const int index = m_deviceIds.indexOf(deviceId);
    if (index < 0) return;

    selectDeviceById(deviceId);
    const bool online = isDeviceOnline(index);
    const bool collecting = m_deviceCollecting.value(deviceId, false);

    QMenu menu(this);
    QAction *editAction = menu.addAction(QStringLiteral("编辑设备信息"));
    QAction *alarmAction = menu.addAction(QStringLiteral("查看历史报警信息"));
    menu.addSeparator();
    QAction *startAction = menu.addAction(QStringLiteral("开始设备采集"));
    QAction *stopAction = menu.addAction(QStringLiteral("停止设备采集"));
    startAction->setEnabled(m_connected && online && !collecting);
    stopAction->setEnabled(m_connected && online && collecting);

    QAction *selected = menu.exec(globalPosition);
    if (selected == editAction) {
        editDeviceInfo(index);
    } else if (selected == alarmAction) {
        showDeviceAlarmHistory(index);
    } else if (selected == startAction) {
        startDeviceCollection(index);
    } else if (selected == stopAction) {
        stopDeviceCollection(index);
    }
}
void MainWindow::startDeviceCollection(int index)
{
    if (index < 0 || index >= m_deviceIds.size()) {
        return;
    }

    const QString deviceId = m_deviceIds.at(index);
    if (!m_connected || m_deviceCollecting.value(deviceId, false)) {
        return;
    }

    m_deviceCollecting[deviceId] = true;
    updateDeviceListItem(index, true, true);
    DatabaseManager::instance().insertLog(
        QStringLiteral("INFO"), QStringLiteral("device"),
        QStringLiteral("设备开始采集：%1").arg(deviceId));
    updateDeviceControlState();
    updateSelectedChart();
}

void MainWindow::stopDeviceCollection(int index)
{
    if (index < 0 || index >= m_deviceIds.size()) {
        return;
    }

    const QString deviceId = m_deviceIds.at(index);
    m_deviceCollecting[deviceId] = false;
    updateDeviceListItem(index, isDeviceOnline(index), false);
    DatabaseManager::instance().insertLog(
        QStringLiteral("INFO"), QStringLiteral("device"),
        QStringLiteral("设备停止采集：%1").arg(deviceId));
    updateDeviceControlState();
    updateSelectedChart();
}

void MainWindow::onStartSelectedDevice()
{
    startDeviceCollection(m_deviceIds.indexOf(m_selectedDeviceId));
}

void MainWindow::onStopSelectedDevice()
{
    stopDeviceCollection(m_deviceIds.indexOf(m_selectedDeviceId));
}

void MainWindow::editDeviceInfo(int index)
{
    if (index < 0 || index >= m_deviceIds.size()) {
        return;
    }

    const QString deviceId = m_deviceIds.at(index);
    DeviceInfo info = m_deviceInfos.value(deviceId);
    info.deviceId = deviceId;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("编辑设备信息 - %1").arg(deviceId));
    dialog.setMinimumWidth(480);

    auto *layout = new QFormLayout(&dialog);
    layout->setHorizontalSpacing(16);
    layout->setVerticalSpacing(12);

    auto *idLabel = new QLabel(deviceId, &dialog);
    auto *nameEdit = new QLineEdit(info.name, &dialog);
    auto *modelEdit = new QLineEdit(info.model, &dialog);
    auto *locationEdit = new QLineEdit(info.location, &dialog);
    auto *ipEdit = new QLineEdit(info.ipAddress, &dialog);
    auto *protocolEdit = new QLineEdit(info.protocol, &dialog);
    auto *notesEdit = new QPlainTextEdit(info.notes, &dialog);
    notesEdit->setMaximumHeight(110);

    layout->addRow(QStringLiteral("设备编号"), idLabel);
    layout->addRow(QStringLiteral("设备名称"), nameEdit);
    layout->addRow(QStringLiteral("设备型号"), modelEdit);
    layout->addRow(QStringLiteral("安装位置"), locationEdit);
    layout->addRow(QStringLiteral("IP 地址"), ipEdit);
    layout->addRow(QStringLiteral("通信协议"), protocolEdit);
    layout->addRow(QStringLiteral("备注"), notesEdit);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(QString(), buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, nameEdit]() {
        if (nameEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("设备信息不完整"),
                                 QStringLiteral("设备名称不能为空。"));
            return;
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    info.name = nameEdit->text().trimmed();
    info.model = modelEdit->text().trimmed();
    info.location = locationEdit->text().trimmed();
    info.ipAddress = ipEdit->text().trimmed();
    info.protocol = protocolEdit->text().trimmed();
    info.notes = notesEdit->toPlainText().trimmed();

    QString errorMessage;
    if (!DatabaseManager::instance().updateDeviceInfo(info, &errorMessage)) {
        QMessageBox::critical(this, QStringLiteral("保存失败"), errorMessage);
        return;
    }

    m_deviceInfos.insert(deviceId, info);
    m_deviceNames[index] = info.name;

    QListWidgetItem *item = ui->deviceList->item(index);
    if (item) {
        item->setText(QStringLiteral("●  %1  %2").arg(deviceId, info.name));
        item->setData(Qt::UserRole + 1, info.name);
        item->setToolTip(
            QStringLiteral("型号：%1\n位置：%2\n协议：%3\nIP：%4")
                .arg(info.model, info.location, info.protocol, info.ipAddress));
    }

    DatabaseManager::instance().insertLog(
        QStringLiteral("INFO"), QStringLiteral("device"),
        QStringLiteral("编辑设备信息：%1").arg(deviceId));

    updateDeviceControlState();
    updateSelectedChart();
    showStatusMessage(QStringLiteral("设备信息已保存"), 3000);
}

void MainWindow::showDeviceAlarmHistory(int index)
{
    if (index < 0 || index >= m_deviceIds.size()) {
        return;
    }

    const QString deviceId = m_deviceIds.at(index);
    const QString deviceName = m_deviceNames.value(index);
    QString errorMessage;
    const QList<AlarmRecord> alarms =
        DatabaseManager::instance().alarmHistoryForDevice(deviceId, 500, &errorMessage);

    QDialog dialog(this);
    dialog.setWindowTitle(
        QStringLiteral("历史报警信息 - %1 %2").arg(deviceId, deviceName));
    dialog.resize(820, 520);

    auto *layout = new QVBoxLayout(&dialog);
    auto *title = new QLabel(
        QStringLiteral("设备：%1  %2").arg(deviceId, deviceName), &dialog);
    title->setObjectName(QStringLiteral("deviceDetailTitle"));
    auto *countLabel = new QLabel(
        errorMessage.isEmpty()
            ? QStringLiteral("共读取 %1 条报警记录（最多显示 500 条）").arg(alarms.size())
            : QStringLiteral("查询失败：%1").arg(errorMessage),
        &dialog);
    countLabel->setWordWrap(true);

    auto *table = new QTableWidget(alarms.size(), 3, &dialog);
    table->setHorizontalHeaderLabels({
        QStringLiteral("报警时间 (ISO 8601)"),
        QStringLiteral("级别"),
        QStringLiteral("报警内容"),
    });
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    for (int row = 0; row < alarms.size(); ++row) {
        const AlarmRecord &alarm = alarms.at(row);
        auto *timeItem = new QTableWidgetItem(TimeUtils::toLocalIso8601(alarm.occurredAt));
        auto *levelItem = new QTableWidgetItem(alarm.level);
        auto *messageItem = new QTableWidgetItem(alarm.message);
        timeItem->setTextAlignment(Qt::AlignCenter);
        levelItem->setTextAlignment(Qt::AlignCenter);
        messageItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        const QColor levelColor = alarm.level == QStringLiteral("ERROR")
            ? QColor(QStringLiteral("#b3261e"))
            : QColor(QStringLiteral("#9a6700"));
        levelItem->setForeground(levelColor);

        table->setItem(row, 0, timeItem);
        table->setItem(row, 1, levelItem);
        table->setItem(row, 2, messageItem);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    layout->addWidget(title);
    layout->addWidget(countLabel);
    layout->addWidget(table, 1);
    layout->addWidget(buttons);

    dialog.exec();
}
bool MainWindow::isDeviceOnline(int index) const
{
    if (index < 0 || index >= m_deviceIds.size()) {
        return false;
    }
    return m_deviceOnline.value(m_deviceIds.at(index), false);
}

void MainWindow::updateDeviceControlState()
{
    const int index = m_deviceIds.indexOf(m_selectedDeviceId);
    const bool hasSelection = index >= 0 && !m_selectedDeviceId.isEmpty();
    const bool online = hasSelection && m_connected && isDeviceOnline(index);
    const bool collecting = hasSelection && m_deviceCollecting.value(m_selectedDeviceId, false);

    TelemetryStatus statusCode = TelemetryStatus::Offline;
    if (hasSelection) {
        statusCode = !online
            ? TelemetryStatus::Offline
            : (collecting ? TelemetryStatus::Online : TelemetryStatus::Stopped);
    }

    TelemetryRecord latestRecord;
    const bool hasLatestRecord = hasSelection
        && m_model
        && m_model->recordForDevice(m_selectedDeviceId, &latestRecord);
    if (hasLatestRecord && online && collecting && latestRecord.status == TelemetryStatus::Alarm) {
        statusCode = TelemetryStatus::Alarm;
    }

    if (!hasSelection) {
        ui->selectedDeviceLabel->setText(QStringLiteral("当前设备：--"));
        m_overviewDeviceNameLabel->setText(QStringLiteral("未选择设备"));
        m_overviewDeviceStateLabel->setText(QStringLiteral("请选择设备"));
        m_overviewDeviceStateLabel->setStyleSheet(QStringLiteral("color:#5f6b7a;"));
        m_overviewDeviceMetaLabel->setText(QStringLiteral("从左侧设备列表选择后显示设备资料"));
        m_overviewDeviceMetricsLabel->setText(QStringLiteral("暂无遥测数据"));
        m_overviewDeviceMetricsLabel->setToolTip(QString());
        ui->startSelectedDeviceButton->setEnabled(false);
        ui->stopSelectedDeviceButton->setEnabled(false);
        return;
    }

    const QString status = telemetryStatusDisplayName(statusCode);
    const DeviceInfo info = m_deviceInfos.value(m_selectedDeviceId);
    const QString displayName = info.name.isEmpty()
        ? m_deviceNames.value(index)
        : info.name;
    ui->selectedDeviceLabel->setText(
        QStringLiteral("当前设备：%1 %2（%3）").arg(m_selectedDeviceId, displayName, status));

    QStringList metaParts;
    if (!info.model.trimmed().isEmpty()) {
        metaParts << QStringLiteral("型号 %1").arg(info.model);
    }
    if (!info.location.trimmed().isEmpty()) {
        metaParts << QStringLiteral("位置 %1").arg(info.location);
    }
    if (!info.protocol.trimmed().isEmpty()) {
        metaParts << QStringLiteral("协议 %1").arg(info.protocol);
    }
    if (!info.ipAddress.trimmed().isEmpty()) {
        metaParts << QStringLiteral("IP %1").arg(info.ipAddress);
    }

    m_overviewDeviceNameLabel->setText(
        QStringLiteral("%1 · %2").arg(m_selectedDeviceId, displayName));
    m_overviewDeviceMetaLabel->setText(
        metaParts.isEmpty()
            ? QStringLiteral("未配置设备资料")
            : metaParts.join(QStringLiteral("  ·  ")));
    m_overviewDeviceStateLabel->setText(QStringLiteral("● %1").arg(status));

    QString stateColor = QStringLiteral("#5f6b7a");
    switch (statusCode) {
    case TelemetryStatus::Alarm:
    case TelemetryStatus::Offline:
        stateColor = QStringLiteral("#b3261e");
        break;
    case TelemetryStatus::Online:
        stateColor = QStringLiteral("#2e7d32");
        break;
    case TelemetryStatus::Stopped:
        stateColor = QStringLiteral("#9a6700");
        break;
    }
    m_overviewDeviceStateLabel->setStyleSheet(
        QStringLiteral("color:%1;").arg(stateColor));

    if (hasLatestRecord) {
        m_overviewDeviceMetricsLabel->setText(
            QStringLiteral("温度 %1 °C  ·  压力 %2 MPa  ·  转速 %3 rpm  ·  电压 %4 V")
                .arg(QString::number(latestRecord.temperature, 'f', 1),
                     QString::number(latestRecord.pressure, 'f', 2),
                     QString::number(latestRecord.speed, 'f', 0),
                     QString::number(latestRecord.voltage, 'f', 1)));
        m_overviewDeviceMetricsLabel->setToolTip(
            QStringLiteral("最近更新时间：%1")
                .arg(TimeUtils::toLocalIso8601(latestRecord.updatedAt)));
    } else {
        m_overviewDeviceMetricsLabel->setText(QStringLiteral("等待遥测数据"));
        m_overviewDeviceMetricsLabel->setToolTip(QString());
    }

    const bool controllable = m_connected;
    ui->startSelectedDeviceButton->setEnabled(controllable && !collecting);
    ui->stopSelectedDeviceButton->setEnabled(controllable && collecting);
}
void MainWindow::updateSelectedChart()
{
    if (m_selectedDeviceId.isEmpty()) return;
    const int index = m_deviceIds.indexOf(m_selectedDeviceId);
    const bool online = isDeviceOnline(index);
    const bool collecting = m_deviceCollecting.value(m_selectedDeviceId, false);
    const QString status = !online
        ? QStringLiteral("未连接")
        : (collecting ? QStringLiteral("采集中") : QStringLiteral("已停止"));
    ui->trendTitle->setText(
        QStringLiteral("实时趋势 - %1 %2（%3）").arg(m_selectedDeviceId, m_deviceNames.value(index), status));
    m_trendChart->setSamples(
        m_temperatureHistory.value(m_selectedDeviceId),
        m_pressureHistory.value(m_selectedDeviceId),
        QStringLiteral("%1 %2").arg(m_selectedDeviceId, m_deviceNames.value(index)));
}
void MainWindow::setConnectionState(bool connected)
{
    m_connected = connected;

    if (connected) {
        ui->connectionStatusLabel->setText(QStringLiteral("● 心跳检测中"));
        ui->connectionStatusLabel->setStyleSheet(QStringLiteral("color:#2e7d32;"));
        ui->connectButton->setText(QStringLiteral("停止检测"));
        ui->startButton->setEnabled(true);

        m_heartbeatTimer->start();
        if (!m_timer->isActive()) {
            m_timer->start();
            ui->startButton->setText(QStringLiteral("暂停采集"));
        }

        updateHeartbeat();
        showStatusMessage(QStringLiteral("设备心跳检测与数据采集已自动启动"), 4000);
        if (m_logOutput) {
            m_logOutput->appendPlainText(QStringLiteral("设备心跳检测与数据采集已自动启动"));
        }
        DatabaseManager::instance().insertLog(
            QStringLiteral("INFO"), QStringLiteral("connection"),
            QStringLiteral("设备心跳检测与数据采集已自动启动"));
    } else {
        ui->connectionStatusLabel->setText(QStringLiteral("● 检测已停止"));
        ui->connectionStatusLabel->setStyleSheet(QStringLiteral("color:#5f6b7a;"));
        ui->connectButton->setText(QStringLiteral("开始检测"));
        ui->startButton->setEnabled(false);
        ui->startButton->setText(QStringLiteral("开始采集"));

        m_heartbeatTimer->stop();
        m_timer->stop();
        for (int i = 0; i < m_deviceIds.size(); ++i) {
            updateDeviceListItem(i, false, m_deviceCollecting.value(m_deviceIds.at(i), false));
        }

        showStatusMessage(QStringLiteral("设备心跳检测已停止"), 4000);
        updateDeviceControlState();
        if (m_logOutput) {
            m_logOutput->appendPlainText(QStringLiteral("设备心跳检测已停止"));
        }
        DatabaseManager::instance().insertLog(
            QStringLiteral("INFO"), QStringLiteral("connection"),
            QStringLiteral("设备心跳检测已停止"));
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
        showStatusMessage(QStringLiteral("采集已暂停"), 3000);
        if (m_logOutput) m_logOutput->appendPlainText(QStringLiteral("采集已暂停"));
        DatabaseManager::instance().insertLog(QStringLiteral("INFO"), QStringLiteral("collection"),
                                              QStringLiteral("采集已暂停"));
    } else {
        m_timer->start();
        ui->startButton->setText(QStringLiteral("暂停采集"));
        showStatusMessage(QStringLiteral("开始接收模拟设备数据"), 3000);
        if (m_logOutput) m_logOutput->appendPlainText(QStringLiteral("开始接收模拟设备数据"));
        DatabaseManager::instance().insertLog(QStringLiteral("INFO"), QStringLiteral("collection"),
                                              QStringLiteral("开始接收模拟设备数据"));
    }
}

void MainWindow::onClearAlarmsClicked()
{
    ui->alarmList->clear();
    ui->overviewAlarmList->clear();
    DatabaseManager::instance().insertLog(QStringLiteral("INFO"), QStringLiteral("alarm"),
                                          QStringLiteral("已清空告警列表"));
    updateKpi();
}

void MainWindow::updateHeartbeat()
{
    int onlineCount = 0;
    QList<HeartbeatRecord> heartbeats;
    heartbeats.reserve(m_deviceIds.size());

    for (int i = 0; i < m_deviceIds.size(); ++i) {
        const QString deviceId = m_deviceIds.at(i);
        const bool online = ((i + 1) % 10) != 0;
        const bool collecting = m_deviceCollecting.value(deviceId, true);

        m_deviceOnline[deviceId] = online;
        updateDeviceListItem(i, online, collecting);
        heartbeats.append({
            deviceId,
            QDateTime::currentDateTime(),
            online,
            collecting,
            online ? QRandomGenerator::global()->bounded(20, 90) : -1,
        });

        if (online) {
            ++onlineCount;
        }
    }

    m_onlineDeviceCount = onlineCount;

    QString databaseError;
    if (!DatabaseManager::instance().insertHeartbeatRecords(heartbeats, &databaseError)) {
        if (m_logOutput) {
            m_logOutput->appendPlainText(
                QStringLiteral("心跳数据入库失败：") + databaseError);
        }
    }

    updateDeviceControlState();
    updateSelectedChart();
    updateKpi();
}
void MainWindow::updateDemoData()
{
    ++m_tick;
    m_averageTemperature = 0.0;
    int collectingCount = 0;
    QList<TelemetryRecord> records;

    for (int i = 0; i < m_deviceIds.size(); ++i) {
        const double jitterTemp = (QRandomGenerator::global()->generateDouble() - 0.5) * 7.0;
        const double jitterPressure = (QRandomGenerator::global()->generateDouble() - 0.5) * 0.16;
        const double jitterSpeed = (QRandomGenerator::global()->generateDouble() - 0.5) * 120.0;
        const double jitterVoltage = (QRandomGenerator::global()->generateDouble() - 0.5) * 5.0;

        const QString deviceId = m_deviceIds.at(i);
        const bool online = isDeviceOnline(i);
        const bool collecting = m_deviceCollecting.value(deviceId, true);

        double temperature = (online && collecting) ? 58.0 + i * 5.0 + jitterTemp : 0.0;
        double pressure = (online && collecting) ? 1.05 + i * 0.10 + jitterPressure : 0.0;
        double speed = (online && collecting) ? 1200.0 + i * 170.0 + jitterSpeed : 0.0;
        double voltage = (online && collecting) ? 220.0 + jitterVoltage : 0.0;
        TelemetryStatus status = !online
            ? TelemetryStatus::Offline
            : (collecting ? TelemetryStatus::Online : TelemetryStatus::Stopped);
        QString alarmMessage;

        if (online && collecting && m_tick % 13 == 0 && i == 1) {
            pressure = 2.08;
            status = TelemetryStatus::Alarm;
            alarmMessage = QStringLiteral("压力超过阈值 1.80 MPa");
        }

        if (online && collecting && m_tick % 17 == 0 && i == 2) {
            temperature = 86.5;
            status = TelemetryStatus::Alarm;
            alarmMessage = QStringLiteral("温度超过阈值 80 °C");
        }

        TelemetryRecord record;
        record.deviceId = deviceId;
        record.name = m_deviceNames.at(i);
        record.status = status;
        record.temperature = temperature;
        record.pressure = pressure;
        record.speed = speed;
        record.voltage = voltage;
        record.updatedAt = QDateTime::currentDateTime();

        m_model->upsertRecord(record);
        records.append(record);

        if (online && collecting) {
            QList<double> &temperatureHistory = m_temperatureHistory[record.deviceId];
            QList<double> &pressureHistory = m_pressureHistory[record.deviceId];
            temperatureHistory.append(temperature);
            pressureHistory.append(pressure);
            while (temperatureHistory.size() > 120) temperatureHistory.removeFirst();
            while (pressureHistory.size() > 120) pressureHistory.removeFirst();
            m_averageTemperature += temperature;
            ++collectingCount;
            ++m_dataPoints;
        }

        if (!alarmMessage.isEmpty()) {
            appendAlarm(record.deviceId, alarmMessage);
        }
    }
    if (collectingCount > 0) {
        m_averageTemperature /= collectingCount;
    } else {
        m_averageTemperature = 0.0;
    }
    updateSelectedChart();
    updateDeviceControlState();

    QString databaseError;
    if (!DatabaseManager::instance().insertTelemetryRecords(records, &databaseError)) {
        if (m_logOutput) {
            m_logOutput->appendPlainText(
                QStringLiteral("遥测数据入库失败：") + databaseError);
        }
    }

    updateKpi();
}

void MainWindow::onAlarmActivated(QListWidgetItem *item)
{
    if (!item) {
        return;
    }

    const QString deviceId = item->data(Qt::UserRole).toString();
    const int row = m_deviceIds.indexOf(deviceId);
    if (row < 0) {
        showStatusMessage(
            QStringLiteral("未找到告警对应设备：%1").arg(deviceId), 4000);
        return;
    }

    ui->deviceList->setCurrentRow(row);
    ui->deviceList->scrollToItem(ui->deviceList->item(row));
    updateDeviceControlState();
    ui->mainTabs->setCurrentWidget(ui->monitorTab);
    showStatusMessage(
        QStringLiteral("已跳转到设备 %1 的实时监控界面").arg(deviceId), 3000);
}
void MainWindow::showStatusMessage(const QString &message, int timeout)
{
    const QString userText = m_userStatusLabel
        ? m_userStatusLabel->text()
        : QStringLiteral("用户：%1").arg(m_currentUser);
    const QString timestamp = QDateTime::currentDateTime()
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    statusBar()->showMessage(
        QStringLiteral("%1  |  [%2]  %3").arg(userText, timestamp, message),
        timeout);
}
void MainWindow::updateKpi()
{
    ui->onlineDevicesValue->setText(QString::number(m_onlineDeviceCount));
    ui->activeAlarmsValue->setText(QString::number(ui->alarmList->count()));
    ui->avgTemperatureValue->setText(
        m_model->recordCount() == 0
            ? QStringLiteral("-- °C")
            : QString::number(m_averageTemperature, 'f', 1) + QStringLiteral(" °C"));
    ui->dataPointsValue->setText(QString::number(m_dataPoints));
}

void MainWindow::appendAlarm(const QString &deviceId, const QString &message)
{
    const QString time = TimeUtils::toLocalIso8601();
    const QString text = QStringLiteral("[%1]  %2  %3").arg(time, deviceId, message);

    auto *item = new QListWidgetItem(text);
    item->setForeground(QColor(QStringLiteral("#b3261e")));
    item->setData(Qt::UserRole, deviceId);
    ui->alarmList->insertItem(0, item);
    if (m_logOutput) {
        m_logOutput->appendPlainText(text);
    }

    DatabaseManager::instance().insertAlarmRecord(deviceId, QStringLiteral("WARN"), message);
    DatabaseManager::instance().insertLog(QStringLiteral("WARN"), QStringLiteral("alarm"), text);

    auto *overviewItem = new QListWidgetItem(text);
    overviewItem->setForeground(QColor(QStringLiteral("#b3261e")));
    overviewItem->setData(Qt::UserRole, deviceId);
    ui->overviewAlarmList->insertItem(0, overviewItem);

    while (ui->alarmList->count() > 50) {
        delete ui->alarmList->takeItem(ui->alarmList->count() - 1);
    }
    while (ui->overviewAlarmList->count() > 8) {
        delete ui->overviewAlarmList->takeItem(ui->overviewAlarmList->count() - 1);
    }

    updateKpi();
}
