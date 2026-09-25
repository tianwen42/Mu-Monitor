#include "app/AppController.h"
#include "config/DataSourceConfig.h"
#include "database/DatabaseManager.h"
#include "database/SqliteTelemetryRepository.h"
#include "network/SimulationDataSource.h"
#include "auth/AuditRepository.h"
#include "auth/AuthTypes.h"
#include "auth/PasswordService.h"
#include "auth/UserManagementService.h"
#include "database/DatabaseManager.h"
#include "database/PasswordRepository.h"
#include "database/UserRepository.h"
#include "ui/SettingsDialog.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QBoxLayout>
#include <QGridLayout>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QPixmap>
#include <QPointer>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

class MainWindowResponsiveTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void reflowsOverviewAtCompactWidth();
    void stopsDevicesIndependently();
    void destroyingWindowStopsController();
    void settingsDialogShowsUserManagementPermissionsAndDatabasePath();
    void settingsDialogPersistsDataSourceConfiguration();

private:
    std::unique_ptr<QTemporaryDir> m_tempDirectory;
};

void MainWindowResponsiveTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("Mu-MonitorTests"));
    QCoreApplication::setApplicationName(QStringLiteral("Mu-MonitorResponsiveTest"));

    m_tempDirectory = std::make_unique<QTemporaryDir>(
        QDir::tempPath() + QStringLiteral("/Mu-MonitorResponsiveTest-XXXXXX"));
    QVERIFY(m_tempDirectory->isValid());
    qputenv("MU_MONITOR_DATA_DIR", m_tempDirectory->path().toUtf8());

    QString errorMessage;
    QVERIFY2(DatabaseManager::instance().initialize(&errorMessage),
             qPrintable(errorMessage));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/mu-monitor.ico")));

    QFile styleFile(QStringLiteral(":/styles/app.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qApp->setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }
}

void MainWindowResponsiveTest::cleanupTestCase()
{
    DatabaseManager::instance().shutdown();
    qunsetenv("MU_MONITOR_DATA_DIR");
    m_tempDirectory.reset();
}

void MainWindowResponsiveTest::reflowsOverviewAtCompactWidth()
{
    SimulationDataSource source;
    source.setSamplingInterval(1000);
    SqliteTelemetryRepository repository(DatabaseManager::instance().databasePath());
    QString repositoryError;
    QVERIFY2(repository.start(&repositoryError), qPrintable(repositoryError));
    AppController controller(&source, &repository, QStringLiteral("admin"));
    QVERIFY(controller.start());
    MainWindow window(&controller, QStringLiteral("admin"));
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    QTest::qWait(50);

    window.resize(1600, 900);
    QTest::qWait(50);
    QCOMPARE(window.size(), QSize(1600, 900));

    auto *appTitle = window.findChild<QWidget *>(QStringLiteral("appTitle"));
    QVERIFY(appTitle);
    QVERIFY(!appTitle->isVisible());

    auto *kpiGrid = window.findChild<QGridLayout *>(QStringLiteral("kpiLayout"));
    auto *trendPlaceholder = window.findChild<QWidget *>(QStringLiteral("trendChartPlaceholder"));
    auto *alarmPanel = window.findChild<QWidget *>(QStringLiteral("overviewAlarmPanel"));
    QVERIFY(kpiGrid);
    QVERIFY(trendPlaceholder);
    QVERIFY(alarmPanel);
    QVERIFY(kpiGrid->itemAtPosition(0, 3));
    QVERIFY(trendPlaceholder->width() > 0);
    QVERIFY(alarmPanel->width() > 0);
    const QPixmap wideCapture = window.grab();

    window.resize(960, 640);
    QTest::qWait(100);
    QCOMPARE(window.size(), QSize(960, 640));

    QCOMPARE(kpiGrid->property("responsiveColumns").toInt(), 2);
    QVERIFY(kpiGrid->itemAtPosition(0, 0));
    QVERIFY(kpiGrid->itemAtPosition(0, 1));
    QVERIFY(kpiGrid->itemAtPosition(1, 0));
    QVERIFY(kpiGrid->itemAtPosition(1, 1));
    QVERIFY(!kpiGrid->itemAtPosition(0, 2));

    auto *lowerLayout = window.findChild<QBoxLayout *>(QStringLiteral("overviewLowerLayout"));
    QVERIFY(lowerLayout);
    QCOMPARE(lowerLayout->direction(), QBoxLayout::TopToBottom);
    QVERIFY(trendPlaceholder->width() > 0);
    QVERIFY(alarmPanel->width() > 0);

    const QString outputDirectory = qEnvironmentVariable("MU_RESPONSIVE_SCREENSHOT_DIR");
    if (!outputDirectory.isEmpty()) {
        QDir directory(outputDirectory);
        QVERIFY(directory.mkpath(QStringLiteral(".")));
        QVERIFY(wideCapture.save(directory.filePath(QStringLiteral("mainwindow-wide.png"))));
        QVERIFY(window.grab().save(directory.filePath(QStringLiteral("mainwindow-compact.png"))));
    }
}

void MainWindowResponsiveTest::stopsDevicesIndependently()
{
    SimulationDataSource source;
    source.setSamplingInterval(1000);
    SqliteTelemetryRepository repository(DatabaseManager::instance().databasePath());
    QString repositoryError;
    QVERIFY2(repository.start(&repositoryError), qPrintable(repositoryError));
    AppController controller(&source, &repository, QStringLiteral("admin"));
    QVERIFY(controller.start());
    MainWindow window(&controller, QStringLiteral("admin"));
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    QTest::qWait(50);

    auto *deviceList = window.findChild<QListWidget *>(QStringLiteral("deviceList"));
    auto *startButton = window.findChild<QPushButton *>(QStringLiteral("startSelectedDeviceButton"));
    auto *stopButton = window.findChild<QPushButton *>(QStringLiteral("stopSelectedDeviceButton"));
    QVERIFY(deviceList);
    QVERIFY(startButton);
    QVERIFY(stopButton);

    deviceList->setCurrentRow(0);
    QCoreApplication::processEvents();
    QVERIFY(stopButton->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(&window, "onStopSelectedDevice", Qt::DirectConnection));
    QVERIFY(startButton->isEnabled());
    QVERIFY(!stopButton->isEnabled());

    deviceList->setCurrentRow(1);
    QCoreApplication::processEvents();
    QVERIFY2(stopButton->isEnabled(), "Stopping one device must not disable control of other devices");
    QVERIFY(QMetaObject::invokeMethod(&window, "onStopSelectedDevice", Qt::DirectConnection));
    QVERIFY(!stopButton->isEnabled());

    deviceList->setCurrentRow(2);
    QCoreApplication::processEvents();
    QVERIFY2(stopButton->isEnabled(), "A second stopped device must not affect a third device");
}
void MainWindowResponsiveTest::destroyingWindowStopsController()
{
    SqliteTelemetryRepository repository(DatabaseManager::instance().databasePath());
    QString repositoryError;
    QVERIFY2(repository.start(&repositoryError), qPrintable(repositoryError));

    auto *source = new SimulationDataSource;
    source->setSamplingInterval(1000);
    source->setHeartbeatInterval(1000);

    {
        AppController controller(source, &repository, QStringLiteral("admin"));
        QVERIFY(controller.start());

        auto *window = new MainWindow(&controller, QStringLiteral("admin"));
        window->setAttribute(Qt::WA_DontShowOnScreen);
        window->show();
        QCoreApplication::processEvents();

        QPointer<MainWindow> windowPointer(window);
        delete window;
        QVERIFY(windowPointer.isNull());
        QVERIFY(!source->isRunning());

        source->triggerTelemetry();
        QCoreApplication::processEvents();
    }

    QVERIFY(!source->isRunning());
    repository.shutdown();
    delete source;
}


void MainWindowResponsiveTest::settingsDialogShowsUserManagementPermissionsAndDatabasePath()
{
    qApp->setProperty(Auth::CurrentUserProperty, QStringLiteral("admin"));

    SettingsDialog adminDialog;
    auto *adminPathEdit =
        adminDialog.findChild<QLineEdit *>(QStringLiteral("databasePathLineEdit"));
    auto *adminUserManagementButton =
        adminDialog.findChild<QPushButton *>(QStringLiteral("openUserManagementButton"));
    auto *adminPasswordButton =
        adminDialog.findChild<QPushButton *>(QStringLiteral("changeOwnPasswordButton"));
    QVERIFY(adminPathEdit);
    QCOMPARE(adminPathEdit->text(), DatabaseManager::instance().databasePath());
    QVERIFY(adminUserManagementButton);
    QVERIFY(adminUserManagementButton->isEnabled());
    QVERIFY(adminPasswordButton);
    QVERIFY(adminPasswordButton->isEnabled());

    UserRepository users;
    PasswordRepository passwords;
    PasswordService passwordService;
    AuditRepository audit;
    UserManagementService service(users, passwords, passwordService, audit,
                                  QStringLiteral("admin"));

    QString errorMessage;
    QVERIFY2(service.createUser(QStringLiteral("ui_operator"),
                               QStringLiteral("UI Operator"),
                               QStringLiteral("operator"),
                               QStringLiteral("Operator123"),
                               true, nullptr, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(service.createUser(QStringLiteral("ui_viewer"),
                               QStringLiteral("UI Viewer"),
                               QStringLiteral("viewer"),
                               QStringLiteral("Viewer123"),
                               true, nullptr, &errorMessage),
             qPrintable(errorMessage));

    qApp->setProperty(Auth::CurrentUserProperty, QStringLiteral("ui_operator"));
    SettingsDialog operatorDialog;
    auto *operatorUserManagementButton =
        operatorDialog.findChild<QPushButton *>(QStringLiteral("openUserManagementButton"));
    auto *operatorPasswordButton =
        operatorDialog.findChild<QPushButton *>(QStringLiteral("changeOwnPasswordButton"));
    QVERIFY(operatorUserManagementButton);
    QVERIFY(!operatorUserManagementButton->isEnabled());
    QVERIFY(operatorPasswordButton);
    QVERIFY(operatorPasswordButton->isEnabled());

    qApp->setProperty(Auth::CurrentUserProperty, QStringLiteral("ui_viewer"));
    SettingsDialog viewerDialog;
    auto *viewerUserManagementButton =
        viewerDialog.findChild<QPushButton *>(QStringLiteral("openUserManagementButton"));
    auto *viewerPasswordButton =
        viewerDialog.findChild<QPushButton *>(QStringLiteral("changeOwnPasswordButton"));
    QVERIFY(viewerUserManagementButton);
    QVERIFY(!viewerUserManagementButton->isEnabled());
    QVERIFY(viewerPasswordButton);
    QVERIFY(viewerPasswordButton->isEnabled());

    qApp->setProperty(Auth::CurrentUserProperty, QStringLiteral("admin"));
}

void MainWindowResponsiveTest::settingsDialogPersistsDataSourceConfiguration()
{
    QSettings settings;
    settings.remove(QStringLiteral("dataSource"));
    settings.sync();

    SettingsDialog dialog;
    auto *typeCombo = dialog.findChild<QComboBox *>(QStringLiteral("dataSourceTypeCombo"));
    auto *hostEdit = dialog.findChild<QLineEdit *>(QStringLiteral("dataSourceHostEdit"));
    auto *portSpin = dialog.findChild<QSpinBox *>(QStringLiteral("dataSourcePortSpin"));
    auto *samplingSpin = dialog.findChild<QSpinBox *>(QStringLiteral("samplingIntervalSpin"));
    auto *heartbeatSpin = dialog.findChild<QSpinBox *>(QStringLiteral("heartbeatIntervalSpin"));
    auto *reconnectCheck = dialog.findChild<QCheckBox *>(QStringLiteral("reconnectEnabledCheck"));
    auto *reconnectDelaySpin = dialog.findChild<QSpinBox *>(QStringLiteral("reconnectDelaySpin"));
    auto *maxReconnectDelaySpin = dialog.findChild<QSpinBox *>(QStringLiteral("maxReconnectDelaySpin"));
    auto *connectTimeoutSpin = dialog.findChild<QSpinBox *>(QStringLiteral("connectTimeoutSpin"));
    auto *readTimeoutSpin = dialog.findChild<QSpinBox *>(QStringLiteral("readTimeoutSpin"));
    auto *buttons = dialog.findChild<QDialogButtonBox *>();

    QVERIFY(typeCombo);
    QVERIFY(hostEdit);
    QVERIFY(portSpin);
    QVERIFY(samplingSpin);
    QVERIFY(heartbeatSpin);
    QVERIFY(reconnectCheck);
    QVERIFY(reconnectDelaySpin);
    QVERIFY(maxReconnectDelaySpin);
    QVERIFY(connectTimeoutSpin);
    QVERIFY(readTimeoutSpin);
    QVERIFY(buttons);

    typeCombo->setCurrentIndex(typeCombo->findData(QStringLiteral("tcp")));
    hostEdit->setText(QStringLiteral("10.0.0.25"));
    portSpin->setValue(45454);
    samplingSpin->setValue(2000);
    heartbeatSpin->setValue(4000);
    reconnectCheck->setChecked(true);
    reconnectDelaySpin->setValue(1500);
    maxReconnectDelaySpin->setValue(12000);
    connectTimeoutSpin->setValue(6000);
    readTimeoutSpin->setValue(20000);

    QPushButton *applyButton = buttons->button(QDialogButtonBox::Apply);
    QVERIFY(applyButton);
    applyButton->click();
    QCoreApplication::processEvents();

    const ApplicationConfig config = ApplicationConfig::fromSettings(QSettings());
    QCOMPARE(int(config.dataSource.type), int(DataSourceType::Tcp));
    QCOMPARE(config.dataSource.host, QStringLiteral("10.0.0.25"));
    QCOMPARE(config.dataSource.port, quint16(45454));
    QCOMPARE(config.dataSource.samplingIntervalMs, 2000);
    QCOMPARE(config.dataSource.heartbeatIntervalMs, 4000);
    QVERIFY(config.dataSource.reconnectEnabled);
    QCOMPARE(config.dataSource.reconnectDelayMs, 1500);
    QCOMPARE(config.dataSource.reconnectMaxDelayMs, 12000);
    QCOMPARE(config.dataSource.connectTimeoutMs, 6000);
    QCOMPARE(config.dataSource.readTimeoutMs, 20000);
}
QTEST_MAIN(MainWindowResponsiveTest)

#include "MainWindowResponsiveTest.moc"
