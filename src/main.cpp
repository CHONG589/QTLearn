#include "tree/Tree.h"
#include <QtWidgets/QApplication>

#include "log/QLog.h"
#include "db/QDBConn.h"

#pragma comment(linker, "/subsystem:console /entry:mainCRTStartup")

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Logger::instance().init("", LogLevel::DEBUG, true);

    DBConfig config;
    config.dbName = "";
    config.user = "";
    config.password = "";
    DBPool::instance().init(config);

    Tree window;
    window.show();

    return app.exec();
}
