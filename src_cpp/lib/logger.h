#pragma once

#include <ostream>

#define VERBOSE(str) \
	LoggerAssistant(LL_VERBOSE, __PRETTY_FUNCTION__) << str

#define LOG(str) \
	LoggerAssistant(LL_NORMAL,  __PRETTY_FUNCTION__) << str

#define WARN(str) \
	LoggerAssistant(LL_WARNING, __PRETTY_FUNCTION__) << str

#define ERROR(str) \
	LoggerAssistant(LL_ERROR,   __PRETTY_FUNCTION__) << str

#define ASSERT(expr, msg) \
	if (!(expr)) { \
		ERROR("Assertion " << #expr << ": " << msg); \
		throw std::string("Assertion failed"); \
	}

void sleep_ms(long long ms);
#define SLEEP_MS(x) sleep_ms(x)

void write_timestamp(std::ostream *os);
void write_datetime(std::ostream *os);


enum LogLevel {
	LL_VERBOSE,
	LL_NORMAL,
	LL_WARNING,
	LL_ERROR,
	LL_INVALID
};

class Logger;
extern Logger *g_logger;

class Logger {
public:
	Logger();
	~Logger();

	void setLogLevels(LogLevel ll_stdout)
	{
		m_loglevel_stdout = ll_stdout;
	}

	std::ostream &getStdout(LogLevel level);

private:
	std::ostream *m_sink;
	LogLevel m_loglevel_stdout = LL_VERBOSE;
};

class LoggerAssistant {
public:
	LoggerAssistant(LogLevel level = LL_NORMAL, const char *func = nullptr);
	~LoggerAssistant();

	template <typename T>
	LoggerAssistant &operator<<(const T &what)
	{
		g_logger->getStdout(m_level) << what;
		return *this;
	}

private:
	LogLevel m_level;
};

