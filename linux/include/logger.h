#ifndef LOGGER_H
#define LOGGER_H

#include <fstream>
#include <mutex>
#include <string>
#include <chrono>
#include <iostream>

extern bool verbose; // Declare verbose flag externally

enum class LogMode {
    Terminal,
    File,
    Both
};

class Logger {
public:
    static Logger& Instance();
    void LogToFile(const std::string& format, ...);
    void LogToTerminal(const std::string& format, ...);
    void SetLogMode(LogMode mode);
    std::ofstream logFile;
    std::mutex logMutex;
    LogMode logMode = LogMode::Terminal; // Default log mode

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void LogMessage(const std::string& message);
};

int LogPrint(int line, const std::string& func, const std::string& format, ...);

#define LOG_VERBOSE(format, ...)                                                                   \
    do {                                                                                           \
        if (verbose)                                                                               \
            Logger::Instance().LogToTermnal(format, ##__VA_ARGS__);                               \
    } while (0)

#define LOGVDAT(format, ...)                                                                       \
    Logger::Instance().LogToTerminal("[data-mismatch] " format "\n", ##__VA_ARGS__)

#define Log(fmt, ...) LogPrint(__LINE__, __func__, fmt, ##__VA_ARGS__)

#define LOG(LogTypeString, format, ...) Log("[%s] " format, LogTypeString, ##__VA_ARGS__)
#define LOG_NO_FN(LogTypeString, format, ...) Log("%s " format, LogTypeString, ##__VA_ARGS__)

#define LOG_ERROR(format, ...) LOG_NO_FN("[ERROR] ", format, ##__VA_ARGS__)
#define LOG_WARNING(format, ...) LOG("WARNING", format, ##__VA_ARGS__)
#define LOG_MSG(format, ...) LOG_NO_FN("", format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) LOG("DEBUG", format, ##__VA_ARGS__)

#define LOGERR0(message) LOG_ERROR("%s", message)
#define LOGWAR0(message) LOG_WARNING("%s", message)
#define LOGMSG0(message) LOG_MSG("%s", message)
#define LOGDBG0(message) LOG_DEBUG("%s", message)

#endif // LOGGER_H
