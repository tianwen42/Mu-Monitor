#include "app/AppController.h"
#include "database/DatabaseManager.h"
#include "network/SimulationDataSource.h"
#include "ui/LoginDialog.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QLocale>
#include <QMessageBox>
#include <QSettings>
#include <QTranslator>

#ifdef Q_OS_WIN
#include <shobjidl.h>
#endif

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName(QStringLiteral("Mu-Monitor"));
    a.setApplicationDisplayName(QStringLiteral("Mu-Monitor"));
    #if defined(MU_MONITOR_VERSION)
    a.setApplicationVersion(QStringLiteral(MU_MONITOR_VERSION));
#else
    a.setApplicationVersion(QStringLiteral("0.1.0"));
#endif
    a.setOrganizationName(QStringLiteral("Mu-Monitor"));
#ifdef Q_OS_WIN
    SetCurrentProcessExplicitAppUserModelID(L"tianwen42.MuMonitor.Industrial");
#endif
    QApplication::setQuitOnLastWindowClosed(false);
    QIcon appIcon(QStringLiteral(":/icons/mu-monitor.png"));
    if (appIcon.isNull()) {
        appIcon = QIcon(QStringLiteral(":/icons/mu-monitor.ico"));
    }
    a.setWindowIcon(appIcon);

    QFile styleFile(QStringLiteral(":/styles/app.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        a.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "Mu-Monitor_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }

    QString databaseError;
    if (!DatabaseManager::instance().initialize(&databaseError)) {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("数据库初始化失败"),
            databaseError + QStringLiteral("\n\n数据库路径：")
                + DatabaseManager::instance().databasePath());
        DatabaseManager::instance().shutdown();
        return 1;
    }

    QSettings settings;
    QString currentUser;
    bool sessionRestored = false;

    const QString rememberedToken =
        settings.value(QStringLiteral("auth/rememberToken")).toString();
    if (!rememberedToken.isEmpty()) {
        QString sessionUser;
        QString sessionError;
        sessionRestored = DatabaseManager::instance().validateRememberSession(
            rememberedToken, &sessionUser, &sessionError);
        if (sessionRestored) {
            currentUser = sessionUser;
        } else {
            settings.remove(QStringLiteral("auth/rememberToken"));
            settings.remove(QStringLiteral("auth/rememberUsername"));
        }
    }

    if (!sessionRestored) {
        LoginDialog loginDialog;
        loginDialog.setWindowIcon(appIcon);
        if (loginDialog.exec() != QDialog::Accepted) {
            DatabaseManager::instance().insertLog(
                QStringLiteral("INFO"), QStringLiteral("application"), QStringLiteral("用户取消登录"));
            DatabaseManager::instance().shutdown();
            return 0;
        }
        currentUser = loginDialog.username();
    }

    DatabaseManager::instance().insertLog(
        QStringLiteral("INFO"), QStringLiteral("application"),
        QStringLiteral("应用启动，当前用户：%1").arg(currentUser));

    int exitCode = 0;
    {
        SimulationDataSource dataSource;
        AppController controller(&dataSource, currentUser);
        MainWindow w(&controller, currentUser);
        w.setWindowIcon(appIcon);
        w.show();
        controller.start();
        exitCode = QApplication::exec();
        controller.stop();
    }
    DatabaseManager::instance().insertLog(
        QStringLiteral("INFO"), QStringLiteral("application"), QStringLiteral("应用退出"));
    DatabaseManager::instance().shutdown();
    return exitCode;
}
