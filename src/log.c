#include <stdio.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>

#include "log.h"

int LogPrint(const int line, const char *func, const char *format, ...)
{
    int charsNo;
    va_list ap;
    struct tm rt;
    struct timeval tv;

    gettimeofday(&tv, NULL);
    time_t cur = tv.tv_sec;

#ifdef _WIN32
    errno_t result = localtime_s(&rt, &cur);
    if (result)
    {
        fprintf(stderr, "Failed to convert time.\n");
        return -1;
    }
#else
    if (localtime_r(&cur, &rt) == NULL)
    {
        fprintf(stderr, "Failed to convert time.\n");
        return -1;
    }
#endif

    printf("[%02d:%02d:%02d.%03d] | ", rt.tm_hour, rt.tm_min, rt.tm_sec, (int)(tv.tv_usec / 1000));

    va_start(ap, format);
    charsNo = vprintf(format, ap);
    va_end(ap);

    return charsNo;
}