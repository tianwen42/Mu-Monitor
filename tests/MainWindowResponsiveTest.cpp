#include "app/AppController.h"
#include "database/DatabaseManager.h"
#include "network/SimulationDataSource.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QBoxLayout>
#include <QGridLayout>
#include <QIcon>
#include <QListWidget>
#include <QPushButton>
#include <QPixmap>
#include <QStandardPaths>
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
    SimulationDataSource source;
    source.setSamplingInterval(1000);
    AppController controller(&source, QStringLiteral("admin"));
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
    AppController controller(&source, QStringLiteral("admin"));
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
QTEST_MAIN(MainWindowResponsiveTest)

#include "MainWindowResponsiveTest.moc"
