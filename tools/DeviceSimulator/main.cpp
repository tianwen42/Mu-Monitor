#include "DeviceSimulatorWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("Mu-Monitor DeviceSimulator"));
    application.setApplicationDisplayName(QStringLiteral("Mu-Monitor 设备模拟器"));
    application.setApplicationVersion(QStringLiteral("0.1.0"));
    application.setOrganizationName(QStringLiteral("Mu-Monitor"));
    application.setQuitOnLastWindowClosed(true);

    DeviceSimulatorWindow window;
    window.show();
    return application.exec();
}
