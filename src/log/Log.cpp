#include "Log.h"

#include <sstream>

namespace zch {

/**
 * @brief 日志级别转字符串
 * @param[in] level 日志级别
 * @return 字符串形式的日志级别，未识别级别返回 "NOTSET"
 */
QString LogLevel::ToQString(LogLevel::Level level) {
    switch (level) {
#define XX(name) case LogLevel::name: return #name;
        XX(FATAL);
        XX(ALERT);
        XX(CRIT);
        XX(ERROR);
        XX(WARN);
        XX(NOTICE);
        XX(INFO);
        XX(DEBUG);
#undef XX
        default:
            return "NOTSET";
    }
    return "NOTSET";
}

/**
 * @brief 字符串转日志级别
 * @param[in] str 字符串
 * @return 日志级别，识别失败返回 NOTSET
 * @note 不区分大小写
 */
LogLevel::Level LogLevel::FromQString(const QString &str) {
#define XX(level) \
    if (str.compare(#level, Qt::CaseInsensitive) == 0) { \
        return LogLevel::level; \
    }

    XX(FATAL)
    XX(ALERT)
    XX(CRIT)
    XX(ERROR)
    XX(WARN)
    XX(NOTICE)
    XX(INFO)
    XX(DEBUG)

#undef XX

    return LogLevel::NOTSET;
}

/**
 * @brief 构造函数
 * @param[in] loggerName 日志器名称
 * @param[in] level 日志级别
 * @param[in] file 源文件名
 * @param[in] line 行号
 * @param[in] elapse 累计运行毫秒数
 * @param[in] time 日志产生时间
 */
LogEvent::LogEvent(const QString &loggerName,
    LogLevel::Level level,
    const QString &file,
    qint32 line,
    qint64 elapse,
    const QDateTime &time)
    : m_level(level)
    , m_stream(&m_content)
    , m_file(file)
    , m_line(line)
    , m_elapse(elapse)
    , m_time(time)
    , m_loggerName(loggerName) {
}

// ============================================================
// LogFormatter::FormatItem 子类实现
// ============================================================

/**
 * @brief 消息内容格式化项
 * @details 输出日志事件的文本内容（%m）
 */
class MessageFormatItem : public LogFormatter::FormatItem {
public:
    /**
     * @brief 构造函数
     * @param[in] fmt 格式参数（未使用）
     */
    explicit MessageFormatItem(const QString &) {}

    /**
     * @brief 格式化输出
     * @param[in,out] os 输出流
     * @param[in] event 日志事件
     */
    void format(QTextStream &os,
        const LogEvent::ptr &event) override {
        os << event->getContent();
    }
};

/**
 * @brief 日志级别格式化项（%p）
 */
class LevelFormatItem : public LogFormatter::FormatItem {
public:
    /**
     * @brief 构造函数
     * @param[in] fmt 格式参数（未使用）
     */
    explicit LevelFormatItem(const QString &) {}

    /**
     * @brief 格式化输出日志级别文本
     * @param[in,out] os 输出流
     * @param[in] event 日志事件
     */
    void format(QTextStream &os,
        const LogEvent::ptr &event) override {
        os << LogLevel::ToQString(event->getLevel());
    }
};

/**
 * @brief 累计耗时格式化项（%r）
 */
class ElapseFormatItem : public LogFormatter::FormatItem {
public:
    /**
     * @brief 构造函数
     * @param[in] fmt 格式参数（未使用）
     */
    explicit ElapseFormatItem(const QString &) {}

    /**
     * @brief 格式化输出累计运行毫秒数
     * @param[in,out] os 输出流
     * @param[in] event 日志事件
     */
    void format(QTextStream &os,
        const LogEvent::ptr &event) override {
        os << event->getElapse();
    }
};

/**
 * @brief 日志器名称格式化项（%c）
 */
class LoggerNameFormatItem : public LogFormatter::FormatItem {
public:
    /**
     * @brief 构造函数
     * @param[in] fmt 格式参数（未使用）
     */
    explicit LoggerNameFormatItem(const QString &) {}

    /**
     * @brief 格式化输出日志器名称
     * @param[in,out] os 输出流
     * @param[in] event 日志事件
     */
    void format(QTextStream &os,
        const LogEvent::ptr &event) override {
        os << event->getLoggerName();
    }
};

/**
 * @brief 日期时间格式化项（%d{...}）
 * @details 支持 %d{format} 语法，format 为 Qt 日期时间格式字符串
 */
class DateTimeFormatItem : public LogFormatter::FormatItem {
public:
    /**
     * @brief 构造函数
     * @param[in] format 日期时间格式，为空则使用 "yyyy-MM-dd HH:mm:ss"
     */
    explicit DateTimeFormatItem(
        const QString &format = "yyyy-MM-dd HH:mm:ss") : m_format(format) {
        if (m_format.isEmpty()) {
            m_format = "yyyy-MM-dd HH:mm:ss";
        }
    }

    /**
     * @brief 格式化输出日期时间
     * @param[in,out] os 输出流
     * @param[in] event 日志事件
     */
    void format(QTextStream &os,
        const LogEvent::ptr &event) override {
        os << event->getTime().toString(m_format);
    }

private:
    QString m_format;  /// 日期时间格式字符串
};

/**
 * @brief 文件名格式化项（%f）
 */
class FileNameFormatItem : public LogFormatter::FormatItem {
public:
    /**
     * @brief 构造函数
     * @param[in] fmt 格式参数（未使用）
     */
    explicit FileNameFormatItem(const QString &) {}

    /**
     * @brief 格式化输出源文件名
     * @param[in,out] os 输出流
     * @param[in] event 日志事件
     */
    void format(QTextStream &os,
        const LogEvent::ptr &event) override {
        os << event->getFile();
    }
};

/**
 * @brief 行号格式化项（%l）
 */
class LineFormatItem : public LogFormatter::FormatItem {
public:
    /**
     * @brief 构造函数
     * @param[in] fmt 格式参数（未使用）
     */
    explicit LineFormatItem(const QString &) {}

    /**
     * @brief 格式化输出行号
     * @param[in,out] os 输出流
     * @param[in] event 日志事件
     */
    void format(QTextStream &os,
        const LogEvent::ptr &event) override {
        os << event->getLine();
    }
};

/**
 * @brief 换行格式化项（%n）
 */
class NewLineFormatItem : public LogFormatter::FormatItem {
public:
    /**
     * @brief 构造函数
     * @param[in] fmt 格式参数（未使用）
     */
    explicit NewLineFormatItem(const QString &) {}

    /**
     * @brief 输出换行符
     * @param[in,out] os 输出流
     * @param[in] event 日志事件（未使用）
     */
    void format(QTextStream &os,
        const LogEvent::ptr &) override {
        os << '\n';
    }
};

/**
 * @brief 普通字符串格式化项
 * @details 用于输出格式模板中非占位符的普通文本
 */
class StringFormatItem : public LogFormatter::FormatItem {
public:
    /**
     * @brief 构造函数
     * @param[in] str 要输出的字符串
     */
    explicit StringFormatItem(const QString &str)
        : m_string(str) {
    }

    /**
     * @brief 输出字符串
     * @param[in,out] os 输出流
     * @param[in] event 日志事件（未使用）
     */
    void format(QTextStream &os,
        const LogEvent::ptr &) override {
        os << m_string;
    }

private:
    QString m_string;  /// 待输出的字符串
};

/**
 * @brief 制表符格式化项（%T）
 */
class TabFormatItem : public LogFormatter::FormatItem {
public:
    /**
     * @brief 构造函数
     * @param[in] fmt 格式参数（未使用）
     */
    explicit TabFormatItem(const QString &) {}

    /**
     * @brief 输出制表符
     * @param[in,out] os 输出流
     * @param[in] event 日志事件（未使用）
     */
    void format(QTextStream &os,
        const LogEvent::ptr &) override {
        os << '\t';
    }
};

/**
 * @brief 百分号格式化项（%%%）
 */
class PercentSignFormatItem : public LogFormatter::FormatItem {
public:
    /**
     * @brief 构造函数
     * @param[in] fmt 格式参数（未使用）
     */
    explicit PercentSignFormatItem(const QString &) {}

    /**
     * @brief 输出百分号
     * @param[in,out] os 输出流
     * @param[in] event 日志事件（未使用）
     */
    void format(QTextStream &os,
        const LogEvent::ptr &) override {
        os << '%';
    }
};

/**
 * @brief 格式模板解析中间结构
 */
struct PatternToken {
    int type = 0;      /// 0=普通字符串，1=格式占位符
    QString value;     /// 占位符字符
    QString format;    /// 子格式（如 %d{...} 中的 ...）
};

/**
 * @brief 构造函数
 * @param[in] pattern 格式模板字符串
 * @details 保存 pattern 并调用 init() 解析模板
 */
LogFormatter::LogFormatter(const QString &pattern)
    : m_pattern(pattern) {
    init();
}

/**
 * @brief 解析格式模板，提取模板项
 * @details 遍历 pattern 字符串：
 *          1. 识别 % 开头的占位符
 *          2. 处理 %d{...} 的嵌套格式
 *          3. 非占位符文本作为普通字符串处理
 *          4. 通过工厂映射表创建对应的 FormatItem
 */
void LogFormatter::init() {
    QVector<PatternToken> tokens;
    QString normalText;
    bool parsingString = true;
    bool error = false;

    int i = 0;
    while (i < m_pattern.size()) {
        QChar ch = m_pattern[i];
        if (ch == '%') {
            if (parsingString) {
                if (!normalText.isEmpty()) {
                    tokens.push_back({0, normalText, ""});
                }

                normalText.clear();
                parsingString = false;
                ++i;
                continue;
            } else {
                tokens.push_back({1, "%", ""});
                parsingString = true;
                ++i;
                continue;
            }
        }

        // 普通字符串
        if (parsingString) {
            normalText += ch;
            ++i;
            continue;
        }

        // pattern字符
        QString patternChar(ch);
        PatternToken token;
        token.type = 1;
        token.value = patternChar;
        parsingString = true;
        ++i;

        // 处理 %d{}
        if (patternChar == "d") {
            if (i < m_pattern.size() && m_pattern[i] == '{') {
                ++i;
                QString fmt;
                while (i < m_pattern.size() && m_pattern[i] != '}') {
                    fmt += m_pattern[i];
                    ++i;
                }

                if (i >= m_pattern.size()) {
                    qWarning() << "LogFormatter pattern error:" << "'{' not closed";
                    error = true;
                    break;
                }

                token.format = fmt;
                ++i;
            }
        }
        tokens.push_back(token);
    }

    if (!normalText.isEmpty()) {
        tokens.push_back({0, normalText, "" });
    }

    if (error) {
        m_error = true;
        return;
    }

    // FormatItem工厂：根据占位符字符创建对应的格式化项
    static QHash<QString, std::function<FormatItem::ptr(const QString &)>> s_formatItems = {

#define XX(str, C) \
    { \
        #str, \
        [](const QString& fmt) { \
            return FormatItem::ptr(new C(fmt)); \
        } \
    }

        XX(m, MessageFormatItem),
        XX(p, LevelFormatItem),
        XX(c, LoggerNameFormatItem),
        XX(d, DateTimeFormatItem),
        XX(r, ElapseFormatItem),
        XX(f, FileNameFormatItem),
        XX(l, LineFormatItem),
        XX(T, TabFormatItem),
        XX(n, NewLineFormatItem),
        XX(%, PercentSignFormatItem),

#undef XX
    };

    // 创建FormatItem
    for (const auto &token : tokens) {
        // 普通字符串
        if (token.type == 0) {
            m_items.push_back(FormatItem::ptr(new StringFormatItem(token.value)));
            continue;
        }

        // pattern项
        auto it = s_formatItems.find(token.value);
        if (it == s_formatItems.end()) {
            qWarning() << "unknown format item:" << token.value;
            error = true;
            break;
        }

        m_items.push_back(it.value()(token.format));
    }

    if (error) {
        m_error = true;
    }
}

/**
 * @brief 对日志事件进行格式化
 * @param[in] event 日志事件
 * @return 格式化后的日志字符串
 * @details 遍历所有 FormatItem，依次对 event 执行格式化并拼接结果
 */
QString LogFormatter::format(const QSharedPointer<LogEvent> &event) {
    QString result;
    QTextStream os(&result);
    for (const auto &item : m_items) {
        item->format(os, event);
    }

    return result;
}

/**
 * @brief 构造函数
 * @param[in] defaultFormatter 默认格式化器
 */
LogAppender::LogAppender(const QSharedPointer<LogFormatter> &defaultFormatter)
    : m_defaultFormatter(defaultFormatter) {
}

/**
 * @brief 设置自定义格式化器
 * @param[in] formatter 新的格式化器
 */
void LogAppender::setFormatter(const QSharedPointer<LogFormatter> &formatter) {
    QWriteLocker locker(&m_lock);

    m_formatter = formatter;
}

/**
 * @brief 获取当前使用的格式化器
 * @details 如果已设置自定义 formatter 则返回，否则返回默认 formatter
 * @return 格式化器指针
 */
QSharedPointer<LogFormatter> LogAppender::formatter() const {
    QReadLocker locker(&m_lock);
    return m_formatter
        ? m_formatter
        : m_defaultFormatter;
}

/**
 * @brief 构造函数
 * @details 使用默认 pattern 创建 LogFormatter，输出流绑定到 stdout
 */
StdoutLogAppender::StdoutLogAppender()
    : LogAppender(LogFormatter::ptr(new LogFormatter))
    , m_stream(stdout) {
}

/**
 * @brief 写日志到标准输出
 * @param[in] event 日志事件
 * @details 格式化日志事件并写入 stdout，立即 flush 确保输出
 */
void StdoutLogAppender::log(const LogEvent::ptr &event) {
    QSharedPointer<LogFormatter> formatter;
    {
        QReadLocker locker(&m_lock);
        formatter = m_formatter ? m_formatter : m_defaultFormatter;
    }

    if (!formatter) {
        return;
    }

    m_stream << formatter->format(event);
    m_stream.flush();
}

/**
 * @brief 转为 YAML 字符串
 * @return YAML 字符串，包含 type 和 pattern
 */
QString StdoutLogAppender::toYamlString() const {
    QReadLocker locker(&m_lock);
    YAML::Node node;
    node["type"] = "StdoutLogAppender";
    auto formatter = m_formatter ? m_formatter : m_defaultFormatter;

    if (formatter) {
        node["pattern"] = formatter->pattern().toStdString();
    }

    std::stringstream ss;
    ss << node;
    return QString::fromStdString(ss.str());
}

/**
 * @brief 构造函数
 * @param[in] logDir 日志目录路径
 * @details 确保目录存在，按当前日期创建日志文件并打开
 */
FileLogAppender::FileLogAppender(const QString &logDir)
    : LogAppender(LogFormatter::ptr(new LogFormatter))
    , m_logDir(logDir), m_stream(&m_file) {

    ensureLogPathExist();
    createNewFile();
    if (!reopen()) {
        qCritical() << "Open log file failed:" << m_fileName;
    }
}

/**
 * @brief 确保日志目录存在
 * @details 如果目录不存在则递归创建
 */
void FileLogAppender::ensureLogPathExist() {
    QDir dir;
    if (!dir.exists(m_logDir)) {
        dir.mkpath(m_logDir);
    }
}

/**
 * @brief 根据日期构建日志文件路径
 * @param[in] date 日期
 * @return 完整文件路径，格式为 logDir/yyyy-MM-dd.log
 */
QString FileLogAppender::buildFileName(const QDate &date) const {
    return QString("%1/%2.log").arg(m_logDir).arg(date.toString("yyyy-MM-dd"));
}

/**
 * @brief 创建新的日志文件
 * @details 按当前日期生成文件名，关闭旧文件并以追加模式打开新文件
 */
void FileLogAppender::createNewFile() {
    QMutexLocker locker(&m_mutex);

    m_currentDate = QDate::currentDate();

    m_fileName = buildFileName(m_currentDate);

    if (m_file.isOpen()) {
        m_file.close();
    }

    m_file.setFileName(m_fileName);

    if (!m_file.open(QIODevice::WriteOnly |
        QIODevice::Append |
        QIODevice::Text)) {
        m_hasError = true;

        qCritical() << "Create log file failed:"
            << m_fileName;

        return;
    }

    m_hasError = false;
}

/**
 * @brief 重新打开日志文件
 * @details 关闭当前文件并以追加模式重新打开，用于文件轮转恢复
 * @return true 打开成功，false 打开失败
 */
bool FileLogAppender::reopen() {
    QMutexLocker locker(&m_mutex);

    if (m_file.isOpen()) {
        m_file.close();
    }

    m_file.setFileName(m_fileName);

    bool ok = m_file.open(QIODevice::WriteOnly |
        QIODevice::Append |
        QIODevice::Text);

    m_hasError = !ok;

    return ok;
}

/**
 * @brief 写日志到文件
 * @param[in] event 日志事件
 * @details 写入前执行两个检查：
 *          1. 日期切换：当前日期与文件日期不同时创建新文件
 *          2. 定期 reopen：每3秒关闭重开文件，确保日志文件在外部删除/移动后能重建
 */
void FileLogAppender::log(const LogEvent::ptr &event) {
    // 日期切换
    QDate today = QDate::currentDate();
    if (today != m_currentDate) {
        createNewFile();
    }

    // 每3秒 reopen 一次
    qint64 now = QDateTime::currentSecsSinceEpoch();
    if (now >= m_lastReopenTime + 3) {
        reopen();
        if (m_hasError) {
            qCritical() << "Reopen log file failed:"
                << m_fileName;
            return;
        }
        m_lastReopenTime = now;
    }

    if (m_hasError) {
        return;
    }

    QSharedPointer<LogFormatter> formatter;
    {
        QReadLocker locker(&m_lock);
        formatter = m_formatter ? m_formatter : m_defaultFormatter;
    }

    if (!formatter) {
        return;
    }

    m_stream << formatter->format(event);
    m_stream.flush();
}

/**
 * @brief 转为 YAML 字符串
 * @return YAML 字符串，包含 type、file 和 pattern
 */
QString FileLogAppender::toYamlString() const {
    QMutexLocker locker(&m_mutex);

    YAML::Node node;
    node["type"] = "FileLogAppender";
    node["file"] = m_fileName.toStdString();

    if (m_formatter) {
        node["pattern"] = m_formatter->pattern().toStdString();
    } else {
        node["pattern"] = m_defaultFormatter->pattern().toStdString();
    }

    std::stringstream stream;
    stream << node;
    return QString::fromStdString(stream.str());
}

/**
 * @brief 构造函数
 * @param[in] name 日志器名称
 * @details 记录创建时间，日志级别初始为 NOTSET
 */
Logger::Logger(const QString &name)
    : m_name(name)
    , m_createTime(QDateTime::currentDateTime()) {
}

/**
 * @brief 获取日志器名称
 * @return 名称字符串
 */
QString Logger::name() const {
    return m_name;
}

/**
 * @brief 获取创建时间
 * @return 创建时的 QDateTime
 */
QDateTime Logger::createTime() const {
    return m_createTime;
}

/**
 * @brief 设置日志级别
 * @param[in] level 新的日志级别
 */
void Logger::setLevel(LogLevel::Level level) {
    QMutexLocker locker(&m_mutex);
    m_level = level;
}

/**
 * @brief 获取日志级别
 * @return 当前日志级别
 */
LogLevel::Level Logger::level() const {
    QMutexLocker locker(&m_mutex);
    return m_level;
}

/**
 * @brief 添加日志输出器
 * @param[in] appender 日志输出器
 * @details 如果列表中已包含相同的 appender 则忽略，避免重复输出
 */
void Logger::addAppender(const LogAppender::ptr &appender) {
    QMutexLocker locker(&m_mutex);
    if (!m_appenders.contains(appender)) {
        m_appenders.append(appender);
    }
}

/**
 * @brief 移除指定的日志输出器
 * @param[in] appender 要移除的日志输出器
 */
void Logger::removeAppender(const LogAppender::ptr &appender) {
    QMutexLocker locker(&m_mutex);
    m_appenders.removeAll(appender);
}

/**
 * @brief 清空所有日志输出器
 */
void Logger::clearAppenders() {
    QMutexLocker locker(&m_mutex);
    m_appenders.clear();
}

/**
 * @brief 写日志
 * @param[in] event 日志事件
 * @details 如果事件级别高于当前日志器级别则忽略；
 *          加锁复制 appender 列表后释放锁再逐个输出，避免持锁期间日志写入阻塞其他线程
 */
void Logger::log(const LogEvent::ptr &event) {
    QList<LogAppender::ptr> appenders;
    {
        QMutexLocker locker(&m_mutex);
        if (event->getLevel() > m_level) {
            return;
        }
        appenders = m_appenders;
    }

    // 不持锁写日志
    for (const auto &appender : appenders) {
        if (appender) {
            appender->log(event);
        }
    }
}

/**
 * @brief 转为 YAML 字符串
 * @return YAML 字符串，包含 name、level 和 appenders 列表
 */
QString Logger::toYamlString() const {
    QMutexLocker locker(&m_mutex);

    YAML::Node node;

    node["name"] =
        m_name.toStdString();

    node["level"] = LogLevel::ToQString(m_level).toStdString();
    for (const auto &appender : m_appenders) {
        if (!appender) {
            continue;
        }

        node["appenders"].push_back(YAML::Load(appender->toYamlString().toStdString()));
    }

    std::stringstream stream;
    stream << node;
    return QString::fromStdString(stream.str());
}

/**
 * @brief 构造函数
 * @param[in] logger 日志器
 * @param[in] event 日志事件
 */
LogEventWrap::LogEventWrap(Logger::ptr logger, LogEvent::ptr event)
    : m_logger(logger)
    , m_event(event) {
}

/**
 * @brief 析构函数
 * @details LogEventWrap 在析构时通过日志器写日志事件
 */
LogEventWrap::~LogEventWrap() {
    m_logger->log(m_event);
}

/**
 * @brief 获取 LoggerManager 单例
 * @details 利用 C++11 Magic Statics 保证线程安全的懒加载
 * @return 全局唯一的 LoggerManager 实例引用
 */
LoggerManager &LoggerManager::instance() {
    static LoggerManager instance;

    return instance;
}

/**
 * @brief 构造函数
 * @details 启动累计运行计时器，创建 root logger（名称 "root"），
 *          添加 StdoutLogAppender 作为默认输出，最后调用 init()
 */
LoggerManager::LoggerManager() {

    m_elapsedTimer.start();

    m_rootLogger = Logger::ptr::create("root");
    m_rootLogger->addAppender(LogAppender::ptr(new StdoutLogAppender));
    m_loggers.insert(m_rootLogger->name(), m_rootLogger);
    init();
}

/**
 * @brief 初始化日志系统
 * @details 预留从 YAML 配置初始化日志系统的入口（暂未实现）
 */
void LoggerManager::init() {
    // TODO:
    // 从 YAML 配置初始化日志系统
}

/**
 * @brief 获取程序累计运行毫秒数
 * @return 从 LoggerManager 构造到当前的毫秒数
 */
qint64 LoggerManager::elapsedMs() const {
    return m_elapsedTimer.elapsed();
}

/**
 * @brief 获取指定名称的日志器
 * @param[in] name 日志器名称
 * @details 从哈希表中查找，如果不存在则创建新日志器：
 *          继承 root logger 的日志级别，并添加 StdoutLogAppender 作为默认输出
 * @return 日志器指针
 */
Logger::ptr LoggerManager::logger(const QString &name) {
    QMutexLocker locker(&m_mutex);
    auto it = m_loggers.find(name);
    if (it != m_loggers.end()) {
        return it.value();
    }

    // 创建新的 logger
    auto logger = Logger::ptr::create(name);

    // 继承 root logger 的日志级别
    logger->setLevel(m_rootLogger->level());

    logger->addAppender(LogAppender::ptr(new StdoutLogAppender));
    m_loggers.insert(name, logger);

    return logger;
}

/**
 * @brief 获取 root 日志器
 * @return root 日志器指针
 */
Logger::ptr LoggerManager::rootLogger() const {
    return m_rootLogger;
}

/**
 * @brief 将所有日志器配置转为 YAML 字符串
 * @return YAML Sequence 字符串，每个元素为一个日志器的配置
 */
QString LoggerManager::toYamlString() const {
    QList<Logger::ptr> loggers;
    {
        QMutexLocker locker(&m_mutex);
        loggers = m_loggers.values();
    }

    YAML::Node node;
    for (const auto &logger : loggers) {
        if (!logger) {
            continue;
        }
        node.push_back(YAML::Load(logger->toYamlString().toStdString()));
    }

    std::stringstream stream;
    stream << node;
    return QString::fromStdString(stream.str());
}

// ============================================================
// LogDefine LexicalCast 特化 — 支持 QSet<LogDefine> 的 YAML 序列化
// ============================================================

/**
 * @brief std::string → LogDefine 类型转换特化
 * @param[in] v YAML 字符串
 * @return 解析后的 LogDefine 对象
 * @exception 当 name 字段不存在时抛出 std::logic_error
 */
template<>
class LexicalCast<std::string, LogDefine> {
public:
    LogDefine operator()(const std::string &v) {
        auto yamlToStr = [](const YAML::Node &node) -> QString {
            std::stringstream ss;
            ss << node;
            return QString::fromStdString(ss.str());
        };

        YAML::Node n = YAML::Load(v);
        LogDefine ld;
        if (!n["name"].IsDefined()) {
            qWarning() << "log config error: name is null," << yamlToStr(n);
            throw std::logic_error("log config name is null");
        }
        ld.name = QString::fromStdString(n["name"].as<std::string>());
        ld.level = LogLevel::FromQString(
            n["level"].IsDefined() ? QString::fromStdString(n["level"].as<std::string>()) : QString());

        if (n["appenders"].IsDefined()) {
            for (size_t i = 0; i < n["appenders"].size(); i++) {
                auto a = n["appenders"][i];
                if (!a["type"].IsDefined()) {
                    qWarning() << "log appender config error: appender type is null," << yamlToStr(a);
                    continue;
                }
                QString type = QString::fromStdString(a["type"].as<std::string>());
                LogAppenderDefine lad;
                if (type == "FileLogAppender") {
                    lad.type = 1;
                    if (!a["file"].IsDefined()) {
                        qWarning() << "log appender config error: file appender file is null," << yamlToStr(a);
                        continue;
                    }
                    lad.file = QString::fromStdString(a["file"].as<std::string>());
                    if (a["pattern"].IsDefined()) {
                        lad.pattern = QString::fromStdString(a["pattern"].as<std::string>());
                    }
                } else if (type == "StdoutLogAppender") {
                    lad.type = 2;
                    if (a["pattern"].IsDefined()) {
                        lad.pattern = QString::fromStdString(a["pattern"].as<std::string>());
                    }
                } else {
                    qWarning() << "log appender config error: appender type is invalid," << yamlToStr(a);
                    continue;
                }
                ld.appenders.push_back(lad);
            }
        }
        return ld;
    }
};

/**
 * @brief LogDefine → std::string 类型转换特化
 * @param[in] i LogDefine 对象
 * @return YAML 字符串
 */
template<>
class LexicalCast<LogDefine, std::string> {
public:
    std::string operator()(const LogDefine &i) {
        YAML::Node n;
        n["name"] = i.name.toStdString();
        n["level"] = LogLevel::ToQString(i.level).toStdString();
        for (auto &a : i.appenders) {
            YAML::Node na;
            if (a.type == 1) {
                na["type"] = "FileLogAppender";
                na["file"] = a.file.toStdString();
            } else if (a.type == 2) {
                na["type"] = "StdoutLogAppender";
            }
            if (!a.pattern.isEmpty()) {
                na["pattern"] = a.pattern.toStdString();
            }
            n["appenders"].push_back(na);
        }
        std::stringstream ss;
        ss << n;
        return ss.str();
    }
};

// ============================================================
// 日志配置注册与监听
// ============================================================

static ConfigVar<QSet<LogDefine>>::ptr g_logDefines =
    zch::Config::lookup("logs", QSet<LogDefine>(), "logs config");

/**
 * @brief 日志配置初始化器
 * @details 注册配置变更回调，当 YAML 中 logs 配置变化时：
 *          1. 遍历新配置，为新增或变更的 Logger 设置 level 和 appenders
 *          2. 遍历旧配置，将被移除的 Logger 重置为 NOTSET 并清空 appenders
 */
struct LogIniter {
    LogIniter() {
        g_logDefines->addListener([](const QSet<LogDefine> &oldValue, const QSet<LogDefine> &newValue) {
            LOG_INFO(LOG_ROOT()) << "on log config changed";
            for (auto &i : newValue) {
                auto it = oldValue.find(i);
                Logger::ptr logger;
                if (it == oldValue.end()) {
                    logger = LOG_NAME(i.name);
                } else {
                    if (!(i == *it)) {
                        logger = LOG_NAME(i.name);
                    } else {
                        continue;
                    }
                }
                logger->setLevel(i.level);
                logger->clearAppenders();
                for (auto &a : i.appenders) {
                    LogAppender::ptr ap;
                    if (a.type == 1) {
                        ap = LogAppender::ptr(new FileLogAppender(a.file));
                    } else if (a.type == 2) {
                        ap = LogAppender::ptr(new StdoutLogAppender);
                    }
                    if (!a.pattern.isEmpty()) {
                        ap->setFormatter(LogFormatter::ptr(new LogFormatter(a.pattern)));
                    } else {
                        ap->setFormatter(LogFormatter::ptr(new LogFormatter));
                    }
                    logger->addAppender(ap);
                }
            }

            for (auto &i : oldValue) {
                auto it = newValue.find(i);
                if (it == newValue.end()) {
                    auto logger = LOG_NAME(i.name);
                    logger->setLevel(LogLevel::NOTSET);
                    logger->clearAppenders();
                }
            }
        });
    }
};

static LogIniter s_logIniter;

}
