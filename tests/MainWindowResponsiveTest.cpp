#include "database/DatabaseManager.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QBoxLayout>
#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QPixmap>
#include <QStandardPaths>
#include <QTableView>
#include <QToolBar>
#include <QtTest>

class MainWindowResponsiveTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void reflowsOverviewAtCompactWidth();
    void stopsDevicesIndependently();
};

void MainWindowResponsiveTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("Mu-MonitorTests"));
    QCoreApplication::setApplicationName(QStringLiteral("Mu-MonitorResponsiveTest"));

    const QString dataDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QVERIFY(!dataDirectory.isEmpty());
    QDir(dataDirectory).removeRecursively();
    QVERIFY(QDir().mkpath(dataDirectory));

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
}

void MainWindowResponsiveTest::reflowsOverviewAtCompactWidth()
{
    MainWindow window(QStringLiteral("admin"));
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    QTest::qWait(50);

    window.resize(1600, 900);
    QTest::qWait(50);
    QCOMPARE(window.size(), QSize(1600, 900));

    auto *appTitle = window.findChild<QLabel *>(QStringLiteral("appTitle"));
    auto *appSubtitle = window.findChild<QLabel *>(QStringLiteral("appSubtitle"));
    auto *headerFrame = window.findChild<QWidget *>(QStringLiteral("headerFrame"));
    auto *mainToolBar = window.findChild<QToolBar *>(QStringLiteral("mainToolBar"));
    auto *aboutAction = window.findChild<QAction *>(QStringLiteral("aboutAction"));
    QVERIFY(appTitle);
    QVERIFY(appSubtitle);
    QVERIFY(headerFrame);
    QVERIFY(mainToolBar);
    QVERIFY(aboutAction);
    QVERIFY(!appTitle->isVisible());
    QVERIFY(!appSubtitle->isVisible());
    QVERIFY(!headerFrame->isVisible());
    QVERIFY(mainToolBar->findChild<QLabel *>(QStringLiteral("connectionStatusLabel")));
    QCOMPARE(aboutAction->text(), QStringLiteral("关于 Mu-Monitor"));

    auto *kpiGrid = window.findChild<QGridLayout *>(QStringLiteral("kpiLayout"));
    auto *trendPlaceholder = window.findChild<QWidget *>(QStringLiteral("trendChartPlaceholder"));
    auto *deviceList = window.findChild<QListWidget *>(QStringLiteral("deviceList"));
    QVERIFY(deviceList);
    QCOMPARE(deviceList->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    QCOMPARE(deviceList->textElideMode(), Qt::ElideRight);
    QCOMPARE(deviceList->contextMenuPolicy(), Qt::CustomContextMenu);

    auto *deviceDock = window.findChild<QDockWidget *>(QStringLiteral("deviceDock"));
    QVERIFY(deviceDock);
    QCOMPARE(deviceDock->features(), QDockWidget::NoDockWidgetFeatures);
    QVERIFY(deviceDock->titleBarWidget());

    auto *deviceSearch = window.findChild<QLineEdit *>(QStringLiteral("deviceSearchEdit"));
    QVERIFY(deviceSearch);
    deviceSearch->setText(QStringLiteral("DEV-010"));
    QCoreApplication::processEvents();
    int visibleRows = 0;
    for (int row = 0; row < deviceList->count(); ++row) {
        visibleRows += deviceList->item(row)->isHidden() ? 0 : 1;
    }
    QCOMPARE(visibleRows, 1);
    deviceSearch->clear();
    QCoreApplication::processEvents();
    auto *alarmPanel = window.findChild<QWidget *>(QStringLiteral("overviewAlarmPanel"));
    auto *overviewTab = window.findChild<QWidget *>(QStringLiteral("overviewTab"));
    auto *monitorTab = window.findChild<QWidget *>(QStringLiteral("monitorTab"));
    QVERIFY(kpiGrid);
    QVERIFY(trendPlaceholder);
    QVERIFY(alarmPanel);
    QVERIFY(overviewTab);
    QVERIFY(monitorTab);
    QVERIFY(monitorTab->findChild<QGridLayout *>(QStringLiteral("kpiLayout")));
    QVERIFY(!overviewTab->findChild<QGridLayout *>(QStringLiteral("kpiLayout")));

    auto *userStatus = window.findChild<QLabel *>(QStringLiteral("userStatusLabel"));
    auto *headerUser = window.findChild<QLabel *>(QStringLiteral("currentUserLabel"));
    QVERIFY(userStatus);
    QVERIFY(headerUser);
    QVERIFY(userStatus->text().contains(QStringLiteral("admin")));
    QVERIFY(!headerUser->isVisible());
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
    QVERIFY(alarmPanel->y() > trendPlaceholder->y());
    QVERIFY(alarmPanel->width() >= trendPlaceholder->width() - 8);

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
    MainWindow window(QStringLiteral("admin"));
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

    auto *telemetryTable = window.findChild<QTableView *>(QStringLiteral("telemetryTable"));
    QVERIFY(telemetryTable);
    QVERIFY(QMetaObject::invokeMethod(&window, "updateDemoData", Qt::DirectConnection));
    QCoreApplication::processEvents();
    QVERIFY(telemetryTable->model()->rowCount() > 3);
    telemetryTable->selectRow(3);
    QCoreApplication::processEvents();
    QCOMPARE(deviceList->currentRow(), 3);
    QCOMPARE(telemetryTable->contextMenuPolicy(), Qt::CustomContextMenu);
}
QTEST_MAIN(MainWindowResponsiveTest)

#include "MainWindowResponsiveTest.moc"
