#include "logger.h"
#include <cstdarg>
#include <iomanip>

bool verbose = true;

Logger &Logger::Instance() {
    static Logger instance;
    return instance;
}

Logger::Logger() {
    logFile.open("log.txt", std::ios::out | std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Failed to open log file." << std::endl;
    }
}

Logger::~Logger() {
    if (logFile.is_open()) {
        logFile.close();
    }
}

void Logger::SetLogMode(LogMode mode) {
    logMode = mode;
}

void Logger::LogToFile(const std::string &format, ...) {
    std::lock_guard<std::mutex> lock(logMutex);

    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm now_tm;
    localtime_r(&now_time_t, &now_tm);

    logFile << "[" << std::setw(2) << std::setfill('0') << now_tm.tm_hour << ":" << std::setw(2)
            << std::setfill('0') << now_tm.tm_min << ":" << std::setw(2) << std::setfill('0')
            << now_tm.tm_sec << "." << std::setw(3) << std::setfill('0') << now_ms.count()
            << "] | ";

    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format.c_str(), args);
    va_end(args);

    logFile << buffer << std::endl;
}

void Logger::LogToTerminal(const std::string &format, ...) {
    std::lock_guard<std::mutex> lock(logMutex);

    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm now_tm;
    localtime_r(&now_time_t, &now_tm);

    std::cout << "[" << std::setw(2) << std::setfill('0') << now_tm.tm_hour << ":" << std::setw(2)
              << std::setfill('0') << now_tm.tm_min << ":" << std::setw(2) << std::setfill('0')
              << now_tm.tm_sec << "." << std::setw(3) << std::setfill('0') << now_ms.count()
              << "] | ";

    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format.c_str(), args);
    va_end(args);

    std::cout << buffer << std::endl;
}

int LogPrint(int line, const std::string &func, const std::string &format, ...) {
    std::lock_guard<std::mutex> lock(Logger::Instance().logMutex);

    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm now_tm;
    localtime_r(&now_time_t, &now_tm);

    std::ofstream &logFile = Logger::Instance().logFile;
    std::ostream &output = (Logger::Instance().logMode == LogMode::File) ? logFile : std::cout;

    output << "[" << std::setw(2) << std::setfill('0') << now_tm.tm_hour << ":" << std::setw(2)
           << std::setfill('0') << now_tm.tm_min << ":" << std::setw(2) << std::setfill('0')
           << now_tm.tm_sec << "." << std::setw(3) << std::setfill('0') << now_ms.count() << "] | ";

    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format.c_str(), args);
    va_end(args);

    output << buffer << std::endl;

    if (Logger::Instance().logMode == LogMode::Both) {
        logFile << buffer << std::endl;
    }

    return static_cast<int>(output.tellp());
}
