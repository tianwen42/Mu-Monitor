#include "ui/mainwindow.h"
#include "ui_mainwindow.h"

#include "app/AppController.h"
#include "ui/TelemetryTableModel.h"
#include "ui/TrendChartWidget.h"
#include "core/HeartbeatRecord.h"

#include "utils/TimeUtils.h"
#include "ui/SettingsDialog.h"
#include "ui/AboutDialog.h"
#include "ui/UserManagementDialog.h"
#include "auth/AuthTypes.h"

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDateTimeEdit>
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

#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QShowEvent>
#include <QSizePolicy>
#include <QWindow>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <QToolBar>
#include <QVBoxLayout>
#include <QtGlobal>

namespace {
constexpr int kAlarmEventIdRole = Qt::UserRole;
constexpr int kAlarmStateRole = Qt::UserRole + 1;
constexpr int kAlarmDeviceIdRole = Qt::UserRole + 2;

QString alarmStateLabel(AlarmState state)
{
    switch (state) {
    case AlarmState::Active:
        return QStringLiteral("活动");
    case AlarmState::Acknowledged:
        return QStringLiteral("已确认");
    case AlarmState::Cleared:
        return QStringLiteral("已恢复");
    case AlarmState::Normal:
        return QStringLiteral("正常");
    }
    return QStringLiteral("未知");
}

QString alarmItemText(const AlarmEvent &event)
{
    const QString time = event.updatedAt.isValid()
        ? event.updatedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : TimeUtils::toLocalIso8601();
    QString state = alarmStateLabel(event.state);
    if (event.state == AlarmState::Acknowledged
        && !event.acknowledgedBy.trimmed().isEmpty()) {
        state += QStringLiteral("（%1）").arg(event.acknowledgedBy);
    }
    return QStringLiteral("[%1] [%2] %3  %4")
        .arg(time, state, event.deviceId, event.message);
}

QColor alarmItemColor(AlarmState state)
{
    switch (state) {
    case AlarmState::Active:
        return QColor(QStringLiteral("#f87171"));
    case AlarmState::Acknowledged:
        return QColor(QStringLiteral("#fbbf24"));
    case AlarmState::Cleared:
        return QColor(QStringLiteral("#4ade80"));
    case AlarmState::Normal:
        return QColor(QStringLiteral("#94a3b8"));
    }
    return QColor(QStringLiteral("#94a3b8"));
}
} // namespace

MainWindow::MainWindow(AppController *controller, const QString &currentUser, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_controller(controller)
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
    setupDeviceList();
    setupHistoryPage();
    restorePersistedState();
    onConnectionStateChanged(m_controller->connectionState());
    onCollectionStateChanged(m_controller->collectionState());
    updateKpi();
}

MainWindow::~MainWindow()
{
    if (m_controller) {
        QObject::disconnect(m_controller, nullptr, this, nullptr);
        if (m_controller->connectionState() != ConnectionState::Disconnected) {
            m_controller->stop();
        }
    }
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
    const bool compact = windowWidth < 1120 || height() < 760;
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

    if (auto *logDock = findChild<QDockWidget *>(QStringLiteral("logDock"))) {
        logDock->setMinimumHeight(96);
        logDock->setMaximumHeight(compact ? 130 : 180);
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

    ui->trendChartPlaceholder->setMinimumHeight(compact ? 120 : 220);
    ui->overviewAlarmPanel->setMinimumHeight(compact ? 100 : 140);
}

void MainWindow::setupUi()
{
    menuBar()->hide();
    ui->appTitle->hide();
    ui->appSubtitle->hide();
    ui->headerFrame->hide();
    ui->alarmTitle->setText(QStringLiteral("告警记录 · 点击告警可定位设备"));

    QString role = m_controller->currentUserRole();
    if (!Auth::isValidRole(role)) {
        role = QStringLiteral("viewer");
    }
    m_canControlCollection = Auth::hasPermission(
        role, Auth::Permission::ControlCollection);
    ui->historyTab->setEnabled(Auth::hasPermission(
        role, Auth::Permission::ViewHistory));
    ui->currentUserLabel->setText(
        QStringLiteral("账号：%1（%2）").arg(m_currentUser, Auth::roleDisplayName(role)));

    ui->currentUserLabel->setProperty("fullText", ui->currentUserLabel->text());
    ui->currentUserLabel->setToolTip(
        QStringLiteral("登录时间：%1\n数据库：%2")
            .arg(TimeUtils::toLocalIso8601(m_loginTime),
                 m_controller->databasePath()));
    m_userStatusLabel = new QLabel(ui->currentUserLabel->text(), this);
    m_userStatusLabel->setObjectName(QStringLiteral("userStatusLabel"));
    m_userStatusLabel->setToolTip(ui->currentUserLabel->toolTip());
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

    m_trendChart = new TrendChartWidget(ui->trendChartPlaceholder);
    auto *chartLayout = new QVBoxLayout(ui->trendChartPlaceholder);
    chartLayout->setContentsMargins(0, 0, 0, 0);
    chartLayout->addWidget(m_trendChart);

    auto *versionLabel = new QLabel(
        QStringLiteral("v%1").arg(QApplication::applicationVersion()), this);
    versionLabel->setObjectName(QStringLiteral("versionStatusLabel"));
    versionLabel->setToolTip(QStringLiteral("Mu-Monitor 当前版本"));
    statusBar()->addPermanentWidget(versionLabel);


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
    deviceDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    deviceDock->setFeatures(QDockWidget::DockWidgetMovable
                            | QDockWidget::DockWidgetFloatable
                            | QDockWidget::DockWidgetClosable);
    ui->devicePanel->setParent(nullptr);
    deviceDock->setWidget(ui->devicePanel);
    addDockWidget(Qt::LeftDockWidgetArea, deviceDock);

    auto *logDock = new QDockWidget(QStringLiteral("运行日志 · 常驻"), this);
    logDock->setObjectName(QStringLiteral("logDock"));
    logDock->setAllowedAreas(Qt::BottomDockWidgetArea);
    logDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    logDock->setMinimumHeight(96);
    logDock->setMaximumHeight(180);
    m_logOutput = new QPlainTextEdit(logDock);
    m_logOutput->setObjectName(QStringLiteral("logOutput"));
    m_logOutput->setReadOnly(true);
    m_logOutput->setMaximumBlockCount(2000);
    m_logOutput->setMinimumHeight(64);
    m_logOutput->setMaximumHeight(142);
    m_logOutput->setPlaceholderText(QStringLiteral("系统运行日志将在这里显示..."));
    logDock->setWidget(m_logOutput);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);
    logDock->show();
    resizeDocks({logDock}, {150}, Qt::Vertical);

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
    const QString role = m_controller->currentUserRole();
    const bool canManageUsers = Auth::hasPermission(role, Auth::Permission::ManageUsers);
    const bool canAcknowledgeAlarm = Auth::hasPermission(role, Auth::Permission::AcknowledgeAlarm);
    const bool canModifySettings = Auth::hasPermission(role, Auth::Permission::ModifySettings);


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
    collectAction->setEnabled(m_canControlCollection);
    connect(collectAction, &QAction::triggered, ui->startButton, &QPushButton::click);
    toolBar->addAction(collectAction);

    QAction *refreshAction = new QAction(
        style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("刷新"), this);
    connect(refreshAction, &QAction::triggered, this, [this]() {
        restorePersistedState();
        showStatusMessage(QStringLiteral("历史数据与看板已刷新"), 2500);
    });
    toolBar->addAction(refreshAction);

    toolBar->addSeparator();

    QAction *clearAction = new QAction(
        style()->standardIcon(QStyle::SP_DialogResetButton), QStringLiteral("清空告警"), this);
    clearAction->setEnabled(canAcknowledgeAlarm);
    connect(clearAction, &QAction::triggered, ui->clearAlarmButton, &QPushButton::click);
    toolBar->addAction(clearAction);

    QAction *optionsAction = new QAction(
        style()->standardIcon(QStyle::SP_FileDialogDetailedView), QStringLiteral("设置"), this);
    optionsAction->setShortcut(QKeySequence::Preferences);
    optionsAction->setEnabled(canModifySettings);
    connect(optionsAction, &QAction::triggered, this, [this]() {
        SettingsDialog dialog(this);
        dialog.exec();
    });
    toolBar->addAction(optionsAction);

    toolBar->addSeparator();

    QAction *userManagementAction = new QAction(
        style()->standardIcon(QStyle::SP_FileDialogListView),
        QStringLiteral("用户管理"), this);
    userManagementAction->setObjectName(QStringLiteral("userManagementAction"));
    userManagementAction->setEnabled(canManageUsers);
    connect(userManagementAction, &QAction::triggered, this, [this]() {
        UserManagementDialog dialog(m_currentUser,
                                    UserManagementDialog::Mode::Management,
                                    this);
        dialog.exec();
    });
    toolBar->addAction(userManagementAction);

    QAction *changePasswordAction = new QAction(
        style()->standardIcon(QStyle::SP_DialogApplyButton),
        QStringLiteral("修改密码"), this);
    changePasswordAction->setObjectName(QStringLiteral("changePasswordAction"));
    connect(changePasswordAction, &QAction::triggered, this, [this]() {
        UserManagementDialog dialog(m_currentUser,
                                    UserManagementDialog::Mode::PasswordOnly,
                                    this);
        dialog.exec();
    });
    toolBar->addAction(changePasswordAction);

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
    collectAction->setEnabled(m_canControlCollection);
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
    connect(ui->acknowledgeAlarmButton, &QPushButton::clicked, this, &MainWindow::onAcknowledgeAlarmClicked);
    connect(ui->clearAlarmButton, &QPushButton::clicked, this, &MainWindow::onClearAlarmsClicked);
    connect(ui->alarmList, &QListWidget::itemClicked, this, &MainWindow::onAlarmActivated);
    connect(ui->overviewAlarmList, &QListWidget::itemClicked, this, &MainWindow::onAlarmActivated);
    connect(ui->deviceList, &QListWidget::currentRowChanged,
            this, &MainWindow::onDeviceSelectionChanged);
    connect(ui->startSelectedDeviceButton, &QPushButton::clicked,
            this, &MainWindow::onStartSelectedDevice);
    connect(ui->stopSelectedDeviceButton, &QPushButton::clicked,
            this, &MainWindow::onStopSelectedDevice);
    connect(m_controller, &AppController::devicesChanged,
            this, &MainWindow::onDevicesChanged);
    connect(m_controller, &AppController::telemetryBatchReceived,
            this, &MainWindow::onTelemetryBatchReceived);
    connect(m_controller, &AppController::heartbeatBatchReceived,
            this, &MainWindow::onHeartbeatBatchReceived);
    connect(m_controller, &AppController::connectionStateChanged,
            this, &MainWindow::onConnectionStateChanged);
    connect(m_controller, &AppController::collectionStateChanged,
            this, &MainWindow::onCollectionStateChanged);
    connect(m_controller, &AppController::deviceStateChanged,
            this, &MainWindow::onDeviceStateChanged);
    connect(m_controller, &AppController::onlineDeviceCountChanged,
            this, &MainWindow::onOnlineDeviceCountChanged);
    connect(m_controller,
            QOverload<const AlarmEvent &>::of(&AppController::alarmRaised),
            this, &MainWindow::onAlarmRaised);
    connect(m_controller, &AppController::alarmAcknowledged,
            this, &MainWindow::onAlarmAcknowledged);
    connect(m_controller, &AppController::alarmCleared,
            this, &MainWindow::onAlarmCleared);
    connect(m_controller, &AppController::errorOccurred,
            this, &MainWindow::onControllerError);
    ui->deviceList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->deviceList, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint &position) {
                QListWidgetItem *item = ui->deviceList->itemAt(position);
                if (!item) {
                    return;
                }

                const int index = ui->deviceList->row(item);
                ui->deviceList->setCurrentRow(index);
                QMenu menu(this);
                QAction *editAction = menu.addAction(QStringLiteral("编辑设备信息"));
                const QString deviceId = item->data(Qt::UserRole).toString();
                const bool collecting = m_deviceCollecting.value(deviceId, false);

                QAction *alarmAction = menu.addAction(QStringLiteral("查看历史报警信息"));
                menu.addSeparator();
                QAction *startAction = menu.addAction(QStringLiteral("开始设备采集"));
                QAction *stopAction = menu.addAction(QStringLiteral("停止设备采集"));
                const bool controllable =
                    m_connectionState == ConnectionState::Connected
                    && m_canControlCollection;
                startAction->setEnabled(controllable && !collecting);
                stopAction->setEnabled(controllable && collecting);                QAction *selected = menu.exec(
                    ui->deviceList->viewport()->mapToGlobal(position));

                if (selected == editAction) {
                    editDeviceInfo(index);
                } else if (selected == alarmAction) {
                    showDeviceAlarmHistory(index);
                } else if (selected == startAction) {
                    startDeviceCollection(index);
                } else if (selected == stopAction) {
                    stopDeviceCollection(index);
                }
            });
}

void MainWindow::setupHistoryPage()
{
    if (!ui->historyPanelLayout || m_historyTable) {
        return;
    }

    ui->historyHint->setText(
        QStringLiteral("按设备和时间范围查询 SQLite 历史数据；单次最多显示 2000 条。"));

    auto *filterLayout = new QHBoxLayout;
    filterLayout->setSpacing(8);

    auto *deviceLabel = new QLabel(QStringLiteral("设备"), ui->historyPanel);
    m_historyDeviceCombo = new QComboBox(ui->historyPanel);
    m_historyDeviceCombo->addItem(QStringLiteral("全部设备"), QString());
    for (int i = 0; i < m_deviceIds.size(); ++i) {
        m_historyDeviceCombo->addItem(
            QStringLiteral("%1  %2").arg(m_deviceIds.at(i), m_deviceNames.at(i)),
            m_deviceIds.at(i));
    }

    auto *startLabel = new QLabel(QStringLiteral("开始"), ui->historyPanel);
    m_historyStartEdit = new QDateTimeEdit(
        QDateTime::currentDateTime().addDays(-1), ui->historyPanel);
    m_historyStartEdit->setCalendarPopup(true);
    m_historyStartEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz"));

    auto *endLabel = new QLabel(QStringLiteral("结束"), ui->historyPanel);
    m_historyEndEdit = new QDateTimeEdit(QDateTime::currentDateTime(), ui->historyPanel);
    m_historyEndEdit->setCalendarPopup(true);
    m_historyEndEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz"));

    auto *queryButton = new QPushButton(QStringLiteral("查询"), ui->historyPanel);
    auto *recentButton = new QPushButton(QStringLiteral("最近 1000 条"), ui->historyPanel);
    m_historyCountLabel = new QLabel(QStringLiteral("暂无历史记录"), ui->historyPanel);
    m_historyCountLabel->setObjectName(QStringLiteral("historyCountLabel"));

    filterLayout->addWidget(deviceLabel);
    filterLayout->addWidget(m_historyDeviceCombo);
    filterLayout->addWidget(startLabel);
    filterLayout->addWidget(m_historyStartEdit);
    filterLayout->addWidget(endLabel);
    filterLayout->addWidget(m_historyEndEdit);
    filterLayout->addWidget(queryButton);
    filterLayout->addWidget(recentButton);
    filterLayout->addWidget(m_historyCountLabel, 1);

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

void MainWindow::rebuildHistoryDeviceCombo()
{
    if (!m_historyDeviceCombo) {
        return;
    }

    const QString selectedDeviceId =
        m_historyDeviceCombo->currentData().toString();
    m_historyDeviceCombo->blockSignals(true);
    m_historyDeviceCombo->clear();
    m_historyDeviceCombo->addItem(QStringLiteral("全部设备"), QString());
    for (int i = 0; i < m_deviceIds.size(); ++i) {
        m_historyDeviceCombo->addItem(
            QStringLiteral("%1  %2").arg(m_deviceIds.at(i), m_deviceNames.at(i)),
            m_deviceIds.at(i));
    }
    const int selectedIndex = m_historyDeviceCombo->findData(selectedDeviceId);
    m_historyDeviceCombo->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
    m_historyDeviceCombo->blockSignals(false);
}
void MainWindow::restorePersistedState()
{
    QString errorMessage;
    const QList<TelemetryRecord> latestRecords =
        m_controller->latestDeviceRecords(&errorMessage);

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

    m_dataPoints = m_controller->telemetryRecordCount(&errorMessage);

    const QList<TelemetryRecord> chartRecords =
        m_controller->recentTelemetryRecords(12000, QString(), &errorMessage);
    for (const TelemetryRecord &record : chartRecords) {
        QList<double> &temperatures = m_temperatureHistory[record.deviceId];
        QList<double> &pressures = m_pressureHistory[record.deviceId];
        temperatures.prepend(record.temperature);
        pressures.prepend(record.pressure);
        while (temperatures.size() > 120) temperatures.removeLast();
        while (pressures.size() > 120) pressures.removeLast();
    }

    const QList<HeartbeatRecord> heartbeats =
        m_controller->latestHeartbeatRecords(&errorMessage);
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

void MainWindow::loadHistoryData(bool useRange)
{
    if (!m_historyTable || !m_historyDeviceCombo) {
        return;
    }

    QString errorMessage;
    QList<TelemetryRecord> records;
    const QString deviceId = m_historyDeviceCombo->currentData().toString();

    if (useRange) {
        const QDateTime start = m_historyStartEdit->dateTime();
        const QDateTime end = m_historyEndEdit->dateTime();
        if (!start.isValid() || !end.isValid() || start > end) {
            QMessageBox::warning(
                this, QStringLiteral("时间范围错误"),
                QStringLiteral("开始时间必须早于或等于结束时间。"));
            return;
        }
        records = m_controller->telemetryHistory(
            start, end, deviceId, 2000, &errorMessage);
    } else {
        records = m_controller->recentTelemetryRecords(
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

        QColor statusColor(QStringLiteral("#94a3b8"));
        switch (record.status) {
        case TelemetryStatus::Online:
            statusColor = QColor(QStringLiteral("#22c55e"));
            break;
        case TelemetryStatus::Alarm:
        case TelemetryStatus::Offline:
            statusColor = QColor(QStringLiteral("#ef4444"));
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
void MainWindow::setupDeviceList()
{
    const QList<DeviceInfo> devices = m_controller->devices();
    onDevicesChanged(devices);
}

void MainWindow::onDevicesChanged(const QList<DeviceInfo> &devices)
{
    m_deviceIds.clear();
    m_deviceNames.clear();
    m_deviceInfos.clear();
    m_deviceCollecting.clear();
    m_deviceOnline.clear();
    ui->deviceList->clear();

    m_deviceIds.reserve(devices.size());
    m_deviceNames.reserve(devices.size());

    for (const DeviceInfo &device : devices) {
        const bool online = m_controller->isDeviceOnline(device.deviceId);
        const bool collecting = m_controller->isDeviceCollecting(device.deviceId);
        m_deviceIds << device.deviceId;
        m_deviceNames << device.name;
        m_deviceInfos.insert(device.deviceId, device);
        m_deviceOnline.insert(device.deviceId, online);
        m_deviceCollecting.insert(device.deviceId, collecting);

        auto *item = new QListWidgetItem(
            QStringLiteral("●  %1  %2").arg(device.deviceId, device.name));
        item->setData(Qt::UserRole, device.deviceId);
        item->setData(Qt::UserRole + 1, device.name);
        item->setForeground(online ? QColor(QStringLiteral("#22c55e"))
                                   : QColor(QStringLiteral("#94a3b8")));
        ui->deviceList->addItem(item);
        updateDeviceListItem(m_deviceIds.size() - 1, online, collecting);
    }

    m_onlineDeviceCount = m_controller->onlineDeviceCount();
    rebuildHistoryDeviceCombo();

    if (ui->deviceList->count() > 0) {
        ui->deviceList->setCurrentRow(0);
        onDeviceSelectionChanged(0);
    }
    updateKpi();
}
void MainWindow::updateDeviceListItem(int index, bool online, bool collecting)
{
    if (index < 0 || index >= ui->deviceList->count()) {
        return;
    }

    QColor color;
    QString status;
    if (!online) {
        color = QColor(QStringLiteral("#94a3b8"));
        status = QStringLiteral("未连接");
    } else if (!collecting) {
        color = QColor(QStringLiteral("#94a3b8"));
        status = QStringLiteral("已停止");
    } else {
        color = QColor(QStringLiteral("#22c55e"));
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
}

void MainWindow::startDeviceCollection(int index)
{
    if (!m_canControlCollection || index < 0 || index >= m_deviceIds.size()) {
        return;
    }
    m_controller->setDeviceCollection(m_deviceIds.at(index), true);
}

void MainWindow::stopDeviceCollection(int index)
{
    if (!m_canControlCollection || index < 0 || index >= m_deviceIds.size()) {
        return;
    }
    m_controller->setDeviceCollection(m_deviceIds.at(index), false);
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
    if (!m_controller->updateDeviceInfo(info, &errorMessage)) {
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

    m_controller->logEvent(
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
        m_controller->alarmHistoryForDevice(deviceId, 500, &errorMessage);

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
            ? QColor(QStringLiteral("#ef4444"))
            : QColor(QStringLiteral("#f59e0b"));
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
    const bool online = hasSelection && m_connectionState == ConnectionState::Connected && isDeviceOnline(index);
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
        m_overviewDeviceStateLabel->setStyleSheet(QStringLiteral("color:#94a3b8;"));
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

    QString stateColor = QStringLiteral("#94a3b8");
    switch (statusCode) {
    case TelemetryStatus::Alarm:
    case TelemetryStatus::Offline:
        stateColor = QStringLiteral("#f87171");
        break;
    case TelemetryStatus::Online:
        stateColor = QStringLiteral("#22c55e");
        break;
    case TelemetryStatus::Stopped:
        stateColor = QStringLiteral("#fbbf24");
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

    const bool controllable =
        m_connectionState == ConnectionState::Connected && m_canControlCollection;
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
void MainWindow::onConnectionStateChanged(ConnectionState state)
{
    m_connectionState = state;
    const bool connected = state == ConnectionState::Connected;

    if (state == ConnectionState::Connecting) {
        ui->connectionStatusLabel->setText(QStringLiteral("● 正在连接"));
        ui->connectionStatusLabel->setStyleSheet(QStringLiteral("color:#fbbf24;"));
        ui->connectButton->setText(QStringLiteral("停止检测"));
        ui->startButton->setEnabled(false);
    } else if (state == ConnectionState::Reconnecting) {
        ui->connectionStatusLabel->setText(QStringLiteral("● 正在重连"));
        ui->connectionStatusLabel->setStyleSheet(QStringLiteral("color:#fbbf24;"));
        ui->connectButton->setText(QStringLiteral("停止检测"));
        ui->startButton->setEnabled(false);
    } else if (connected) {
        ui->connectionStatusLabel->setText(QStringLiteral("● 心跳检测中"));
        ui->connectionStatusLabel->setStyleSheet(QStringLiteral("color:#22c55e;"));
        ui->connectButton->setText(QStringLiteral("停止检测"));
        ui->startButton->setEnabled(m_canControlCollection);
        showStatusMessage(
            QStringLiteral("设备心跳检测与数据采集已启动"), 4000);
        if (m_logOutput) {
            m_logOutput->appendPlainText(
                QStringLiteral("设备心跳检测与数据采集已启动"));
        }
    } else {
        ui->connectionStatusLabel->setText(QStringLiteral("● 检测已停止"));
        ui->connectionStatusLabel->setStyleSheet(QStringLiteral("color:#94a3b8;"));
        ui->connectButton->setText(QStringLiteral("开始检测"));
        ui->startButton->setEnabled(false);
        ui->startButton->setText(QStringLiteral("开始采集"));

        for (int i = 0; i < m_deviceIds.size(); ++i) {
            const QString deviceId = m_deviceIds.at(i);
            m_deviceOnline[deviceId] = false;
            updateDeviceListItem(
                i, false, m_deviceCollecting.value(deviceId, false));
        }
        m_onlineDeviceCount = 0;
        showStatusMessage(QStringLiteral("设备心跳检测已停止"), 4000);
        if (m_logOutput) {
            m_logOutput->appendPlainText(QStringLiteral("设备心跳检测已停止"));
        }
    }

    updateDeviceControlState();
    updateSelectedChart();
    updateKpi();
}

void MainWindow::onCollectionStateChanged(CollectionState state)
{
    m_collectionState = state;
    const bool running = state == CollectionState::Running;
    ui->startButton->setText(
        running ? QStringLiteral("暂停采集") : QStringLiteral("开始采集"));
    ui->startButton->setEnabled(m_connectionState == ConnectionState::Connected);
    updateDeviceControlState();
}

void MainWindow::onConnectClicked()
{
    if (m_connectionState == ConnectionState::Connected
        || m_connectionState == ConnectionState::Connecting
        || m_connectionState == ConnectionState::Reconnecting) {
        m_controller->stop();
    } else {
        m_controller->start();
    }
}

void MainWindow::onStartClicked()
{
    if (m_connectionState != ConnectionState::Connected || !m_canControlCollection) {
        return;
    }

    if (m_collectionState == CollectionState::Running) {
        m_controller->pauseCollection();
        showStatusMessage(QStringLiteral("采集已暂停"), 3000);
        if (m_logOutput) {
            m_logOutput->appendPlainText(QStringLiteral("采集已暂停"));
        }
    } else {
        m_controller->resumeCollection();
        showStatusMessage(
            QStringLiteral("开始接收模拟设备数据"), 3000);
        if (m_logOutput) {
            m_logOutput->appendPlainText(
                QStringLiteral("开始接收模拟设备数据"));
        }
    }
}void MainWindow::onClearAlarmsClicked()
{
    int removed = 0;
    for (int row = ui->alarmList->count() - 1; row >= 0; --row) {
        QListWidgetItem *item = ui->alarmList->item(row);
        const auto state = static_cast<AlarmState>(
            item->data(kAlarmStateRole).toInt());
        if (state == AlarmState::Cleared || state == AlarmState::Normal) {
            delete ui->alarmList->takeItem(row);
            ++removed;
        }
    }

    ui->overviewAlarmList->clear();
    const QList<AlarmEvent> activeEvents = m_controller->activeAlarms();
    for (const AlarmEvent &event : activeEvents) {
        auto *item = new QListWidgetItem;
        updateAlarmItem(item, event);
        ui->overviewAlarmList->addItem(item);
    }
    while (ui->overviewAlarmList->count() > 8) {
        delete ui->overviewAlarmList->takeItem(ui->overviewAlarmList->count() - 1);
    }

    if (removed > 0) {
        showStatusMessage(QStringLiteral("已清理 %1 条恢复告警记录").arg(removed), 3000);
        m_controller->logEvent(QStringLiteral("INFO"), QStringLiteral("alarm"),
                               QStringLiteral("已清理 %1 条恢复告警记录").arg(removed));
    } else {
        showStatusMessage(QStringLiteral("没有可清理的已恢复告警"), 3000);
    }
    updateKpi();
}

void MainWindow::onAcknowledgeAlarmClicked()
{
    QListWidgetItem *item = ui->alarmList->currentItem();
    if (!item && ui->overviewAlarmList->currentItem()) {
        item = ui->overviewAlarmList->currentItem();
    }
    if (!item) {
        showStatusMessage(QStringLiteral("请先选择一条活动告警"), 3000);
        return;
    }

    const QString eventId = item->data(kAlarmEventIdRole).toString();
    const auto state = static_cast<AlarmState>(item->data(kAlarmStateRole).toInt());
    if (state != AlarmState::Active) {
        showStatusMessage(QStringLiteral("所选告警已经确认或恢复"), 3000);
        return;
    }

    if (m_controller->acknowledgeAlarm(eventId)) {
        showStatusMessage(QStringLiteral("告警已确认"), 3000);
    }
}

void MainWindow::onTelemetryBatchReceived(const QList<TelemetryRecord> &records)
{
    double temperatureSum = 0.0;
    int temperatureCount = 0;

    for (const TelemetryRecord &record : records) {
        m_model->upsertRecord(record);
        if (record.status != TelemetryStatus::Online
            && record.status != TelemetryStatus::Alarm) {
            continue;
        }

        QList<double> &temperatureHistory = m_temperatureHistory[record.deviceId];
        QList<double> &pressureHistory = m_pressureHistory[record.deviceId];
        temperatureHistory.append(record.temperature);
        pressureHistory.append(record.pressure);
        while (temperatureHistory.size() > 120) {
            temperatureHistory.removeFirst();
        }
        while (pressureHistory.size() > 120) {
            pressureHistory.removeFirst();
        }

        temperatureSum += record.temperature;
        ++temperatureCount;
        ++m_dataPoints;
    }

    m_averageTemperature = temperatureCount > 0
        ? temperatureSum / temperatureCount
        : 0.0;
    updateSelectedChart();
    updateDeviceControlState();
    updateKpi();
}

void MainWindow::onHeartbeatBatchReceived(const QList<HeartbeatRecord> &heartbeats)
{
    for (const HeartbeatRecord &heartbeat : heartbeats) {
        const int index = m_deviceIds.indexOf(heartbeat.deviceId);
        if (index < 0) {
            continue;
        }
        m_deviceOnline[heartbeat.deviceId] = heartbeat.online;
        m_deviceCollecting[heartbeat.deviceId] = heartbeat.collecting;
        updateDeviceListItem(index, heartbeat.online, heartbeat.collecting);
    }

    m_onlineDeviceCount = m_controller->onlineDeviceCount();
    updateDeviceControlState();
    updateSelectedChart();
    updateKpi();
}

void MainWindow::onDeviceStateChanged(const QString &deviceId, bool online, bool collecting)
{
    const int index = m_deviceIds.indexOf(deviceId);
    if (index < 0) {
        return;
    }

    m_deviceOnline[deviceId] = online;
    m_deviceCollecting[deviceId] = collecting;
    updateDeviceListItem(index, online, collecting);
    updateDeviceControlState();
    updateSelectedChart();
    updateKpi();
}

void MainWindow::onOnlineDeviceCountChanged(int count)
{
    m_onlineDeviceCount = count;
    updateKpi();
}

void MainWindow::onAlarmRaised(const AlarmEvent &event)
{
    appendAlarm(event);
}

void MainWindow::onAlarmAcknowledged(const AlarmEvent &event)
{
    QListWidgetItem *item = findAlarmItem(event.eventId);
    if (!item) {
        appendAlarm(event);
        item = findAlarmItem(event.eventId);
    }
    if (item) {
        updateAlarmItem(item, event);
    }

    QListWidgetItem *overviewItem = findOverviewAlarmItem(event.eventId);
    if (!overviewItem) {
        overviewItem = new QListWidgetItem;
        overviewItem->setData(kAlarmEventIdRole, event.eventId);
        ui->overviewAlarmList->insertItem(0, overviewItem);
    }
    updateAlarmItem(overviewItem, event);
    updateKpi();
}

void MainWindow::onAlarmCleared(const AlarmEvent &event)
{
    QListWidgetItem *item = findAlarmItem(event.eventId);
    if (!item) {
        appendAlarm(event);
        item = findAlarmItem(event.eventId);
    }
    if (item) {
        updateAlarmItem(item, event);
    }

    if (QListWidgetItem *overviewItem = findOverviewAlarmItem(event.eventId)) {
        delete ui->overviewAlarmList->takeItem(ui->overviewAlarmList->row(overviewItem));
    }
    showStatusMessage(QStringLiteral("告警已恢复：%1").arg(event.deviceId), 4000);
    updateKpi();
}

void MainWindow::onControllerError(const QString &message)
{
    if (m_logOutput) {
        m_logOutput->appendPlainText(
            QStringLiteral("应用层错误：%1").arg(message));
    }
    showStatusMessage(message, 5000);
}
void MainWindow::onAlarmActivated(QListWidgetItem *item)
{
    if (!item) {
        return;
    }

    const QString deviceId = item->data(kAlarmDeviceIdRole).toString();
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
    ui->activeAlarmsValue->setText(QString::number(m_controller->activeAlarmCount()));
    ui->avgTemperatureValue->setText(
        m_model->recordCount() == 0
            ? QStringLiteral("-- °C")
            : QString::number(m_averageTemperature, 'f', 1) + QStringLiteral(" °C"));
    ui->dataPointsValue->setText(QString::number(m_dataPoints));
}

void MainWindow::appendAlarm(const AlarmEvent &event)
{
    auto *item = new QListWidgetItem;
    item->setData(kAlarmEventIdRole, event.eventId);
    updateAlarmItem(item, event);
    ui->alarmList->insertItem(0, item);

    if (event.state == AlarmState::Active
        || event.state == AlarmState::Acknowledged) {
        auto *overviewItem = new QListWidgetItem;
        overviewItem->setData(kAlarmEventIdRole, event.eventId);
        updateAlarmItem(overviewItem, event);
        ui->overviewAlarmList->insertItem(0, overviewItem);
    }

    if (m_logOutput) {
        m_logOutput->appendPlainText(alarmItemText(event));
    }

    while (ui->alarmList->count() > 50) {
        delete ui->alarmList->takeItem(ui->alarmList->count() - 1);
    }
    while (ui->overviewAlarmList->count() > 8) {
        delete ui->overviewAlarmList->takeItem(ui->overviewAlarmList->count() - 1);
    }

    updateKpi();
}

void MainWindow::updateAlarmItem(QListWidgetItem *item, const AlarmEvent &event)
{
    if (!item) {
        return;
    }
    item->setText(alarmItemText(event));
    item->setForeground(alarmItemColor(event.state));
    item->setData(kAlarmEventIdRole, event.eventId);
    item->setData(kAlarmStateRole, static_cast<int>(event.state));
    item->setData(kAlarmDeviceIdRole, event.deviceId);
    item->setToolTip(QStringLiteral("事件 ID：%1\n告警键：%2\n设备：%3\n状态：%4")
                         .arg(event.eventId, event.alarmKey, event.deviceId,
                              alarmStateLabel(event.state)));
}

QListWidgetItem *MainWindow::findAlarmItem(const QString &eventId) const
{
    for (int row = 0; row < ui->alarmList->count(); ++row) {
        QListWidgetItem *item = ui->alarmList->item(row);
        if (item->data(kAlarmEventIdRole).toString() == eventId) {
            return item;
        }
    }
    return nullptr;
}

QListWidgetItem *MainWindow::findOverviewAlarmItem(const QString &eventId) const
{
    for (int row = 0; row < ui->overviewAlarmList->count(); ++row) {
        QListWidgetItem *item = ui->overviewAlarmList->item(row);
        if (item->data(kAlarmEventIdRole).toString() == eventId) {
            return item;
        }
    }
    return nullptr;
}
