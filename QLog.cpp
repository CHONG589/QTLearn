#include "QLog.h"

/**
 * @brief 构造 FileSink 并打开初始日志文件
 * @param[in] dir 日志目录路径
 * @param[in] maxFileSize 单个文件大小上限（字节）
 * @details 以追加模式打开文件（避免覆盖已有日志），
 *          使用 Text 模式确保跨平台换行符一致。
 */
FileSink::FileSink(const QString &dir, qint64 maxFileSize)
    : m_logDir(dir)
    , m_maxFileSize(maxFileSize)
{
    QDir().mkpath(dir);

    m_currentDate = QDate::currentDate().toString("yyyy-MM-dd");
    QString fileName = dir + "/" + m_currentDate + ".log";

    m_file.setFileName(fileName);
    // 追加模式：保留历史日志；Text 模式：自动处理 \\r\\n 转换
    m_file.open(QIODevice::Append | QIODevice::Text);

    m_stream.setDevice(&m_file);
}

/**
 * @brief 写入日志到文件
 * @param[in] formattedMsg 已格式化的日志字符串
 * @details 加锁 → 滚动检查 → 写入 → flush 四个步骤在同一临界区内，
 *          确保滚动决策与写入之间不会有其他线程插入写入。
 */
void FileSink::write(const QString &formattedMsg) {
    QMutexLocker locker(&m_mutex);

    rotateIfNeeded();

    m_stream << formattedMsg;
    // 每次写入后立即 flush：日志场景对延迟不敏感，但丢失日志不可接受
    m_stream.flush();
}

/**
 * @brief 强制刷新文件缓冲区到磁盘
 * @details 同时 flush QTextStream（用户态缓冲区）和 QFile（内核态缓冲区），
 *          确保数据真正落盘而非仅停留在 libc 缓冲区。
 */
void FileSink::flush() {
    QMutexLocker locker(&m_mutex);
    m_stream.flush();
    m_file.flush();
}

/**
 * @brief 检查并执行日志文件滚动
 * @details 两个独立维度：
 *          - 按天：当前日期与 m_currentDate 不同则创建新日期文件
 *          - 按大小：文件超过 m_maxFileSize 则重命名当前文件为 日期_时分秒.log，
 *            再创建新的日期文件
 *          文件操作失败时通过 qWarning 告警，不中断写入流程
 */
void FileSink::rotateIfNeeded() {
    QString today = QDate::currentDate().toString("yyyy-MM-dd");

    // 按天切
    if (today != m_currentDate) {
        m_file.close();

        m_currentDate = today;
        QString newFile = m_logDir + "/" + today + ".log";

        m_file.setFileName(newFile);
        if (!m_file.open(QIODevice::Append)) {
            qWarning() << "FileSink: failed to open" << newFile;
        }
    }

    // 按大小切
    if (m_file.size() > m_maxFileSize) {
        m_file.close();

        QString newName = QString("%1/%2_%3.log")
            .arg(m_logDir)
            .arg(m_currentDate)
            .arg(QDateTime::currentDateTime().toString("hhmmss"));

        if (!m_file.rename(newName)) {
            qWarning() << "FileSink: failed to rename log to" << newName;
        }

        m_file.setFileName(m_logDir + "/" + m_currentDate + ".log");
        if (!m_file.open(QIODevice::Append)) {
            qWarning() << "FileSink: failed to reopen log after rotation";
        }
    }
}

/**
 * @brief 输出日志到调试控制台
 * @param[in] formattedMsg 已格式化的日志字符串
 * @details 使用 noquote() 避免 Qt 给字符串自动添加引号（日志本身已有完整格式）
 */
void ConsoleSink::write(const QString &formattedMsg) {
    qDebug().noquote() << formattedMsg;
}

/**
 * @brief 将日志条目格式化为经典纯文本
 * @param[in] entry 结构化日志条目
 * @return 格式化后的单行文本，末尾带换行符
 * @details 输出格式：yyyy-MM-dd hh:mm:ss[LEVEL][file:line] message
 *          使用 const char* 而非 QString 存储级别名，避免每次格式化的内存分配
 */
QString SimpleFormatter::format(const LogEntry &entry) {
    QString time = entry.timestamp.toString("yyyy-MM-dd hh:mm:ss");

    // 用 const char* 而非 QString 避免在热路径上构造临时对象
    const char *levelStr = "UNKNOWN";
    switch (entry.level) {
        case LogLevel::DEBUG: levelStr = "DEBUG"; break;
        case LogLevel::INFO:  levelStr = "INFO";  break;
        case LogLevel::WARN:  levelStr = "WARN";  break;
        case LogLevel::ERROR: levelStr = "ERROR"; break;
        default: break;
    }

    return QString("%1[%2][%3:%4] %5\n")
        .arg(time)
        .arg(levelStr)
        .arg(entry.file)
        .arg(entry.line)
        .arg(entry.message);
}

/**
 * @brief 获取 Logger 单例
 * @return Logger& 全局唯一实例
 * @details 利用 C++11 Magic Statics：局部静态变量的初始化是线程安全的，
 *          无需额外加锁
 */
Logger &Logger::instance() {
    static Logger inst;
    return inst;
}

Logger::Logger() {}

Logger::~Logger() {
    stop();
}

/**
 * @brief 初始化日志系统
 * @param[in] dir 日志文件输出目录
 * @param[in] level 最低输出级别
 * @param[in] async 是否启用异步模式
 * @param[in] maxFileSize 单个日志文件最大字节数
 * @param[in] console 是否同时输出到控制台
 * @details 构建默认的 Sink 和 Formatter。
 *          m_sinks.clear() 确保重复调用不会累积 sink。
 *          使用 QThread::create() + lambda 而非继承 QThread，
 *          因为 asyncLoop 是私有成员函数，直接用 lambda 捕获 this 更简洁。
 */
void Logger::init(const QString &dir,
                  LogLevel level,
                  bool async,
                  qint64 maxFileSize,
                  bool console) {

    m_level = level;
    m_async = async;

    m_formatter = std::make_unique<SimpleFormatter>();

    // clear() 支持重复 init()，避免每次调用追加重复的 sink
    m_sinks.clear();
    m_sinks.push_back(std::make_unique<FileSink>(dir, maxFileSize));
    if (console) {
        m_sinks.push_back(std::make_unique<ConsoleSink>());
    }

    m_running = true;

    if (m_async) {
        // 用 lambda 捕获 this 调用私有成员，比继承 QThread 更轻量
        m_thread = QThread::create([this]() { asyncLoop(); });
        m_thread->start();
    }
}

/**
 * @brief 提交日志条目
 * @details 在 LogStream 析构时被调用。
 *          时间戳在此刻（业务线程）确定，保证日志时间的准确性。
 *          异步模式仅入队 + 唤醒，I/O 在写线程完成。
 */
void Logger::log(LogLevel level, const QString &msg,
                 const char *file, int line) {

    if (level < m_level) {
        return;
    }

    LogEntry entry;
    entry.timestamp = QDateTime::currentDateTime();
    entry.level     = level;
    entry.message   = msg;
    entry.file      = file;
    entry.line      = line;

    if (m_async) {
        QMutexLocker locker(&m_mutex);
        m_queue.enqueue(entry);
        m_cond.wakeOne();
    } else {
        writeToSinks(entry);
    }
}

/**
 * @brief 异步写线程主循环
 * @details 循环等待队列消息，消费到队列空为止。
 *          关键设计：dequeue 后立即解锁 → 执行 I/O（writeToSinks）→ 重新加锁。
 *          这样 I/O 期间不持锁，业务线程可以继续入队。
 */
void Logger::asyncLoop() {
    while (m_running) {
        QMutexLocker locker(&m_mutex);

        if (m_queue.isEmpty()) {
            m_cond.wait(&m_mutex);
        }

        while (!m_queue.isEmpty()) {
            LogEntry entry = m_queue.dequeue();
            locker.unlock();

            writeToSinks(entry);

            locker.relock();
        }
    }
}

/**
 * @brief 格式化日志条目并分发到所有输出目标
 * @param[in] entry 结构化日志条目
 * @details 先用 m_formatter 格式化，加 m_sinkMutex 锁后遍历所有 sink 写入。
 *          加锁是为了与 addSink() / setFormatter() 互斥。
 */
void Logger::writeToSinks(const LogEntry &entry) {
    QString formatted = m_formatter->format(entry);

    QMutexLocker locker(&m_sinkMutex);
    for (auto &sink : m_sinks) {
        sink->write(formatted);
    }
}

/**
 * @brief 添加一个输出目标
 * @param[in] sink 输出目标实例（unique_ptr，转移所有权）
 * @details 加锁保护 m_sinks，与 writeToSinks() / flush() 互斥
 */
void Logger::addSink(std::unique_ptr<ILogSink> sink) {
    QMutexLocker locker(&m_sinkMutex);
    m_sinks.push_back(std::move(sink));
}

/**
 * @brief 替换当前格式化器
 * @param[in] formatter 新格式化器实例（unique_ptr，转移所有权）
 * @details 加锁保护 m_formatter，与 writeToSinks() 互斥
 */
void Logger::setFormatter(std::unique_ptr<ILogFormatter> formatter) {
    QMutexLocker locker(&m_sinkMutex);
    m_formatter = std::move(formatter);
}

/**
 * @brief 强制排空队列并刷新所有输出目标
 * @details 异步模式下在调用线程直接消费队列（取锁、dequeue、解锁、写入、重新加锁循环），
 *          避免与写线程的唤醒竞态。队列完全排空后再依次 flush 所有 sink。
 */
void Logger::flush() {
    if (m_async) {
        QMutexLocker locker(&m_mutex);
        while (!m_queue.isEmpty()) {
            LogEntry entry = m_queue.dequeue();
            locker.unlock();
            writeToSinks(entry);
            locker.relock();
        }
    }

    QMutexLocker locker(&m_sinkMutex);
    for (auto &sink : m_sinks) {
        sink->flush();
    }
}

/**
 * @brief 安全关闭日志系统
 * @details 步骤：设置退出标志 → 唤醒阻塞中的写线程 → 等待线程结束 → flush 所有 sink。
 *          必须在写线程 wait() 之前 wakeAll()，否则写线程可能阻塞在 m_cond.wait()
 *          中永远不被唤醒，导致 stop() 死锁。
 */
void Logger::stop() {
    m_running = false;

    {
        QMutexLocker locker(&m_mutex);
        m_cond.wakeAll();
    }

    if (m_thread) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }

    QMutexLocker locker(&m_sinkMutex);
    for (auto &sink : m_sinks) {
        sink->flush();
    }
}
