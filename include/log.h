#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdio.h>

// Forward declaration of the LogPrint function with variable arguments
int LogPrint(const int line, const char *func, const char *format, ...);

#define LOG_VERBOSE(format, ...)                                                                   \
    do {                                                                                           \
        if (verbose)                                                                               \
            printf(format, ##__VA_ARGS__);                                                         \
    } while (0)

// Helper macro to print a specific type of log data
#define LOGVDAT(format, ...) printf("[data-mismatch] " format "\n", ##__VA_ARGS__)

// Main logging macro that includes line number, function name, and custom format
#define Log(fmt, ...) LogPrint(__LINE__, __func__, fmt, ##__VA_ARGS__)

// Macros for logging with specific prefixes and the name of the current function
#define LOG(LogTypeString, format, ...) Log("[%s] " format, LogTypeString)
#define LOG_NO_FN(LogTypeString, format, ...) Log("%s " format, LogTypeString, ##__VA_ARGS__)

// Specific logging level macros that simplify usage
#define LOG_ERROR(format, ...) LOG_NO_FN("[ERROR] ", format, ##__VA_ARGS__)
#define LOG_WARNING(format, ...) LOG("WARNING", format, ##__VA_ARGS__)
#define LOG_MSG(format, ...) LOG_NO_FN("", format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) LOG("DEBUG", format, ##__VA_ARGS__)

// Simple message logging macros without additional formatting
#define LOGERR0(message) LOG_ERROR("%s", message)
#define LOGWAR0(message) LOG_WARNING("%s", message)
#define LOGMSG0(message) LOG_MSG("%s", message)
#define LOGDBG0(message) LOG_DEBUG("%s", message)

#endif // LOG_H