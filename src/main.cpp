#include "database/DatabaseManager.h"
#include "ui/LoginDialog.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QFile>
#include <QLocale>
#include <QMessageBox>
#include <QTranslator>

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

    LoginDialog loginDialog;
    if (loginDialog.exec() != QDialog::Accepted) {
        DatabaseManager::instance().shutdown();
        return 0;
    }

    int exitCode = 0;
    {
        MainWindow w;
        w.show();
        exitCode = QApplication::exec();
    }

    DatabaseManager::instance().shutdown();
    return exitCode;
}
