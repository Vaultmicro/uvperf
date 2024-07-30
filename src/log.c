#include <sys/time.h>
#include <time.h>

#include "log.h"

void ShowUsage() {
    LOG_MSG("Version : V1.1.1\n");
    LOG_MSG("\n");
    LOG_MSG(
        "Usage: uvperf -v VID -p PID -i INTERFACE -a AltInterface -e ENDPOINT -m TRANSFERMODE "
        "-T TIMER -t TIMEOUT -f FileIO -b BUFFERCOUNT-l READLENGTH -w WRITELENGTH -r REPEAT -S \n");
    LOG_MSG("\t-v VID           USB Vendor ID\n");
    LOG_MSG("\t-p PID           USB Product ID\n");
    LOG_MSG("\t-i INTERFACE     USB Interface\n");
    LOG_MSG("\t-a AltInterface  USB Alternate Interface\n");
    LOG_MSG("\t-e ENDPOINT      USB Endpoint\n");
    LOG_MSG("\t-m TRANSFER      0 = isochronous, 1 = bulk\n");
    LOG_MSG("\t-T TIMER         Timer in seconds\n");
    LOG_MSG("\t-t TIMEOUT       USB Transfer Timeout\n");
    LOG_MSG("\t-f FileIO        Use file I/O, default : FALSE\n");
    LOG_MSG("\t-b BUFFERCOUNT   Number of buffers to use\n");
    LOG_MSG("\t-l READLENGTH    Length of read transfers\n");
    LOG_MSG("\t-w WRITELENGTH   Length of write transfers\n");
    LOG_MSG("\t-r REPEAT        Number of transfers to perform\n");
    LOG_MSG("\t-S               Show transfer data, default : FALSE\n");
    LOG_MSG("\n");
    LOG_MSG("Example:\n");
    LOG_MSG("uvperf -v 0x1004 -p 0xa000 -i 0 -a 0 -e 0x81 -m 0 -t 1000 -l 1024 -r 1000 -R\n");
    LOG_MSG("This will perform 1000 bulk transfers of 1024 bytes to endpoint 0x81\n");
    LOG_MSG("on interface 0, alternate setting 0 of a device with VID 0x1004 and PID 0xA000.\n");
    LOG_MSG("The transfers will have a timeout of 1000ms.\n");

    LOG_MSG("\n");
}


int LogPrint(const int line, const char *func, const char *format, ...)
{
    int charsNo;
    va_list ap;
    struct tm rt;
    struct timeval tv;

    gettimeofday(&tv, NULL);
    time_t cur = tv.tv_sec;
    errno_t result = localtime_s(&rt, &cur);
    if (result)
    {
        fprintf(stderr, "Failed to convert time.\n");
        return -1;
    }

    printf("[%02d:%02d:%02d.%03d] | ", rt.tm_hour, rt.tm_min, rt.tm_sec, (int)(tv.tv_usec / 1000));

    va_start(ap, format);
    charsNo = vprintf(format, ap);
    va_end(ap);

    return charsNo;
}