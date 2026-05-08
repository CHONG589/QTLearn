#include "tree/Tree.h"
#include <QtWidgets/QApplication>

#include "log/QLog.h"
#include "db/QDBConn.h"

#pragma comment(linker, "/subsystem:console /entry:mainCRTStartup")

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Logger::instance().init("", LogLevel::DEBUG, true);

    zch::Config::loadFromConfDir("./config");

    Tree window;
    window.show();

    return app.exec();
}
