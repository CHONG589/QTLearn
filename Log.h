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

namespace zch {

/**
* @brief 日志级别
*/
class LogLevel {
public:
	enum Level {
		// 致命情况，系统不可用
		FATAL = 0,
		// 高优先级情况，例如数据库系统崩溃
		ALERT = 100,
		// 严重错误，例如硬盘错误
		CRIT = 200,
		// 错误
		ERROR = 300,
		// 警告
		WARN = 400,
		// 正常但值得注意
		NOTICE = 500,
		// 一般信息
		INFO = 600,
		// 调试信息
		DEBUG = 700,
		// 未设置
		NOTSET = 800,
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
	 */
	LogEvent(const QString &loggerName,
		LogLevel::Level level,
		const QString &file,
		qint32 line,
		qint64 elapse,
		const QDateTime &time);

	/**
	 * @brief 获取日志级别
	 */
	LogLevel::Level getLevel() const { return m_level; }

	/**
	 * @brief 获取日志内容
	 */
	QString getContent() const { return m_content; }

	/**
	 * @brief 获取文件名
	 */
	QString getFile() const { return m_file; }

	/**
	 * @brief 获取行号
	 */
	qint32 getLine() const { return m_line; }

	/**
	 * @brief 获取累计运行毫秒数
	 */
	qint64 getElapse() const { return m_elapse; }

	/**
	 * @brief 获取时间
	 */
	QDateTime getTime() const { return m_time; }

	/**
	 * @brief 获取流对象
	 */
	QTextStream &stream() { return m_stream; }

	/**
	 * @brief 获取日志器名称
	 */
	QString getLoggerName() const { return m_loggerName; }

private:
	// 日志级别
	LogLevel::Level m_level;
	// 日志内容
	QString m_content;
	// 流式写入对象
	QTextStream m_stream;
	// 文件名
	QString m_file;
	// 行号
	qint32 m_line = 0;
	// 累计耗时(ms)
	qint64 m_elapse = 0;
	// 时间
	QDateTime m_time;
	// 日志器名称
	QString m_loggerName;
};

/**
 * @brief 日志格式化
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
	 * 默认格式描述：年-月-日 时:分:秒 [累计运行毫秒数] \\t [日志级别] \\t [日志器名称] \\t 文件名:行号 \\t 日志消息 换行符
	 */
	explicit LogFormatter(const QString &pattern = "[%d{yyyy-MM-dd HH:mm:ss}][%rms][%p][%c][%f:%l] %m%n");

	/**
	 * @brief 初始化，解析格式模板，提取模板项
	 */
	void init();

	/**
	 * @brief 模板解析是否出错
	 */
	bool isError() const { return m_error; }

	/**
	 * @brief 对日志事件进行格式化，返回格式化日志文本
	 * @param[in] event 日志事件
	 * @return 格式化日志字符串
	 */
	QString format(const QSharedPointer<LogEvent> &event);

	/**
	 * @brief 获取pattern
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
		 */
		virtual void format(QTextStream &os, const QSharedPointer<LogEvent> &event) = 0;
	};

private:
	// 日志格式模板
	QString m_pattern;
	// 解析后的格式模板数组
	QVector<FormatItem::ptr> m_items;
	// 是否出错
	bool m_error = false;
};

class LogAppender {
public:
	using ptr = QSharedPointer<LogAppender>;

	explicit LogAppender(const QSharedPointer<LogFormatter> &defaultFormatter);

	virtual ~LogAppender() = default;

	/**
	 * @brief 设置formatter
	 */
	void setFormatter(const QSharedPointer<LogFormatter> &formatter);

	/**
	 * @brief 获取formatter
	 */
	QSharedPointer<LogFormatter> formatter() const;

	/**
	 * @brief 写日志
	 */
	virtual void log(const QSharedPointer<LogEvent> &event) = 0;

	/**
	 * @brief 转YAML
	 */
	virtual QString toYamlString() const = 0;

protected:
	mutable QReadWriteLock m_lock;
	// 当前formatter
	QSharedPointer<LogFormatter> m_formatter;
	// 默认formatter
	QSharedPointer<LogFormatter> m_defaultFormatter;
};

class StdoutLogAppender : public LogAppender {
public:
	using ptr = QSharedPointer<StdoutLogAppender>;

	StdoutLogAppender();

	void log(const LogEvent::ptr &event) override;

	QString toYamlString() const override;

private:
	QTextStream m_stream;
};

class FileLogAppender : public LogAppender {
public:
	using ptr = QSharedPointer<FileLogAppender>;

public:
	explicit FileLogAppender(const QString &logDir);

	void log(const LogEvent::ptr &event) override;

	bool reopen();

	QString toYamlString() const override;

private:
	void createNewFile();

	QString buildFileName(const QDate &date) const;

	void ensureLogPathExist();

private:
	// 日志目录
	QString m_logDir;
	// 当前日志文件路径
	QString m_fileName;
	// 文件对象
	QFile m_file;
	// 当前日期
	QDate m_currentDate;
	// 上次 reopen 时间
	qint64 m_lastReopenTime{0};
	// 文件错误状态
	bool m_hasError{false};
	mutable QMutex m_mutex;
	QTextStream m_stream;
};

class Logger {
public:
	using ptr = QSharedPointer<Logger>;

public:
	explicit Logger(const QString &name = "default");

	QString name() const;

	QDateTime createTime() const;

	void setLevel(LogLevel::Level level);

	LogLevel::Level level() const;

	void addAppender(const LogAppender::ptr &appender);

	void removeAppender(const LogAppender::ptr &appender);

	void clearAppenders();

	void log(const LogEvent::ptr &event);

	QString toYamlString() const;

private:
	QString m_name;
	LogLevel::Level m_level;
	QList<LogAppender::ptr> m_appenders;
	QDateTime m_createTime;
	mutable QMutex m_mutex;
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
	 */
	LogEvent::ptr GetLogEvent() const { return m_event; }

private:
	// 日志器
	Logger::ptr m_logger;
	// 日志事件
	LogEvent::ptr m_event;
};

class LoggerManager {
public:
	using ptr = QSharedPointer<LoggerManager>;

public:
	static LoggerManager &instance();

private:
	LoggerManager();

public:
	void init();

	Logger::ptr logger(
		const QString &name);

	Logger::ptr rootLogger() const;

	QString toYamlString() const;

private:
	mutable QMutex m_mutex;

	QHash<QString, Logger::ptr> m_loggers;

	Logger::ptr m_rootLogger;
};

}

