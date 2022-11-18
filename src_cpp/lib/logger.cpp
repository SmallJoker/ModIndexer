#include "logger.h"
#include <fstream>
#include <iomanip> // std::put_time
#include <iostream> // cout
#include <sstream>
#include <time.h> // sleep_ms

Logger *g_logger = nullptr;

void sleep_ms(long long ms)
{
	timespec duration {
		.tv_sec = 0,
		.tv_nsec = ms * 1000 * 1000,
	};
	nanosleep(&duration, nullptr);
}

void write_timestamp(std::ostream *os)
{
	std::time_t time = std::time(nullptr);
	std::tm *tm = std::localtime(&time);
	*os << std::put_time(tm, "[%T]");
}

void write_datetime(std::ostream *os)
{
	std::time_t time = std::time(nullptr);
	std::tm *tm = std::localtime(&time);
	*os << std::put_time(tm, "%c");
}

Logger::Logger()
{
	m_sink = new std::ostringstream();
}

Logger::~Logger()
{
	delete m_sink;
}

std::ostream &Logger::getStdout(LogLevel level)
{
	if (level >= m_loglevel_stdout)
		return std::cout;

	m_sink->clear();
	return *m_sink;
}

static const struct {
	const char *text;
	const char *terminal_fmt;
} s_format[LL_INVALID] = {
	{ " verbose  ", "\e[1;30m" },
	{ "          ", "\e[0;36m" },
	{ " WARNING  ", "\e[1;33m" },
	{ "  ERROR   ", "\e[0;31m" },
};

LoggerAssistant::LoggerAssistant(LogLevel level, const char *func)
{
	m_level = level;

	std::ostringstream oss;
	write_timestamp(&oss);
	*this << oss.str();

	g_logger->getStdout(m_level) << s_format[(int)level].terminal_fmt;

	*this << s_format[(int)level].text;
	if (func)
		*this << func;

	g_logger->getStdout(m_level) << "\e[0m";

	if (func)
		*this  << ": ";
}

LoggerAssistant::~LoggerAssistant()
{
	g_logger->getStdout(m_level) << std::endl;
}
