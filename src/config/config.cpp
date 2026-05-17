/**
 * @file config.cpp
 * @brief 配置模块实现（Qt 移植版）
 * @author zch
 * @date 2026-05-05
 */

#include "config.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QMutexLocker>
#include <sstream>

static zch::Logger::ptr g_logger = LOG_NAME("default");

namespace zch {

// ============================================================
// 内部辅助
// ============================================================

/**
 * @brief 递归展开 YAML 节点树，将叶子节点路径展平为 "a.b.c" 形式的 key
 * @param[in] prefix 当前 key 前缀
 * @param[in] node 当前 YAML 节点
 * @param[out] output 展平后的 (key, node) 列表
 */
static void listAllMember(const QString &prefix, const YAML::Node &node,
                          QList<QPair<QString, YAML::Node>> &output) {
    QRegularExpression re("[^abcdefghikjlmnopqrstuvwxyz._012345678]");
    QRegularExpressionMatch match = re.match(prefix);
    if (match.hasMatch()) {
        LOG_ERROR(g_logger) << "Config invalid name: " << prefix;
        return;
    }

    output.append(QPair<QString, YAML::Node>(prefix, node));
    if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            QString key = QString::fromStdString(it->first.Scalar());
            listAllMember(prefix.isEmpty() ? key : prefix + "." + key, it->second, output);
        }
    }
}

// ============================================================
// Config 静态方法实现
// ============================================================

/**
 * @brief 查找配置参数基类
 * @param[in] name 配置参数名称
 * @return 找到返回对应指针，否则返回空
 */
ConfigVarBase::ptr Config::lookupBase(const QString &name) {
    QReadLocker lock(&getMutex());
    auto it = getDatas().find(name.toLower());
    return it == getDatas().end() ? ConfigVarBase::ptr() : it.value();
}

/**
 * @brief 使用 YAML::Node 初始化配置模块
 * @param[in] root YAML 根节点
 * @details 将 YAML 树展平为 key-value 对，逐个查找已注册的 ConfigVar 并赋值。
 *          未注册的 key 被忽略（允许 YAML 中存在暂未定义配置项的字段）。
 */
void Config::loadFromYaml(const YAML::Node &root) {
    QList<QPair<QString, YAML::Node>> allNodes;
    listAllMember("", root, allNodes);

    for (auto &i : allNodes) {
        QString key = i.first;
        if (key.isEmpty()) {
            continue;
        }

        key = key.toLower();
        ConfigVarBase::ptr var = lookupBase(key);

        if (var) {
            if (i.second.IsScalar()) {
                var->fromString(QString::fromStdString(i.second.Scalar()));
            } else {
                std::stringstream ss;
                ss << i.second;
                var->fromString(QString::fromStdString(ss.str()));
            }
        }
    }
}

// 记录每个文件的修改时间
static QMap<QString, quint64> s_file2modifytime;
// 保护 s_file2modifytime 的互斥锁
static QMutex s_mutex;

/**
 * @brief 从目录加载所有 .yml 配置文件
 * @param[in] path 配置文件目录路径
 * @param[in] force 是否强制重新加载（忽略修改时间检查）
 * @details 遍历目录下所有 .yml 文件，跳过修改时间未变化的文件（非强制模式）。
 *          使用 QFileInfo::lastModified() 替代 POSIX stat，适配 Windows 平台。
 */
void Config::loadFromConfDir(const QString &path, bool force) {
    QDirIterator it(path, QStringList() << "*.yml", QDir::Files);

    while (it.hasNext()) {
        it.next();
        QString filePath = QFileInfo(it.filePath()).absoluteFilePath();

        {
            QFileInfo fi(filePath);
            quint64 modTime = static_cast<quint64>(fi.lastModified().toSecsSinceEpoch());
            QMutexLocker lock(&s_mutex);
            if (!force && s_file2modifytime.value(filePath) == modTime) {
                continue;
            }
            s_file2modifytime[filePath] = modTime;
        }

        try {
            YAML::Node root = YAML::LoadFile(filePath.toStdString());
            loadFromYaml(root);
            LOG_INFO(g_logger) << "LoadConfFile file=" << filePath << " ok";
        } catch (const YAML::BadFile &e) {
            LOG_ERROR(g_logger) << "LoadConfFile file=" << filePath << " not found: " << e.what();
        } catch (const std::exception &e) {
            LOG_ERROR(g_logger) << "LoadConfFile file=" << filePath << " failed: " << e.what();
        }
    }
}

/**
 * @brief 遍历所有配置项
 * @param[in] cb 回调函数，对每个 ConfigVarBase 执行一次
 */
void Config::visit(std::function<void(ConfigVarBase::ptr)> cb) {
    QReadLocker lock(&getMutex());
    ConfigVarMap &m = getDatas();
    for (auto it = m.begin(); it != m.end(); ++it) {
        cb(it.value());
    }
}

}  // namespace zch
