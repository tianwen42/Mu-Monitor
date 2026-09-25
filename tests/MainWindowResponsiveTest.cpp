#include "app/AppController.h"
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
    QCOMPARE(lowerLayout->direction(), QBoxLayout::LeftToRight);
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
QTEST_MAIN(MainWindowResponsiveTest)

#include "MainWindowResponsiveTest.moc"
