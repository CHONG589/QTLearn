#include "tree/Tree.h"
#include <QtWidgets/QApplication>

#include "db/QDBConn.h"
#include "crypto/Crypto.h"
#include "log/log.h"

#pragma comment(linker, "/subsystem:console /entry:mainCRTStartup")

static zch::Logger::ptr g_logger = LOG_NAME("default");

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    zch::Config::loadFromConfDir("./config");
    if (!(Crypto::loadKey("config/db.key"))) {
        LOG_ERROR(g_logger) << "加载密钥失败";
        return 1;
    }

    LOG_INFO(g_logger) << "开始构造";

    Tree window;
    window.show();

    return app.exec();
}
