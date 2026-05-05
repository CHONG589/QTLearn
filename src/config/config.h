/**
 * @file config.h
 * @brief 配置模块（Qt 移植版，基于 yaml-cpp）
 * @author zch
 * @date 2026-05-05
 */

#ifndef CONFIG_H__
#define CONFIG_H__

#include <yaml-cpp/yaml.h>

#include <QHash>
#include <QList>
#include <QMap>
#include <QReadWriteLock>
#include <QReadLocker>
#include <QWriteLocker>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QSharedPointer>
#include <QString>
#include <QVector>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <typeinfo>
#include <unordered_set>
#include <QRegularExpression>

namespace zch {

// ============================================================
// LexicalCast — 类型转换体系（std::string ↔ 各类容器）
// yaml-cpp 原生使用 std::string，故内部序列化层保留 std::string
// ============================================================

/**
 * @brief 类型转换模板类(F 源类型, T 目标类型)
 */
template <class F, class T>
class LexicalCast {
public:
    /**
     * @brief 类型转换
     * @param[in] v 源类型值
     * @return 返回v转换后的目标类型
     * @exception 当类型不可转换时抛出异常
     */
    T operator()(const F &v) {
        T ret;
        std::stringstream ss;
        ss << v;
        ss >> ret;
        return ret;
    }
};

/**
 * @brief 类型转换模板类全特化(std::string 转换成 QString)
 */
template <>
class LexicalCast<std::string, QString> {
public:
    QString operator()(const std::string &v) { return QString::fromStdString(v); }
};

/**
 * @brief 类型转换模板类全特化(QString 转换成 std::string)
 */
template <>
class LexicalCast<QString, std::string> {
public:
    std::string operator()(const QString &v) { return v.toStdString(); }
};

/**
 * @brief 类型转换模板类偏特化(std::string 转换成 std::vector<T>)
 */
template <class T>
class LexicalCast<std::string, std::vector<T>> {
public:
    std::vector<T> operator()(const std::string &v) {
        YAML::Node node = YAML::Load(v);
        typename std::vector<T> vec;
        std::stringstream ss;
        for (size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.push_back(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

/**
 * @brief 类型转换模板类偏特化(std::vector<T> 转换成 std::string)
 */
template <class T>
class LexicalCast<std::vector<T>, std::string> {
public:
    std::string operator()(const std::vector<T> &v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for (auto &i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief 类型转换模板类偏特化(std::string 转换成 std::list<T>)
 */
template <class T>
class LexicalCast<std::string, std::list<T>> {
public:
    std::list<T> operator()(const std::string &v) {
        YAML::Node node = YAML::Load(v);
        typename std::list<T> vec;
        std::stringstream ss;
        for (size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.push_back(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

/**
 * @brief 类型转换模板类偏特化(std::list<T> 转换成 std::string)
 */
template <class T>
class LexicalCast<std::list<T>, std::string> {
public:
    std::string operator()(const std::list<T> &v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for (auto &i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief 类型转换模板类偏特化(std::string 转换成 std::set<T>)
 */
template <class T>
class LexicalCast<std::string, std::set<T>> {
public:
    std::set<T> operator()(const std::string &v) {
        YAML::Node node = YAML::Load(v);
        typename std::set<T> vec;
        std::stringstream ss;
        for (size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.insert(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

/**
 * @brief 类型转换模板类偏特化(std::set<T> 转换成 std::string)
 */
template <class T>
class LexicalCast<std::set<T>, std::string> {
public:
    std::string operator()(const std::set<T> &v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for (auto &i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief 类型转换模板类偏特化(std::string 转换成 std::unordered_set<T>)
 */
template <class T>
class LexicalCast<std::string, std::unordered_set<T>> {
public:
    std::unordered_set<T> operator()(const std::string &v) {
        YAML::Node node = YAML::Load(v);
        typename std::unordered_set<T> vec;
        std::stringstream ss;
        for (size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.insert(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

/**
 * @brief 类型转换模板类偏特化(std::unordered_set<T> 转换成 std::string)
 */
template <class T>
class LexicalCast<std::unordered_set<T>, std::string> {
public:
    std::string operator()(const std::unordered_set<T> &v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for (auto &i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief 类型转换模板类偏特化(std::string 转换成 std::map<std::string, T>)
 */
template <class T>
class LexicalCast<std::string, std::map<std::string, T>> {
public:
    std::map<std::string, T> operator()(const std::string &v) {
        YAML::Node node = YAML::Load(v);
        typename std::map<std::string, T> vec;
        std::stringstream ss;
        for (auto it = node.begin(); it != node.end(); ++it) {
            ss.str("");
            ss << it->second;
            vec.insert(std::make_pair(it->first.Scalar(), LexicalCast<std::string, T>()(ss.str())));
        }
        return vec;
    }
};

/**
 * @brief 类型转换模板类偏特化(std::map<std::string, T> 转换成 std::string)
 */
template <class T>
class LexicalCast<std::map<std::string, T>, std::string> {
public:
    std::string operator()(const std::map<std::string, T> &v) {
        YAML::Node node(YAML::NodeType::Map);
        for (auto &i : v) {
            node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief 类型转换模板类偏特化(std::string 转换成 std::unordered_map<std::string, T>)
 */
template <class T>
class LexicalCast<std::string, std::unordered_map<std::string, T>> {
public:
    std::unordered_map<std::string, T> operator()(const std::string &v) {
        YAML::Node node = YAML::Load(v);
        typename std::unordered_map<std::string, T> vec;
        std::stringstream ss;
        for (auto it = node.begin(); it != node.end(); ++it) {
            ss.str("");
            ss << it->second;
            vec.insert(std::make_pair(it->first.Scalar(), LexicalCast<std::string, T>()(ss.str())));
        }
        return vec;
    }
};

/**
 * @brief 类型转换模板类偏特化(std::unordered_map<std::string, T> 转换成 std::string)
 */
template <class T>
class LexicalCast<std::unordered_map<std::string, T>, std::string> {
public:
    std::string operator()(const std::unordered_map<std::string, T> &v) {
        YAML::Node node(YAML::NodeType::Map);
        for (auto &i : v) {
            node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// ============================================================
// ConfigVarBase — 配置参数基类
// ============================================================

/**
 * @brief 配置变量的基类
 */
class ConfigVarBase {
public:
    typedef QSharedPointer<ConfigVarBase> ptr;

    /**
     * @brief 构造函数
     * @param[in] name 配置参数名称[0-9a-z_.]
     * @param[in] description 配置参数描述
     */
    ConfigVarBase(const QString &name, const QString &description = QString())
        : m_name(name.trimmed().toLower()), m_description(description) {}

    /**
     * @brief 析构函数
     */
    virtual ~ConfigVarBase() {}

    /**
     * @brief 返回配置参数名称
     */
    const QString &getName() const { return m_name; }

    /**
     * @brief 返回配置参数的描述
     */
    const QString &getDescription() const { return m_description; }

    /**
     * @brief 转成 YAML 字符串
     */
    virtual QString toString() = 0;

    /**
     * @brief 从 YAML 字符串初始化值
     */
    virtual bool fromString(const QString &val) = 0;

    /**
     * @brief 返回配置参数值的类型名称
     */
    virtual QString getTypeName() const = 0;

protected:
    QString m_name;         /// 配置参数的名称（小写）
    QString m_description;  /// 配置参数的描述
};

// ============================================================
// ConfigVar<T> — 类型安全的配置参数
// ============================================================

/**
 * @brief 配置参数模板子类,保存对应类型的参数值
 * @details T 参数的具体类型
 *          FromStr 从std::string转换成T类型的仿函数
 *          ToStr 从T转换成std::string的仿函数
 */
template <class T, class FromStr = LexicalCast<std::string, T>, class ToStr = LexicalCast<T, std::string>>
class ConfigVar : public ConfigVarBase {
public:
    typedef QSharedPointer<ConfigVar> ptr;
    typedef std::function<void(const T &oldValue, const T &newValue)> onChangeCb;

    /**
     * @brief 通过参数名,参数值,描述构造ConfigVar
     * @param[in] name 参数名称有效字符为[0-9a-z_.]
     * @param[in] defaultValue 参数的默认值
     * @param[in] description 参数的描述
     */
    ConfigVar(const QString &name, const T &defaultValue, const QString &description = QString())
        : ConfigVarBase(name, description), m_val(defaultValue) {}

    /**
     * @brief 将参数值转换成 YAML String
     * @exception 当转换失败抛出异常
     */
    QString toString() override {
        try {
            QReadLocker lock(&m_mutex);
            return QString::fromStdString(ToStr()(m_val));
        } catch (std::exception &e) {
            // LOG_ERROR() << "ConfigVar::toString exception " << e.what()
            //             << " name=" << m_name;
        }
        return QString();
    }

    /**
     * @brief 从 YAML String 转成参数的值
     * @exception 当转换失败抛出异常
     */
    bool fromString(const QString &val) override {
        try {
            setValue(FromStr()(val.toStdString()));
            return true;
        } catch (std::exception &e) {
            // LOG_ERROR() << "ConfigVar::fromString exception "
            //             << e.what() << " name=" << m_name << " - " << val;
        }
        return false;
    }

    /**
     * @brief 获取当前参数的值
     */
    const T getValue() {
        QReadLocker lock(&m_mutex);
        return m_val;
    }

    /**
     * @brief 设置当前参数的值
     * @details 如果参数的值有发生变化,则通知对应的注册回调函数
     */
    void setValue(const T &v) {
        {
            QReadLocker lock(&m_mutex);
            if (v == m_val) {
                return;
            }
            for (auto it = m_cbs.begin(); it != m_cbs.end(); ++it) {
                it.value()(m_val, v);
            }
        }
        QWriteLocker lock(&m_mutex);
        m_val = v;
    }

    /**
     * @brief 返回参数值的类型名称
     */
    QString getTypeName() const override { return QString::fromLatin1(typeid(T).name()); }

    /**
     * @brief 添加变化回调函数
     * @return 返回该回调函数对应的唯一id,用于删除回调
     */
    quint64 addListener(onChangeCb cb) {
        static quint64 s_funId = 0;
        QWriteLocker lock(&m_mutex);
        ++s_funId;
        m_cbs[s_funId] = cb;
        return s_funId;
    }

    /**
     * @brief 删除回调函数
     * @param[in] key 回调函数的唯一id
     */
    void delListener(quint64 key) {
        QWriteLocker lock(&m_mutex);
        m_cbs.remove(key);
    }

    /**
     * @brief 获取回调函数
     * @param[in] key 回调函数的唯一id
     * @return 如果存在返回对应的回调函数,否则返回nullptr
     */
    onChangeCb getListener(quint64 key) {
        QReadLocker lock(&m_mutex);
        auto it = m_cbs.find(key);
        return it == m_cbs.end() ? nullptr : it.value();
    }

    /**
     * @brief 清理所有的回调函数
     */
    void clearListener() {
        QWriteLocker lock(&m_mutex);
        m_cbs.clear();
    }

private:
    QReadWriteLock m_mutex;               /// 读写锁
    T m_val;                              /// 当前值
    QMap<quint64, onChangeCb> m_cbs;      /// 变更回调函数组
};

// ============================================================
// Config — 配置管理器
// ============================================================

/**
 * @brief ConfigVar的管理类
 * @details 提供便捷的方法创建/访问ConfigVar
 */
class Config {
public:
    typedef QHash<QString, ConfigVarBase::ptr> ConfigVarMap;

    /**
     * @brief 获取/创建对应参数名的配置参数
     * @param[in] name 配置参数名称
     * @param[in] defaultValue 参数默认值
     * @param[in] description 参数描述
     * @details 获取参数名为name的配置参数,如果存在直接返回
     *          如果不存在,创建参数配置并用defaultValue赋值
     * @return 返回对应的配置参数,如果参数名存在但是类型不匹配则返回nullptr
     * @exception 如果参数名包含非法字符[^0-9a-z_.] 抛出异常 std::invalid_argument
     */
    template <class T>
    static typename ConfigVar<T>::ptr lookup(const QString &name, const T &defaultValue,
                                             const QString &description = QString()) {
        QWriteLocker lock(&getMutex());
        auto it = getDatas().find(name.toLower());
        if (it != getDatas().end()) {
            auto tmp = qSharedPointerDynamicCast<ConfigVar<T>>(it.value());
            if (tmp) {
                // LOG_INFO() << "Lookup name=" << name << " exists";
                return tmp;
            } else {
                // LOG_ERROR() << "Lookup name=" << name << " exists but type not "
                //             << typeid(T).name() << " real_type=" << it.value()->getTypeName();
                return typename ConfigVar<T>::ptr();
            }
        }

        if (name.contains(QRegularExpression("[^abcdefghikjlmnopqrstuvwxyz._012345678]"))) {
            // LOG_ERROR() << "Lookup name invalid " << name;
            throw std::invalid_argument(name.toStdString());
        }

        typename ConfigVar<T>::ptr v(new ConfigVar<T>(name, defaultValue, description));
        getDatas()[name.toLower()] = v;
        return v;
    }

    /**
     * @brief 查找配置参数
     * @param[in] name 配置参数名称
     * @return 返回配置参数名为name的配置参数
     */
    template <class T>
    static typename ConfigVar<T>::ptr lookup(const QString &name) {
        QReadLocker lock(&getMutex());
        auto it = getDatas().find(name.toLower());
        if (it == getDatas().end()) {
            return typename ConfigVar<T>::ptr();
        }
        return qSharedPointerDynamicCast<ConfigVar<T>>(it.value());
    }

    /**
     * @brief 使用YAML::Node初始化配置模块
     */
    static void loadFromYaml(const YAML::Node &root);

    /**
     * @brief 加载path文件夹里面的配置文件
     */
    static void loadFromConfDir(const QString &path, bool force = false);

    /**
     * @brief 查找配置参数,返回配置参数的基类
     * @param[in] name 配置参数名称
     */
    static ConfigVarBase::ptr lookupBase(const QString &name);

    /**
     * @brief 遍历配置模块里面所有配置项
     * @param[in] cb 配置项回调函数
     */
    static void visit(std::function<void(ConfigVarBase::ptr)> cb);

private:
    /**
     * @brief 返回所有的配置项
     */
    static ConfigVarMap &getDatas() {
        static ConfigVarMap s_datas;
        return s_datas;
    }

    /**
     * @brief 配置项的读写锁
     */
    static QReadWriteLock &getMutex() {
        static QReadWriteLock s_mutex;
        return s_mutex;
    }
};

}  // namespace zch

#endif  // CONFIG_H__
