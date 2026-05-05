#pragma once

#include <QString>
#include <QFile>
#include <QMutex>
#include <QQueue>
#include <QWaitCondition>
#include <QThread>
#include <QDateTime>
#include <atomic>
#include <QDir>
#include <QTextStream>
#include <iostream>
#include <vector>

enum class LogLevel {
    DEBUG = 0,
    INFO,
    WARN,
    ERROR,
    OFF
};

/**
 * @brief 单条日志的完整信息
 *
 * @details 在业务线程中填充（时间戳在此刻确定），入队后由写线程消费。
 *          file 指向 __FILE__ 字符串字面量，生命周期永久，无需拷贝。
 */
struct LogEntry {
    QDateTime   timestamp;  /// 日志产生时刻（业务线程捕获，保证时间准确性）
    LogLevel    level;      /// 日志级别
    QString     message;    /// LogStream 收集的原始消息
    const char *file;       /// 来源文件名（指向 __FILE__ 字符串字面量，生命周期永久）
    int         line;       /// 来源行号
};

/**
 * @brief 日志格式化器抽象接口
 *
 * @details 将 LogEntry 转换为用于输出的 QString。
 *          实现类负责决定输出格式（纯文本、JSON 等）。
 */
class ILogFormatter {
public:
    virtual ~ILogFormatter() = default;

    /**
     * @brief 将日志条目格式化为字符串
     * @param[in] entry 结构化日志条目
     * @return 格式化后的字符串
     */
    virtual QString format(const LogEntry &entry) = 0;
};

/**
 * @brief 日志输出目标抽象接口
 *
 * @details 接收已格式化的日志字符串，输出到具体目标（文件、控制台、网络等）。
 *          实现类需保证 write() 线程安全——Logger 的写线程和 flush() 可能并发调用。
 */
class ILogSink {
public:
    virtual ~ILogSink() = default;

    /**
     * @brief 写入一条已格式化的日志
     * @param[in] formattedMsg 由 ILogFormatter 格式化后的日志字符串
     */
    virtual void write(const QString &formattedMsg) = 0;

    /**
     * @brief 强制刷新底层输出缓冲区
     * @details 默认为空操作，实现类按需覆盖
     */
    virtual void flush() {}
};

/**
 * @brief 文件日志输出，支持按天滚动和按大小切分
 *
 * @details 内部维护 QFile 和 QTextStream，通过 QMutex 保证线程安全。
 *          滚动策略在每次 write() 时自动检查。
 */
class FileSink : public ILogSink {
public:
    /**
     * @brief 构造并打开日志文件
     * @param[in] dir 日志目录（不存在则自动创建）
     * @param[in] maxFileSize 文件大小上限（字节），默认 10MB
     */
    FileSink(const QString &dir, qint64 maxFileSize = 10 * 1024 * 1024);

    /**
     * @brief 写入日志到文件
     * @param[in] formattedMsg 已格式化的日志字符串
     * @details 加锁后先检查滚动策略再写入并 flush，保证原子性
     */
    void write(const QString &formattedMsg) override;

    /**
     * @brief 强制刷新文件缓冲区
     */
    void flush() override;

private:
    /**
     * @brief 检查并执行日志滚动
     * @details 按天切：日期变化时切换到新文件
     *          按大小切：文件超过 m_maxFileSize 时重命名当前文件并创建新文件
     */
    void rotateIfNeeded();

    QString  m_logDir;       /// 日志文件输出目录
    qint64   m_maxFileSize;  /// 单个日志文件大小上限（字节），超过则切分
    QString  m_currentDate;  /// 当前日志文件对应的日期（yyyy-MM-dd），用于按天滚动判断
    QFile    m_file;         /// 当前日志文件句柄
    QTextStream m_stream;    /// 绑定 m_file 的文本流，用于高效写入
    QMutex   m_mutex;        /// 保护 m_file / m_stream 的互斥锁（write 与 flush 可能并发）
};

/**
 * @brief 控制台日志输出
 * @details 通过 qDebug() 将日志输出到调试控制台
 */
class ConsoleSink : public ILogSink {
public:
    /**
     * @brief 输出日志到控制台
     * @param[in] formattedMsg 已格式化的日志字符串
     */
    void write(const QString &formattedMsg) override;
};

/**
 * @brief 经典纯文本日志格式化器
 * @details 输出格式：yyyy-MM-dd hh:mm:ss[LEVEL][file:line] message
 */
class SimpleFormatter : public ILogFormatter {
public:
    /**
     * @brief 格式化日志条目为纯文本
     * @param[in] entry 结构化日志条目
     * @return 格式化后的单行文本
     */
    QString format(const LogEntry &entry) override;
};

/**
 * @brief 企业级日志系统核心（单例模式）
 *
 * @details 核心职责：
 *          - 管理异步写线程（负责消费队列并分发到所有 sink）
 *          - 持有一个 ILogFormatter 和多个 ILogSink
 *          - 同步模式下直接在当前线程调用 sink
 *
 *          级别过滤在宏层面完成（LOG_DEBUG 等），低于当前级别的日志
 *          不会构造 LogStream，避免无效开销。
 */
class Logger {
public:
    /**
     * @brief 获取日志器单例实例
     * @return Logger& 全局唯一实例
     */
    static Logger &instance();

    /**
     * @brief 初始化日志系统
     * @param[in] dir 日志文件输出目录
     * @param[in] level 最低输出级别（默认 DEBUG）
     * @param[in] async 是否启用异步模式（默认 true）
     * @param[in] maxFileSize 单个日志文件最大字节数（默认 10MB）
     * @param[in] console 是否同时输出到控制台（默认 true）
     * @details 创建默认的 FileSink、ConsoleSink 和 SimpleFormatter。
     *          重复调用无副作用。
     */
    void init(const QString &dir,
              LogLevel level = LogLevel::DEBUG,
              bool async = true,
              qint64 maxFileSize = 10 * 1024 * 1024,
              bool console = true);

    /**
     * @brief 提交一条日志
     * @param[in] level 日志级别
     * @param[in] msg 原始消息内容
     * @param[in] file 来源文件名
     * @param[in] line 来源行号
     * @details 构造 LogEntry 后根据模式处理：
     *          - 异步：入队并唤醒写线程
     *          - 同步：直接调用 writeToSinks()
     */
    void log(LogLevel level, const QString &msg,
             const char *file, int line);

    /**
     * @brief 获取当前日志级别
     * @return LogLevel 最低输出级别
     */
    LogLevel level() const { return m_level; }

    /**
     * @brief 添加一个输出目标
     * @param[in] sink 输出目标实例（unique_ptr，转移所有权）
     * @details 线程安全，建议在 init() 之后、产生日志之前调用
     */
    void addSink(std::unique_ptr<ILogSink> sink);

    /**
     * @brief 替换当前格式化器
     * @param[in] formatter 新格式化器实例（unique_ptr，转移所有权）
     * @details 线程安全，建议在 init() 之后、产生日志之前调用
     */
    void setFormatter(std::unique_ptr<ILogFormatter> formatter);

    /**
     * @brief 强制排空队列并刷新所有 sink
     * @details 异步模式下在调用线程直接消费队列中的所有 LogEntry，
     *          然后依次 flush 所有 sink。
     */
    void flush();

    /**
     * @brief 安全关闭日志系统
     * @details 通知写线程退出 → 等待线程结束 → flush 所有 sink
     */
    void stop();

private:
    Logger();
    ~Logger();

    /**
     * @brief 异步写线程主循环
     * @details 循环等待队列中有日志条目，取出后调用 writeToSinks()，
     *          不在持有锁时执行 I/O。
     */
    void asyncLoop();

    /**
     * @brief 将日志条目格式化并分发到所有 sink
     * @param[in] entry 结构化日志条目
     * @details 先用 m_formatter 格式化，再遍历所有 sink 调用 write()
     */
    void writeToSinks(const LogEntry &entry);

    QQueue<LogEntry>   m_queue;                         /// 日志条目队列（业务线程入队，写线程出队）
    QMutex             m_mutex;                         /// 保护 m_queue 和 m_cond 的互斥锁
    QWaitCondition     m_cond;                          /// 条件变量（队列空时写线程等待，入队时唤醒）
    std::atomic<bool>  m_running{false};                /// 写线程运行标志（原子变量，stop 时置 false）
    QThread           *m_thread = nullptr;              /// 异步写线程（nullptr 表示同步模式）
    bool               m_async = true;                  /// 是否启用异步模式

    LogLevel   m_level = LogLevel::DEBUG;               /// 最低输出级别

    std::vector<std::unique_ptr<ILogSink>> m_sinks;      /// 输出目标列表（可动态扩展）
    std::unique_ptr<ILogFormatter>     m_formatter;     /// 当前格式化器
    mutable QMutex m_sinkMutex;                         /// 保护 m_sinks / m_formatter 的互斥锁
};

/**
 * @brief 流式日志收集器（RAII 临时对象）
 *
 * @details 通过 operator<< 收集日志内容到内部缓冲区，
 *          析构时自动调用 Logger::log() 提交。
 *          通常由 LOG_DEBUG / LOG_INFO 等宏创建，不直接使用。
 */
class LogStream {
public:
    /**
     * @brief 构造流式日志收集器
     * @param[in] level 日志级别
     * @param[in] file 来源文件名
     * @param[in] line 来源行号
     */
    LogStream(LogLevel level, const char *file, int line)
        : m_level(level), m_file(file), m_line(line),
          m_stream(&m_buffer) {}

    /**
     * @brief 析构时提交日志
     */
    ~LogStream() {
        Logger::instance().log(m_level, m_buffer, m_file, m_line);
    }

    /**
     * @brief 流式插入运算符
     * @param[in] value 任意可被 QTextStream 接受的值
     * @return LogStream& 自身引用，支持链式调用
     */
    template<typename T>
    LogStream &operator<<(const T &value) {
        m_stream << value;
        return *this;
    }

private:
    LogLevel    m_level;    /// 日志级别
    const char *m_file;     /// 来源文件名（指向字符串字面量）
    int         m_line;     /// 来源行号
    QString     m_buffer;   /// 内部缓冲区，operator<< 收集的内容暂存于此
    QTextStream m_stream;   /// 绑定 m_buffer 的流，用于拼接字符串
};

/**
 * @brief 从完整路径中提取文件名
 * @param[in] path 完整文件路径（通常为 __FILE__ 宏展开值）
 * @return 仅包含文件名的字符串指针（指向原字符串内部，不分配新内存）
 * @details 同时处理 Unix('/') 和 Windows('\\') 路径分隔符
 */
inline const char *__filename_impl(const char *path) {
    const char *p1 = strrchr(path, '/');
    const char *p2 = strrchr(path, '\\');
    const char *p  = (p1 && p2) ? (p1 > p2 ? p1 : p2) : (p1 ? p1 : p2);
    return p ? p + 1 : path;
}

#define __FILENAME__ __filename_impl(__FILE__)

#define LOG_DEBUG() \
    if (LogLevel::DEBUG >= Logger::instance().level()) \
        LogStream(LogLevel::DEBUG, __FILENAME__, __LINE__)

#define LOG_INFO() \
    if (LogLevel::INFO >= Logger::instance().level()) \
        LogStream(LogLevel::INFO, __FILENAME__, __LINE__)

#define LOG_WARN() \
    if (LogLevel::WARN >= Logger::instance().level()) \
        LogStream(LogLevel::WARN, __FILENAME__, __LINE__)

#define LOG_ERROR() \
    if (LogLevel::ERROR >= Logger::instance().level()) \
        LogStream(LogLevel::ERROR, __FILENAME__, __LINE__)
