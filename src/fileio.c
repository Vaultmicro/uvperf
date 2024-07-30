#include <stdio.h>
#include <stdlib.h>

#include "fileio.h"

void FileIOOpen(PUVPERF_PARAM TestParams) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    strftime(TestParams->LogFileName, MAX_PATH - 1, "../log/uvperf_log_%Y%m%d_%H%M%S.txt", t);
    TestParams->LogFileName[MAX_PATH - 1] = '\0';

    if (TestParams->fileIO) {
        TestParams->LogFile =
            CreateFile(TestParams->LogFileName, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_ALWAYS, // Open the file if it exists; otherwise, create it
                       FILE_ATTRIBUTE_NORMAL, NULL);

        if (TestParams->LogFile == INVALID_HANDLE_VALUE) {
            LOG_ERROR("failed opening %s\n", TestParams->LogFileName);
            TestParams->fileIO = FALSE;
        }
    }
}

void FileIOLog(PUVPERF_PARAM TestParams) {
    if (!TestParams->fileIO) {
        return;
    }

    freopen(TestParams->LogFileName, "a+", stdout);
    freopen(TestParams->LogFileName, "a+", stderr);

    ShowParams(TestParams);
}

void FileIOClose(PUVPERF_PARAM TestParams) {
    if (TestParams->fileIO) {
        // if (TestParams->BufferFile != INVALID_HANDLE_VALUE) {
        //     CloseHandle(TestParams->BufferFile);
        //     TestParams->BufferFile = INVALID_HANDLE_VALUE;
        // }

        if (TestParams->LogFile != INVALID_HANDLE_VALUE) {
            fclose(stdout);
            CloseHandle(TestParams->LogFile);
            TestParams->LogFile = INVALID_HANDLE_VALUE;
        }
    }
}

