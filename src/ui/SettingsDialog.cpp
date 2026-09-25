#include "ui/SettingsDialog.h"

#include "database/DatabaseManager.h"
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
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, [this]() {
                m_statusLabel->setText(QStringLiteral("设置已应用到当前会话"));
            });

    if (QPushButton *applyButton = m_buttonBox->button(QDialogButtonBox::Apply)) {
        applyButton->setEnabled(false);
    }
    m_statusLabel->setText(QStringLiteral("数据导出页已可用；其他设置将在后续阶段接入。"));

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
    auto *protocolCombo = new QComboBox;
    protocolCombo->addItems({QStringLiteral("TCP 自定义协议"), QStringLiteral("Modbus TCP"), QStringLiteral("MQTT")});
    auto *hostEdit = new QLineEdit(QStringLiteral("127.0.0.1"));
    auto *portSpin = new QSpinBox;
    portSpin->setRange(1, 65535);
    portSpin->setValue(1234);
    auto *timeoutSpin = new QSpinBox;
    timeoutSpin->setRange(1000, 60000);
    timeoutSpin->setValue(5000);
    timeoutSpin->setSuffix(QStringLiteral(" ms"));
    auto *retrySpin = new QSpinBox;
    retrySpin->setRange(1, 60);
    retrySpin->setValue(5);
    retrySpin->setSuffix(QStringLiteral(" s"));
    auto *testButton = new QPushButton(QStringLiteral("测试连接"));
    connect(testButton, &QPushButton::clicked, this, [this]() {
        m_statusLabel->setText(QStringLiteral("连接测试将在 TCP 阶段实现"));
    });
    connectionForm->addRow(QStringLiteral("通信协议"), protocolCombo);
    connectionForm->addRow(QStringLiteral("设备地址"), hostEdit);
    connectionForm->addRow(QStringLiteral("设备端口"), portSpin);
    connectionForm->addRow(QStringLiteral("连接超时"), timeoutSpin);
    connectionForm->addRow(QStringLiteral("重连间隔"), retrySpin);
    connectionForm->addRow(QString(), testButton);
    disableUntilImplemented({protocolCombo, hostEdit, portSpin, timeoutSpin, retrySpin, testButton});
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
