#include "Log.h"

namespace zch {

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

class MessageFormatItem : public LogFormatter::FormatItem {
public:
	explicit MessageFormatItem(const QString &) {}

	void format(QTextStream &os,
		const LogEvent::ptr &event) override {
		os << event->getContent();
	}
};

class LevelFormatItem : public LogFormatter::FormatItem {
public:
	explicit LevelFormatItem(const QString &) {}

	void format(QTextStream &os,
		const LogEvent::ptr &event) override {
		os << LogLevel::ToQString(event->getLevel());
	}
};

class ElapseFormatItem : public LogFormatter::FormatItem {
public:
	explicit ElapseFormatItem(const QString &) {}

	void format(QTextStream &os,
		const LogEvent::ptr &event) override {
		os << event->getElapse();
	}
};

class LoggerNameFormatItem : public LogFormatter::FormatItem {
public:
	explicit LoggerNameFormatItem(const QString &) {}

	void format(QTextStream &os,
		const LogEvent::ptr &event) override {
		os << event->getLoggerName();
	}
};

class DateTimeFormatItem : public LogFormatter::FormatItem {
public:
	explicit DateTimeFormatItem(
		const QString &format = "yyyy-MM-dd HH:mm:ss") : m_format(format) {
		if (m_format.isEmpty()) {
			m_format = "yyyy-MM-dd HH:mm:ss";
		}
	}

	void format(QTextStream &os,
		const LogEvent::ptr &event) override {
		os << event->getTime().toString(m_format);
	}

private:
	QString m_format;
};

class FileNameFormatItem : public LogFormatter::FormatItem {
public:
	explicit FileNameFormatItem(const QString &) {}

	void format(QTextStream &os,
		const LogEvent::ptr &event) override {
		os << event->getFile();
	}
};

class LineFormatItem : public LogFormatter::FormatItem {
public:
	explicit LineFormatItem(const QString &) {}

	void format(QTextStream &os,
		const LogEvent::ptr &event) override {
		os << event->getLine();
	}
};

class NewLineFormatItem : public LogFormatter::FormatItem {
public:
	explicit NewLineFormatItem(const QString &) {}

	void format(QTextStream &os,
		const LogEvent::ptr &) override {
		os << '\n';
	}
};

class StringFormatItem : public LogFormatter::FormatItem {
public:
	explicit StringFormatItem(const QString &str)
		: m_string(str) {
	}

	void format(QTextStream &os,
		const LogEvent::ptr &) override {
		os << m_string;
	}

private:
	QString m_string;
};

class TabFormatItem : public LogFormatter::FormatItem {
public:
	explicit TabFormatItem(const QString &) {}

	void format(QTextStream &os,
		const LogEvent::ptr &) override {
		os << '\t';
	}
};

class PercentSignFormatItem : public LogFormatter::FormatItem {
public:
	explicit PercentSignFormatItem(const QString &) {}

	void format(QTextStream &os,
		const LogEvent::ptr &) override {
		os << '%';
	}
};

struct PatternToken {
	int type = 0;
	QString value;
	QString format;
};

LogFormatter::LogFormatter(const QString &pattern)
	: m_pattern(pattern) {
	init();
}

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

	// FormatItem工厂
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

QString LogFormatter::format(const QSharedPointer<LogEvent> &event) {
	QString result;
	QTextStream os(&result);
	for (const auto &item : m_items) {
		item->format(os, event);
	}

	return result;
}

LogAppender::LogAppender(const QSharedPointer<LogFormatter> &defaultFormatter)
	: m_defaultFormatter(defaultFormatter) {
}

void LogAppender::setFormatter(const QSharedPointer<LogFormatter> &formatter) {
	QWriteLocker locker(&m_lock);

	m_formatter = formatter;
}

QSharedPointer<LogFormatter> LogAppender::formatter() const {
	QReadLocker locker(&m_lock);
	return m_formatter
		? m_formatter
		: m_defaultFormatter;
}

StdoutLogAppender::StdoutLogAppender()
	: LogAppender(LogFormatter::ptr(new LogFormatter))
	, m_stream(stdout) {
}

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

FileLogAppender::FileLogAppender(const QString &logDir) : m_logDir(logDir), m_stream(&m_file) {

	ensureLogPathExist();
	createNewFile();
	if (!reopen()) {
		qCritical() << "Open log file failed:" << m_fileName;
	}
}

void FileLogAppender::ensureLogPathExist() {
	QDir dir;
	if (!dir.exists(m_logDir)) {
		dir.mkpath(m_logDir);
	}
}

QString FileLogAppender::buildFileName(const QDate &date) const {
	return QString("%1/%2.log").arg(m_logDir).arg(date.toString("yyyy-MM-dd"));
}

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

Logger::Logger(const QString &name)
	: m_name(name)
	, m_createTime(QDateTime::currentDateTime()) {

}

QString Logger::name() const {
	return m_name;
}

QDateTime Logger::createTime() const {
	return m_createTime;
}

void Logger::setLevel(LogLevel::Level level) {
	QMutexLocker locker(&m_mutex);
	m_level = level;
}

LogLevel::Level Logger::level() const {
	QMutexLocker locker(&m_mutex);
	return m_level;
}

void Logger::addAppender(const LogAppender::ptr &appender) {
	QMutexLocker locker(&m_mutex);
	if (!m_appenders.contains(appender)) {
		m_appenders.append(appender);
	}
}

void Logger::removeAppender(const LogAppender::ptr &appender) {
	QMutexLocker locker(&m_mutex);
	m_appenders.removeAll(appender);
}

void Logger::clearAppenders() {
	QMutexLocker locker(&m_mutex);
	m_appenders.clear();
}

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

LogEventWrap::LogEventWrap(Logger::ptr logger, LogEvent::ptr event)
	: m_logger(logger)
	, m_event(event) {
}

/**
 * @note LogEventWrap在析构时写日志
 */
LogEventWrap::~LogEventWrap() {
	m_logger->log(m_event);
}

LoggerManager &LoggerManager::instance() {
	static LoggerManager instance;

	return instance;
}

LoggerManager::LoggerManager() {
	m_rootLogger = Logger::ptr::create("root");
	m_rootLogger->addAppender(LogAppender::ptr::create(new StdoutLogAppender));
	m_loggers.insert(m_rootLogger->name(), m_rootLogger);
	init();
}

void LoggerManager::init() {
	// TODO:
	// 从 YAML 配置初始化日志系统
}

Logger::ptr LoggerManager::logger(const QString &name) {
	QMutexLocker locker(&m_mutex);
	auto it = m_loggers.find(name);
	if (it != m_loggers.end()) {
		return it.value();
	}

	// 创建新的 logger
	auto logger = Logger::ptr::create(name);

	// 继承 root logger appenders
	logger->setLevel(m_rootLogger->level());

	logger->addAppender(LogAppender::ptr(new StdoutLogAppender));
	m_loggers.insert(name, logger);

	return logger;
}

Logger::ptr LoggerManager::rootLogger() const {
	return m_rootLogger;
}

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

}
