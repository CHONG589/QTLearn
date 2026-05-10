#include "tree/Tree.h"
#include <QtWidgets/QApplication>

#include "log/QLog.h"
#include "db/QDBConn.h"
#include "crypto/Crypto.h"

#pragma comment(linker, "/subsystem:console /entry:mainCRTStartup")

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Logger::instance().init("", LogLevel::DEBUG, true);

    zch::Config::loadFromConfDir("./config");
    if (!(Crypto::loadKey("config/db.key"))) {
        LOG_ERROR() << "加载密钥失败";
        return 1;
    }

    Tree window;
    window.show();

    return app.exec();
}
