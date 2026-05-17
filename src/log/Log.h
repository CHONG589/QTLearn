#pragma once

#include <QString>
#include <QTextStream>
#include <QSharedPointer>
#include <QDateTime>
#include <QMutex>
#include <QReadWriteLock>
#include <yaml-cpp/yaml.h>
#include <QFile>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>

#include "src/config/config.h"

/**
 * @brief 获取root日志器
 */
#define LOG_ROOT() zch::LoggerManager::instance().rootLogger()

/**
 * @brief 获取指定名称的日志器
 * @param[in] name 日志器名称
 */
#define LOG_NAME(name) zch::LoggerManager::instance().logger(name)

/**
 * @brief 使用流式方式将日志级别level的日志写入到logger
 * @details 构造一个LogEventWrap对象，包裹包含日志器和日志事件，在对象析构时调用日志器写日志事件
 * @todo 协程id未实现，暂时写0
 */
#define LOG_LEVEL(logger, curLevel) \
    if(curLevel <= logger->level()) \
        zch::LogEventWrap(logger, zch::LogEvent::ptr(new zch::LogEvent(logger->name(), \
            curLevel, QFileInfo(__FILE__).fileName(), __LINE__, zch::LoggerManager::instance().elapsedMs(), \
                QDateTime::currentDateTime()))).getLogEvent()->stream()

#define LOG_FATAL(logger) LOG_LEVEL(logger, zch::LogLevel::FATAL)

#define LOG_ALERT(logger) LOG_LEVEL(logger, zch::LogLevel::ALERT)

#define LOG_CRIT(logger) LOG_LEVEL(logger, zch::LogLevel::CRIT)

#define LOG_ERROR(logger) LOG_LEVEL(logger, zch::LogLevel::ERROR)

#define LOG_WARN(logger) LOG_LEVEL(logger, zch::LogLevel::WARN)

#define LOG_NOTICE(logger) LOG_LEVEL(logger, zch::LogLevel::NOTICE)

#define LOG_INFO(logger) LOG_LEVEL(logger, zch::LogLevel::INFO)

#define LOG_DEBUG(logger) LOG_LEVEL(logger, zch::LogLevel::DEBUG)

// 防止 ERROR 与 Windows 的宏定义冲突
#ifdef ERROR
#undef ERROR
#endif

namespace zch {

/**
 * @brief 日志级别
 */
class LogLevel {
public:
    enum Level {
        FATAL = 0,      /// 致命情况，系统不可用
        ALERT = 100,    /// 高优先级情况，例如数据库系统崩溃
        CRIT = 200,     /// 严重错误，例如硬盘错误
        ERROR = 300,    /// 错误
        WARN = 400,     /// 警告
        NOTICE = 500,   /// 正常但值得注意
        INFO = 600,     /// 一般信息
        DEBUG = 700,    /// 调试信息
        NOTSET = 800,   /// 未设置
    };

    /**
     * @brief 日志级别转字符串
     * @param[in] level 日志级别
     * @return 字符串形式的日志级别
     */
    static QString ToQString(LogLevel::Level level);

    /**
     * @brief 字符串转日志级别
     * @param[in] str 字符串
     * @return 日志级别
     * @note 不区分大小写
     */
    static LogLevel::Level FromQString(const QString &str);
};

/**
 * @brief 日志事件
 */
class LogEvent {
public:
    using ptr = QSharedPointer<LogEvent>;

    /**
     * @brief 构造函数
     * @param[in] loggerName 日志器名称
     * @param[in] level 日志级别
     * @param[in] file 源文件名
     * @param[in] line 源文件行号
     * @param[in] elapse 累计运行毫秒数
     * @param[in] time 日志产生时间
     */
    LogEvent(const QString &loggerName,
        LogLevel::Level level,
        const QString &file,
        qint32 line,
        qint64 elapse,
        const QDateTime &time);

    /**
     * @brief 获取日志级别
     * @return 日志级别
     */
    LogLevel::Level getLevel() const { return m_level; }

    /**
     * @brief 获取日志内容
     * @return 日志文本内容
     */
    QString getContent() const { return m_content; }

    /**
     * @brief 获取源文件名
     * @return 文件名
     */
    QString getFile() const { return m_file; }

    /**
     * @brief 获取源文件行号
     * @return 行号
     */
    qint32 getLine() const { return m_line; }

    /**
     * @brief 获取累计运行毫秒数
     * @return 程序启动到日志产生时的毫秒数
     */
    qint64 getElapse() const { return m_elapse; }

    /**
     * @brief 获取日志产生时间
     * @return 时间
     */
    QDateTime getTime() const { return m_time; }

    /**
     * @brief 获取流对象
     * @details 返回 QTextStream 引用，用于流式写入日志内容
     * @return 流对象引用
     */
    QTextStream &stream() { return m_stream; }

    /**
     * @brief 获取日志器名称
     * @return 日志器名称
     */
    QString getLoggerName() const { return m_loggerName; }

private:
    LogLevel::Level m_level;   /// 日志级别
    QString m_content;         /// 日志内容
    QTextStream m_stream;      /// 流式写入对象
    QString m_file;            /// 文件名
    qint32 m_line = 0;         /// 行号
    qint64 m_elapse = 0;       /// 累计耗时(ms)
    QDateTime m_time;          /// 时间
    QString m_loggerName;      /// 日志器名称
};

/**
 * @brief 日志格式化器
 * @details 解析格式模板字符串，将 LogEvent 格式化为最终输出文本
 */
class LogFormatter {
public:
    using ptr = QSharedPointer<LogFormatter>;

    /**
     * @brief 构造函数
     * @param[in] pattern 格式模板
     * @details 模板参数说明：
     * - %%m 消息
     * - %%p 日志级别
     * - %%c 日志器名称
     * - %%d 日期时间，后面可跟一对括号指定时间格式
     * - %%r 该日志器创建后的累计运行毫秒数
     * - %%f 文件名
     * - %%l 行号
     * - %%t 线程id
     * - %%F 协程id
     * - %%N 线程名称
     * - %%% 百分号
     * - %%T 制表符
     * - %%n 换行
     *
     * 默认格式：年-月-日 时:分:秒 [累计运行毫秒数][日志级别][日志器名称]文件名:行号 日志消息 换行符
     */
    explicit LogFormatter(const QString &pattern = "[%d{yyyy-MM-dd HH:mm:ss}][%rms][%p][%c][%f:%l] %m%n");

    /**
     * @brief 初始化，解析格式模板，提取模板项
     * @details 遍历 pattern 字符串，识别 %x 占位符，构建 FormatItem 列表
     */
    void init();

    /**
     * @brief 模板解析是否出错
     * @return true 表示解析过程中遇到错误
     */
    bool isError() const { return m_error; }

    /**
     * @brief 对日志事件进行格式化
     * @param[in] event 日志事件
     * @return 格式化后的日志字符串
     */
    QString format(const QSharedPointer<LogEvent> &event);

    /**
     * @brief 获取格式模板字符串
     * @return 当前格式模板
     */
    QString pattern() const { return m_pattern; }

public:
    /**
     * @brief 日志内容格式化项，虚基类，用于派生出不同的格式化项
     */
    class FormatItem {
    public:
        using ptr = QSharedPointer<FormatItem>;

        /**
         * @brief 析构函数
         */
        virtual ~FormatItem() = default;

        /**
         * @brief 格式化日志事件
         * @param[in,out] os 输出流，格式化结果写入此流
         * @param[in] event 日志事件
         */
        virtual void format(QTextStream &os, const QSharedPointer<LogEvent> &event) = 0;
    };

private:
    QString m_pattern;                  /// 日志格式模板
    QVector<FormatItem::ptr> m_items;   /// 解析后的格式模板项数组
    bool m_error = false;               /// 是否出错
};

/**
 * @brief 日志输出器，基类
 */
class LogAppender {
public:
    using ptr = QSharedPointer<LogAppender>;

    /**
     * @brief 构造函数
     * @param[in] defaultFormatter 默认格式化器
     */
    explicit LogAppender(const QSharedPointer<LogFormatter> &defaultFormatter);

    /**
     * @brief 析构函数
     */
    virtual ~LogAppender() = default;

    /**
     * @brief 设置格式化器
     * @param[in] formatter 新的格式化器
     */
    void setFormatter(const QSharedPointer<LogFormatter> &formatter);

    /**
     * @brief 获取格式化器
     * @details 如果未设置自定义 formatter，返回默认 formatter
     * @return 当前使用的格式化器
     */
    QSharedPointer<LogFormatter> formatter() const;

    /**
     * @brief 写日志
     * @param[in] event 日志事件
     */
    virtual void log(const QSharedPointer<LogEvent> &event) = 0;

    /**
     * @brief 转为 YAML 字符串
     * @details 将输出器配置序列化为 YAML 格式，用于导出/持久化
     * @return YAML 字符串
     */
    virtual QString toYamlString() const = 0;

protected:
    mutable QReadWriteLock m_lock;                    /// 读写锁
    QSharedPointer<LogFormatter> m_formatter;         /// 当前 formatter
    QSharedPointer<LogFormatter> m_defaultFormatter;  /// 默认 formatter
};

/**
 * @brief 标准输出日志输出器
 * @details 将日志输出到 stdout
 */
class StdoutLogAppender : public LogAppender {
public:
    using ptr = QSharedPointer<StdoutLogAppender>;

    /**
     * @brief 构造函数
     * @details 默认格式化器以缺省 pattern 构造，输出流绑定到 stdout
     */
    StdoutLogAppender();

    /**
     * @brief 写日志到标准输出
     * @param[in] event 日志事件
     */
    void log(const LogEvent::ptr &event) override;

    /**
     * @brief 转为 YAML 字符串
     * @return YAML 字符串，包含 type 和 pattern 字段
     */
    QString toYamlString() const override;

private:
    QTextStream m_stream;  /// 标准输出流
};

/**
 * @brief 文件日志输出器
 * @details 将日志输出到文件，支持按天滚动和定期 reopen
 */
class FileLogAppender : public LogAppender {
public:
    using ptr = QSharedPointer<FileLogAppender>;

    /**
     * @brief 构造函数
     * @param[in] logDir 日志文件目录
     * @details 自动创建目录，按当前日期生成日志文件名并打开
     */
    explicit FileLogAppender(const QString &logDir);

    /**
     * @brief 写日志到文件
     * @param[in] event 日志事件
     * @details 写入前检查日期切换和定期 reopen
     */
    void log(const LogEvent::ptr &event) override;

    /**
     * @brief 重新打开日志文件
     * @details 关闭并重新以追加模式打开当前日志文件，用于文件轮转后的恢复
     * @return true 表示打开成功
     */
    bool reopen();

    /**
     * @brief 转为 YAML 字符串
     * @return YAML 字符串，包含 type、file 和 pattern 字段
     */
    QString toYamlString() const override;

private:
    /**
     * @brief 创建新的日志文件
     * @details 按当前日期生成文件名，关闭旧文件并打开新文件
     */
    void createNewFile();

    /**
     * @brief 根据日期构建日志文件路径
     * @param[in] date 日期
     * @return 文件完整路径，格式为 logDir/yyyy-MM-dd.log
     */
    QString buildFileName(const QDate &date) const;

    /**
     * @brief 确保日志目录存在
     * @details 如果目录不存在则递归创建
     */
    void ensureLogPathExist();

private:
    QString m_logDir;                /// 日志目录
    QString m_fileName;              /// 当前日志文件路径
    QFile m_file;                    /// 文件对象
    QDate m_currentDate;             /// 当前日期
    qint64 m_lastReopenTime{0};      /// 上次 reopen 时间
    bool m_hasError{false};          /// 文件错误状态
    mutable QMutex m_mutex;          /// 互斥锁
    QTextStream m_stream;            /// 文件输出流
};

/**
 * @brief 日志器
 * @details 持有日志级别和一组输出器，负责将日志事件分发给所有输出器
 */
class Logger {
public:
    using ptr = QSharedPointer<Logger>;

    /**
     * @brief 构造函数
     * @param[in] name 日志器名称，默认为 "default"
     */
    explicit Logger(const QString &name = "default");

    /**
     * @brief 获取日志器名称
     * @return 名称
     */
    QString name() const;

    /**
     * @brief 获取创建时间
     * @return 创建时间
     */
    QDateTime createTime() const;

    /**
     * @brief 设置日志级别
     * @param[in] level 日志级别
     */
    void setLevel(LogLevel::Level level);

    /**
     * @brief 获取日志级别
     * @return 当前日志级别
     */
    LogLevel::Level level() const;

    /**
     * @brief 添加日志输出器
     * @param[in] appender 日志输出器
     * @details 如果已存在相同的 appender 则忽略
     */
    void addAppender(const LogAppender::ptr &appender);

    /**
     * @brief 移除日志输出器
     * @param[in] appender 要移除的日志输出器
     */
    void removeAppender(const LogAppender::ptr &appender);

    /**
     * @brief 清空所有日志输出器
     */
    void clearAppenders();

    /**
     * @brief 写日志
     * @param[in] event 日志事件
     * @details 如果事件级别低于当前日志器级别则忽略；否则分发到所有输出器
     */
    void log(const LogEvent::ptr &event);

    /**
     * @brief 转为 YAML 字符串
     * @return YAML 字符串，包含 name、level 和 appenders
     */
    QString toYamlString() const;

private:
    QString m_name;                         /// 日志器名称
    LogLevel::Level m_level;                /// 日志级别
    QList<LogAppender::ptr> m_appenders;    /// 日志输出器列表
    QDateTime m_createTime;                 /// 创建时间
    mutable QMutex m_mutex;                 /// 互斥锁
};

/**
 * @brief 日志事件包装器，方便宏定义，内部包含日志事件和日志器
 */
class LogEventWrap {
public:
    /**
     * @brief 构造函数
     * @param[in] logger 日志器
     * @param[in] event 日志事件
     */
    LogEventWrap(Logger::ptr logger, LogEvent::ptr event);

    /**
     * @brief 析构函数
     * @details 日志事件在析构时由日志器进行输出
     */
    ~LogEventWrap();

    /**
     * @brief 获取日志事件
     * @return 日志事件指针
     */
    LogEvent::ptr getLogEvent() const { return m_event; }

private:
    Logger::ptr m_logger;  /// 日志器
    LogEvent::ptr m_event; /// 日志事件
};

/**
 * @brief 日志器管理器，单例
 * @details 管理所有日志器实例，提供 root logger 的创建和按名查找
 */
class LoggerManager {
public:
    using ptr = QSharedPointer<LoggerManager>;

    /**
     * @brief 获取单例实例
     * @return 全局唯一的 LoggerManager 引用
     */
    static LoggerManager &instance();

    /**
     * @brief 获取程序累计运行毫秒数
     * @return 从 LoggerManager 构造到当前的毫秒数
     */
    qint64 elapsedMs() const;

private:
    /**
     * @brief 构造函数
     * @details 初始化计时器，创建 root logger 并添加 StdoutLogAppender
     */
    LoggerManager();

public:
    /**
     * @brief 初始化日志系统
     * @details 从 YAML 配置初始化日志系统（暂未实现）
     */
    void init();

    /**
     * @brief 获取指定名称的日志器
     * @param[in] name 日志器名称
     * @details 如果名称对应的日志器不存在，创建新的日志器并继承 root logger 的配置
     * @return 日志器指针
     */
    Logger::ptr logger(const QString &name);

    /**
     * @brief 获取 root 日志器
     * @return root 日志器指针
     */
    Logger::ptr rootLogger() const;

    /**
     * @brief 将所有日志器配置转为 YAML 字符串
     * @return YAML 字符串
     */
    QString toYamlString() const;

private:
    mutable QMutex m_mutex;                /// 互斥锁
    QHash<QString, Logger::ptr> m_loggers; /// 日志器映射表
    Logger::ptr m_rootLogger;              /// root 日志器
    QElapsedTimer m_elapsedTimer;          /// 累计运行计时器
};

/**
 * @brief 日志输出器配置结构体
 */
struct LogAppenderDefine {
    int type = 0;       /// 输出器类型：1=File，2=Stdout
    QString pattern;    /// 格式模板
    QString file;       /// 文件路径（type=1时使用）

    /**
     * @brief 相等比较
     * @param[in] other 另一个 LogAppenderDefine
     * @return true 表示所有字段相等
     */
    bool operator==(const LogAppenderDefine &other) const {
        return type == other.type
            && pattern == other.pattern
            && file == other.file;
    }
};

/**
 * @brief 日志器配置结构体
 */
struct LogDefine {
    QString name;                               /// 日志器名称
    LogLevel::Level level = LogLevel::NOTSET;   /// 日志级别
    QVector<LogAppenderDefine> appenders;       /// 输出器配置列表

    /**
     * @brief 相等比较
     * @param[in] other 另一个 LogDefine
     * @return true 表示所有字段相等
     */
    bool operator==(const LogDefine &other) const {
        return name == other.name
            && level == other.level
            && appenders == other.appenders;
    }

    /**
     * @brief 小于比较（按名称）
     * @param[in] other 另一个 LogDefine
     * @return true 表示当前对象名称小于 other 的名称
     */
    bool operator<(const LogDefine &other) const {
        return name < other.name;
    }

    /**
     * @brief 判断配置是否有效
     * @return true 表示名称非空
     */
    bool isValid() const {
        return !name.isEmpty();
    }
};

/**
 * @brief QSet 哈希函数（用于 QSet<LogDefine>）
 * @param[in] key LogDefine 对象
 * @param[in] seed 哈希种子
 * @return 哈希值
 */
inline uint qHash(const LogDefine &key, uint seed = 0) {
    return qHash(key.name, seed);
}

}  // namespace zch
