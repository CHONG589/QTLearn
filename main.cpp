#include "Tree.h"
#include <QtWidgets/QApplication>

#include "QLog.h"
#include "QDBConn.h"

#pragma comment(linker, "/subsystem:console /entry:mainCRTStartup")

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Logger::instance().init("./logs", LogLevel::DEBUG, true);

    DBConfig config;
    config.dbName = "learn";
    config.user = "root";
    config.password = "589520";
    DBPool::instance().init(config);

    Tree window;
    window.show();

    return app.exec();
}
