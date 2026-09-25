#include "ui/SettingsDialog.h"

#include "auth/AuthTypes.h"
#include "config/DataSourceConfig.h"
#include "database/DatabaseManager.h"
#include "database/UserRepository.h"
#include "ui/UserManagementDialog.h"
#include "utils/ExcelExporter.h"
#include "utils/TimeUtils.h"

#include <QApplication>
#include <QDateTimeEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QVBoxLayout>
#include <QtGlobal>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Mu-Monitor 选项"));
    setMinimumSize(840, 560);
    resize(900, 620);
    setupUi();
    setupPages();

    connect(m_categoryList, &QListWidget::currentRowChanged,
            m_pageStack, &QStackedWidget::setCurrentIndex);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (saveDataSourceSettings()) {
            accept();
        }
    });
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, [this]() {
                saveDataSourceSettings();
            });
    m_statusLabel->setText(
        QStringLiteral("数据源配置可在“连接”页面修改，其他设置将在后续阶段接入。"));

    m_categoryList->setCurrentRow(0);
}

void SettingsDialog::setupUi()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(14, 14, 14, 14);
    rootLayout->setSpacing(10);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    m_categoryList = new QListWidget(splitter);
    m_categoryList->setObjectName(QStringLiteral("settingsCategoryList"));
    m_categoryList->setMinimumWidth(160);
    m_categoryList->setMaximumWidth(220);
    m_categoryList->setIconSize(QSize(18, 18));
    m_categoryList->setFrameShape(QFrame::NoFrame);

    m_pageStack = new QStackedWidget(splitter);
    m_pageStack->setObjectName(QStringLiteral("settingsPageStack"));

    splitter->addWidget(m_categoryList);
    splitter->addWidget(m_pageStack);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    rootLayout->addWidget(splitter);

    m_statusLabel = new QLabel(QStringLiteral("修改设置后点击“应用”保存到当前会话"), this);
    m_statusLabel->setObjectName(QStringLiteral("settingsStatusLabel"));
    rootLayout->addWidget(m_statusLabel);

    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    rootLayout->addWidget(m_buttonBox);
}

void SettingsDialog::addPage(const QString &title, const QIcon &icon, QWidget *page)
{
    auto *item = new QListWidgetItem(icon, title);
    item->setSizeHint(QSize(180, 36));
    m_categoryList->addItem(item);
    m_pageStack->addWidget(page);
}

void SettingsDialog::setupPages()
{
    const QStyle *appStyle = QApplication::style();

    auto createFormPage = []() {
        auto *page = new QWidget;
        auto *form = new QFormLayout(page);
        form->setContentsMargins(22, 22, 22, 22);
        form->setHorizontalSpacing(20);
        form->setVerticalSpacing(12);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return page;
    };

    auto disableUntilImplemented = [](const QList<QWidget *> &widgets) {
        for (QWidget *widget : widgets) {
            widget->setEnabled(false);
            widget->setToolTip(QStringLiteral("开发中，将在后续阶段接入。"));
        }
    };
    QWidget *generalPage = createFormPage();
    auto *generalForm = qobject_cast<QFormLayout *>(generalPage->layout());
    auto *languageCombo = new QComboBox;
    languageCombo->addItems({QStringLiteral("简体中文"), QStringLiteral("English")});
    auto *startPageCombo = new QComboBox;
    startPageCombo->addItems({QStringLiteral("总览"), QStringLiteral("设备管理"), QStringLiteral("实时监控")});
    auto *autoStartCheck = new QCheckBox(QStringLiteral("程序启动后自动开始采集"));
    auto *confirmExitCheck = new QCheckBox(QStringLiteral("退出前弹出确认"));
    generalForm->addRow(QStringLiteral("界面语言"), languageCombo);
    generalForm->addRow(QStringLiteral("启动页面"), startPageCombo);
    generalForm->addRow(QString(), autoStartCheck);
    generalForm->addRow(QString(), confirmExitCheck);
    disableUntilImplemented({languageCombo, startPageCombo, autoStartCheck, confirmExitCheck});
    addPage(QStringLiteral("常规"), appStyle->standardIcon(QStyle::SP_FileDialogDetailedView), generalPage);

    QWidget *connectionPage = createFormPage();
    auto *connectionForm = qobject_cast<QFormLayout *>(connectionPage->layout());
    const ApplicationConfig currentConfig = ApplicationConfig::fromSettings(QSettings());
    const DataSourceConfig &currentDataSource = currentConfig.dataSource;

    m_dataSourceTypeCombo = new QComboBox;
    m_dataSourceTypeCombo->setObjectName(QStringLiteral("dataSourceTypeCombo"));
    m_dataSourceTypeCombo->addItem(QStringLiteral("内置模拟数据"),
                                   QStringLiteral("simulation"));
    m_dataSourceTypeCombo->addItem(QStringLiteral("TCP 设备"),
                                   QStringLiteral("tcp"));
    m_dataSourceTypeCombo->setCurrentIndex(
        currentDataSource.type == DataSourceType::Tcp ? 1 : 0);

    m_hostEdit = new QLineEdit(currentDataSource.host);
    m_hostEdit->setObjectName(QStringLiteral("dataSourceHostEdit"));
    m_hostEdit->setPlaceholderText(QStringLiteral("127.0.0.1"));

    m_portSpin = new QSpinBox;
    m_portSpin->setObjectName(QStringLiteral("dataSourcePortSpin"));
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(currentDataSource.port);

    m_samplingIntervalSpin = new QSpinBox;
    m_samplingIntervalSpin->setObjectName(QStringLiteral("samplingIntervalSpin"));
    m_samplingIntervalSpin->setRange(100, 60000);
    m_samplingIntervalSpin->setValue(currentDataSource.samplingIntervalMs);
    m_samplingIntervalSpin->setSuffix(QStringLiteral(" ms"));

    m_heartbeatIntervalSpin = new QSpinBox;
    m_heartbeatIntervalSpin->setObjectName(QStringLiteral("heartbeatIntervalSpin"));
    m_heartbeatIntervalSpin->setRange(100, 60000);
    m_heartbeatIntervalSpin->setValue(currentDataSource.heartbeatIntervalMs);
    m_heartbeatIntervalSpin->setSuffix(QStringLiteral(" ms"));

    m_reconnectCheck = new QCheckBox(QStringLiteral("连接断开后自动重连"));
    m_reconnectCheck->setObjectName(QStringLiteral("reconnectEnabledCheck"));
    m_reconnectCheck->setChecked(currentDataSource.reconnectEnabled);

    m_reconnectDelaySpin = new QSpinBox;
    m_reconnectDelaySpin->setObjectName(QStringLiteral("reconnectDelaySpin"));
    m_reconnectDelaySpin->setRange(100, 60000);
    m_reconnectDelaySpin->setValue(currentDataSource.reconnectDelayMs);
    m_reconnectDelaySpin->setSuffix(QStringLiteral(" ms"));

    m_maxReconnectDelaySpin = new QSpinBox;
    m_maxReconnectDelaySpin->setObjectName(QStringLiteral("maxReconnectDelaySpin"));
    m_maxReconnectDelaySpin->setRange(100, 300000);
    m_maxReconnectDelaySpin->setValue(currentDataSource.reconnectMaxDelayMs);
    m_maxReconnectDelaySpin->setSuffix(QStringLiteral(" ms"));

    m_connectTimeoutSpin = new QSpinBox;
    m_connectTimeoutSpin->setObjectName(QStringLiteral("connectTimeoutSpin"));
    m_connectTimeoutSpin->setRange(1000, 120000);
    m_connectTimeoutSpin->setValue(currentDataSource.connectTimeoutMs);
    m_connectTimeoutSpin->setSuffix(QStringLiteral(" ms"));

    m_readTimeoutSpin = new QSpinBox;
    m_readTimeoutSpin->setObjectName(QStringLiteral("readTimeoutSpin"));
    m_readTimeoutSpin->setRange(1000, 300000);
    m_readTimeoutSpin->setValue(currentDataSource.readTimeoutMs);
    m_readTimeoutSpin->setSuffix(QStringLiteral(" ms"));

    auto *checkConfigButton = new QPushButton(QStringLiteral("检查配置"));
    connect(checkConfigButton, &QPushButton::clicked, this, [this]() {
        if (m_dataSourceTypeCombo->currentData().toString()
            == QStringLiteral("simulation")) {
            m_statusLabel->setText(QStringLiteral("内置模拟数据源配置有效。"));
            return;
        }
        if (m_hostEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(
                this, QStringLiteral("配置错误"),
                QStringLiteral("TCP 主机地址不能为空。"));
            return;
        }
        m_statusLabel->setText(
            QStringLiteral("TCP 配置格式有效。连接状态将在主界面显示。"));
    });

    connectionForm->addRow(QStringLiteral("数据源类型"), m_dataSourceTypeCombo);
    connectionForm->addRow(QStringLiteral("TCP 主机"), m_hostEdit);
    connectionForm->addRow(QStringLiteral("TCP 端口"), m_portSpin);
    connectionForm->addRow(QStringLiteral("采样周期"), m_samplingIntervalSpin);
    connectionForm->addRow(QStringLiteral("心跳周期"), m_heartbeatIntervalSpin);
    connectionForm->addRow(QString(), m_reconnectCheck);
    connectionForm->addRow(QStringLiteral("重连初始延迟"), m_reconnectDelaySpin);
    connectionForm->addRow(QStringLiteral("重连最大延迟"), m_maxReconnectDelaySpin);
    connectionForm->addRow(QStringLiteral("连接超时"), m_connectTimeoutSpin);
    connectionForm->addRow(QStringLiteral("读取超时"), m_readTimeoutSpin);
    connectionForm->addRow(QString(), checkConfigButton);

    connect(m_dataSourceTypeCombo, &QComboBox::currentIndexChanged,
            this, [this]() { updateDataSourceControls(); });
    updateDataSourceControls();

    addPage(QStringLiteral("连接"), appStyle->standardIcon(QStyle::SP_DriveNetIcon), connectionPage);

    QWidget *storagePage = createFormPage();
    auto *storageForm = qobject_cast<QFormLayout *>(storagePage->layout());
    QString defaultDbPath = DatabaseManager::instance().databasePath();
    if (defaultDbPath.isEmpty()) {
        defaultDbPath = QStandardPaths::writableLocation(
                            QStandardPaths::AppLocalDataLocation)
            + QStringLiteral("/database/mu-monitor.db");
    }
    auto *databasePathEdit = new QLineEdit(defaultDbPath);
    databasePathEdit->setObjectName(QStringLiteral("databasePathLineEdit"));
    auto *browseButton = new QPushButton(QStringLiteral("浏览..."));
    auto *retentionSpin = new QSpinBox;
    retentionSpin->setRange(1, 3650);
    retentionSpin->setValue(180);
    retentionSpin->setSuffix(QStringLiteral(" 天"));
    auto *autoCleanupCheck = new QCheckBox(QStringLiteral("自动清理过期数据"));
    connect(browseButton, &QPushButton::clicked, this, [this, databasePathEdit]() {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("选择数据库文件"), databasePathEdit->text(),
            QStringLiteral("SQLite Database (*.db *.sqlite);;All Files (*)"));
        if (!path.isEmpty()) {
            databasePathEdit->setText(path);
        }
    });
    storageForm->addRow(QStringLiteral("数据库路径"), databasePathEdit);
    storageForm->addRow(QString(), browseButton);
    storageForm->addRow(QStringLiteral("数据保留"), retentionSpin);
    storageForm->addRow(QString(), autoCleanupCheck);
    disableUntilImplemented({databasePathEdit, browseButton, retentionSpin, autoCleanupCheck});
    addPage(QStringLiteral("数据存储"), appStyle->standardIcon(QStyle::SP_DriveHDIcon), storagePage);
    QWidget *accountPage = new QWidget;
    auto *accountLayout = new QVBoxLayout(accountPage);
    accountLayout->setContentsMargins(24, 24, 24, 24);
    accountLayout->setSpacing(12);

    const QString actorUsername = qApp
        ? qApp->property(Auth::CurrentUserProperty).toString().trimmed()
        : QString();
    UserRepository userRepository;
    Auth::UserRecord actor;
    QString accountError;
    const bool actorLoaded = !actorUsername.isEmpty()
        && userRepository.findByUsername(actorUsername, &actor, &accountError);
    const QString role = actorLoaded && Auth::isValidRole(actor.role)
        ? actor.role
        : QStringLiteral("viewer");
    const bool canManageUsers = actorLoaded
        && Auth::hasPermission(role, Auth::Permission::ManageUsers);

    auto *accountSummary = new QLabel(
        actorLoaded
            ? QStringLiteral("当前账号：%1（%2）")
                  .arg(actorUsername, Auth::roleDisplayName(role))
            : QStringLiteral("当前账号：未识别。请重新登录后再管理用户。"),
        accountPage);
    accountSummary->setObjectName(QStringLiteral("accountSummaryLabel"));
    accountSummary->setWordWrap(true);
    accountLayout->addWidget(accountSummary);

    auto *permissionHint = new QLabel(
        canManageUsers
            ? QStringLiteral("当前角色可管理用户、角色和密码重置。")
            : actorLoaded
                ? QStringLiteral("当前角色不能管理用户，但可以修改本人密码。")
                : QStringLiteral("无法确定当前用户权限。"),
        accountPage);
    permissionHint->setObjectName(QStringLiteral("accountPermissionHint"));
    permissionHint->setWordWrap(true);
    accountLayout->addWidget(permissionHint);

    auto *openUserManagementButton = new QPushButton(QStringLiteral("打开用户管理"), accountPage);
    openUserManagementButton->setObjectName(QStringLiteral("openUserManagementButton"));
    openUserManagementButton->setEnabled(canManageUsers);
    openUserManagementButton->setToolTip(
        canManageUsers
            ? QStringLiteral("管理用户、角色、启用状态和密码重置。")
            : QStringLiteral("仅管理员可以打开用户管理。"));
    connect(openUserManagementButton, &QPushButton::clicked, this,
            [this, actorUsername]() {
                UserManagementDialog dialog(
                    actorUsername, UserManagementDialog::Mode::Management, this);
                dialog.exec();
            });

    auto *changeOwnPasswordButton = new QPushButton(
        QStringLiteral("修改我的密码"), accountPage);
    changeOwnPasswordButton->setObjectName(QStringLiteral("changeOwnPasswordButton"));
    changeOwnPasswordButton->setEnabled(actorLoaded);
    connect(changeOwnPasswordButton, &QPushButton::clicked, this,
            [this, actorUsername]() {
                UserManagementDialog dialog(
                    actorUsername, UserManagementDialog::Mode::PasswordOnly, this);
                dialog.exec();
            });

    accountLayout->addWidget(openUserManagementButton, 0, Qt::AlignLeft);
    accountLayout->addWidget(changeOwnPasswordButton, 0, Qt::AlignLeft);
    accountLayout->addStretch();
    addPage(QStringLiteral("用户与权限"),
            appStyle->standardIcon(QStyle::SP_FileDialogListView), accountPage);

    QWidget *alarmPage = createFormPage();
    auto *alarmForm = qobject_cast<QFormLayout *>(alarmPage->layout());
    auto *soundCheck = new QCheckBox(QStringLiteral("启用告警声音"));
    auto *popupCheck = new QCheckBox(QStringLiteral("弹窗提示严重告警"));
    auto *temperatureThresholdSpin = new QSpinBox;
    temperatureThresholdSpin->setRange(0, 300);
    temperatureThresholdSpin->setValue(80);
    temperatureThresholdSpin->setSuffix(QStringLiteral(" °C"));
    auto *debounceSpin = new QSpinBox;
    debounceSpin->setRange(0, 3600);
    debounceSpin->setValue(3);
    debounceSpin->setSuffix(QStringLiteral(" s"));
    alarmForm->addRow(QString(), soundCheck);
    alarmForm->addRow(QString(), popupCheck);
    alarmForm->addRow(QStringLiteral("温度阈值"), temperatureThresholdSpin);
    alarmForm->addRow(QStringLiteral("抖动过滤"), debounceSpin);
    disableUntilImplemented({soundCheck, popupCheck, temperatureThresholdSpin, debounceSpin});
    addPage(QStringLiteral("告警"), appStyle->standardIcon(QStyle::SP_MessageBoxWarning), alarmPage);

    QWidget *appearancePage = createFormPage();
    auto *appearanceForm = qobject_cast<QFormLayout *>(appearancePage->layout());
    auto *themeCombo = new QComboBox;
    themeCombo->addItems({QStringLiteral("系统默认"), QStringLiteral("Fusion 深色"), QStringLiteral("Fusion 浅色")});
    auto *densityCombo = new QComboBox;
    densityCombo->addItems({QStringLiteral("标准"), QStringLiteral("紧凑")});
    auto *chartPointsSpin = new QSpinBox;
    chartPointsSpin->setRange(10, 600);
    chartPointsSpin->setValue(60);
    appearanceForm->addRow(QStringLiteral("主题"), themeCombo);
    appearanceForm->addRow(QStringLiteral("界面密度"), densityCombo);
    appearanceForm->addRow(QStringLiteral("趋势点数"), chartPointsSpin);
    disableUntilImplemented({themeCombo, densityCombo, chartPointsSpin});
    addPage(QStringLiteral("外观"), appStyle->standardIcon(QStyle::SP_DesktopIcon), appearancePage);

    auto chooseExportPath = [this](const QString &description, const QString &prefix) {
        const QString defaultName = QDir::homePath()
            + QLatin1Char('/')
            + prefix
            + QStringLiteral("_")
            + TimeUtils::toFileTimestamp()
            + QStringLiteral(".xlsx");

        QString filePath = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("导出 %1").arg(description),
            defaultName,
            QStringLiteral("Excel Workbook (*.xlsx)"));

        if (!filePath.isEmpty() && !filePath.endsWith(QStringLiteral(".xlsx"), Qt::CaseInsensitive)) {
            filePath += QStringLiteral(".xlsx");
        }
        return filePath;
    };

    auto *exportPage = new QWidget;
    auto *exportLayout = new QVBoxLayout(exportPage);
    exportLayout->setContentsMargins(24, 24, 24, 24);
    exportLayout->setSpacing(14);

    auto *allDevicesGroup = new QGroupBox(QStringLiteral("全部设备信息"), exportPage);
    auto *allDevicesLayout = new QVBoxLayout(allDevicesGroup);
    auto *allDevicesHint = new QLabel(
        QStringLiteral("导出数据库中每台设备的最新状态和遥测数据。"), allDevicesGroup);
    allDevicesHint->setWordWrap(true);
    auto *exportAllButton = new QPushButton(QStringLiteral("导出全部设备信息"), allDevicesGroup);
    allDevicesLayout->addWidget(allDevicesHint);
    allDevicesLayout->addWidget(exportAllButton, 0, Qt::AlignLeft);
    exportLayout->addWidget(allDevicesGroup);

    auto *rangeGroup = new QGroupBox(QStringLiteral("按时间范围导出"), exportPage);
    auto *rangeForm = new QFormLayout(rangeGroup);
    rangeForm->setHorizontalSpacing(16);
    rangeForm->setVerticalSpacing(12);

    auto *startEdit = new QDateTimeEdit(
        QDateTime::currentDateTime().addDays(-1), rangeGroup);
    startEdit->setCalendarPopup(true);
    startEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz"));

    auto *endEdit = new QDateTimeEdit(QDateTime::currentDateTime(), rangeGroup);
    endEdit->setCalendarPopup(true);
    endEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz"));

    auto *exportRangeButton = new QPushButton(QStringLiteral("按时间导出 Excel"), rangeGroup);
    rangeForm->addRow(QStringLiteral("开始时间 (ISO 8601)"), startEdit);
    rangeForm->addRow(QStringLiteral("结束时间 (ISO 8601)"), endEdit);
    rangeForm->addRow(QString(), exportRangeButton);
    exportLayout->addWidget(rangeGroup);

    auto *exportHint = new QLabel(
        QStringLiteral("时间范围导出会包含范围内所有设备的每条遥测记录，数据量较大时可能需要等待。"),
        exportPage);
    exportHint->setWordWrap(true);
    exportHint->setObjectName(QStringLiteral("exportHintLabel"));
    exportLayout->addWidget(exportHint);
    exportLayout->addStretch();

    connect(exportAllButton, &QPushButton::clicked, this, [this, chooseExportPath]() {
        QString errorMessage;
        const QList<TelemetryRecord> records =
            DatabaseManager::instance().latestDeviceRecords(&errorMessage);
        if (records.isEmpty()) {
            QMessageBox::information(
                this,
                QStringLiteral("暂无数据"),
                errorMessage.isEmpty()
                    ? QStringLiteral("数据库中还没有设备遥测数据，请先连接设备并开始采集。")
                    : errorMessage);
            return;
        }

        const QString filePath = chooseExportPath(
            QStringLiteral("全部设备信息"), QStringLiteral("Mu-Monitor_设备信息"));
        if (filePath.isEmpty()) {
            return;
        }

        if (ExcelExporter::exportRecords(filePath, records, &errorMessage)) {
            m_statusLabel->setText(QStringLiteral("已导出 %1 台设备：%2").arg(records.size()).arg(filePath));
        } else {
            QMessageBox::warning(this, QStringLiteral("导出失败"), errorMessage);
        }
    });

    connect(exportRangeButton, &QPushButton::clicked, this, [this, startEdit, endEdit, chooseExportPath]() {
        if (startEdit->dateTime() >= endEdit->dateTime()) {
            QMessageBox::warning(
                this,
                QStringLiteral("时间范围错误"),
                QStringLiteral("开始时间必须早于结束时间。"));
            return;
        }

        QString errorMessage;
        const QList<TelemetryRecord> records =
            DatabaseManager::instance().telemetryBetween(
                startEdit->dateTime(), endEdit->dateTime(), &errorMessage);
        if (records.isEmpty()) {
            QMessageBox::information(
                this,
                QStringLiteral("暂无数据"),
                errorMessage.isEmpty()
                    ? QStringLiteral("所选时间范围内没有遥测记录。")
                    : errorMessage);
            return;
        }

        const QString filePath = chooseExportPath(
            QStringLiteral("时间范围数据"), QStringLiteral("Mu-Monitor_时间范围数据"));
        if (filePath.isEmpty()) {
            return;
        }

        if (ExcelExporter::exportRecords(filePath, records, &errorMessage)) {
            m_statusLabel->setText(QStringLiteral("已导出 %1 条记录：%2").arg(records.size()).arg(filePath));
        } else {
            QMessageBox::warning(this, QStringLiteral("导出失败"), errorMessage);
        }
    });

    addPage(QStringLiteral("数据导出"), appStyle->standardIcon(QStyle::SP_DialogSaveButton), exportPage);

    auto *aboutPage = new QWidget;
    auto *aboutLayout = new QVBoxLayout(aboutPage);
    aboutLayout->setContentsMargins(24, 24, 24, 24);
    auto *aboutLabel = new QLabel(
        QStringLiteral("<h3>Mu-Monitor</h3>"
                       "<p>Industrial Equipment Monitoring &amp; Alarm Console</p>"
                       "<p>Version: %1<br/>Qt Runtime: %2</p>"
                       "<p>当前阶段：界面原型 + 模拟数据</p>")
            .arg(QApplication::applicationVersion(), QString::fromLatin1(qVersion())));
    aboutLabel->setWordWrap(true);
    aboutLayout->addWidget(aboutLabel);
    aboutLayout->addStretch();
    addPage(QStringLiteral("关于"), appStyle->standardIcon(QStyle::SP_MessageBoxInformation), aboutPage);
}
bool SettingsDialog::saveDataSourceSettings()
{
    QSettings settings;
    ApplicationConfig config = ApplicationConfig::fromSettings(settings);
    DataSourceConfig &dataSource = config.dataSource;

    const bool tcpSelected =
        m_dataSourceTypeCombo->currentData().toString() == QStringLiteral("tcp");
    dataSource.type = tcpSelected ? DataSourceType::Tcp
                                  : DataSourceType::Simulation;
    dataSource.host = m_hostEdit->text().trimmed();
    dataSource.port = static_cast<quint16>(m_portSpin->value());
    dataSource.samplingIntervalMs = m_samplingIntervalSpin->value();
    dataSource.heartbeatIntervalMs = m_heartbeatIntervalSpin->value();
    dataSource.reconnectEnabled = m_reconnectCheck->isChecked();
    dataSource.reconnectDelayMs = m_reconnectDelaySpin->value();
    dataSource.reconnectMaxDelayMs = m_maxReconnectDelaySpin->value();
    dataSource.connectTimeoutMs = m_connectTimeoutSpin->value();
    dataSource.readTimeoutMs = m_readTimeoutSpin->value();

    QString errorMessage;
    if (!dataSource.isValid(&errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("数据源配置错误"), errorMessage);
        return false;
    }

    config.save(settings);
    settings.sync();
    m_statusLabel->setText(
        QStringLiteral("数据源配置已保存，重启 Mu-Monitor 后生效。"));
    return true;
}

void SettingsDialog::updateDataSourceControls()
{
    const bool tcpSelected =
        m_dataSourceTypeCombo->currentData().toString() == QStringLiteral("tcp");
    const bool reconnectEnabled = tcpSelected && m_reconnectCheck->isChecked();

    m_hostEdit->setEnabled(tcpSelected);
    m_portSpin->setEnabled(tcpSelected);
    m_connectTimeoutSpin->setEnabled(tcpSelected);
    m_readTimeoutSpin->setEnabled(tcpSelected);
    m_reconnectCheck->setEnabled(tcpSelected);
    m_reconnectDelaySpin->setEnabled(reconnectEnabled);
    m_maxReconnectDelaySpin->setEnabled(reconnectEnabled);

    m_samplingIntervalSpin->setEnabled(true);
    m_heartbeatIntervalSpin->setEnabled(true);
}