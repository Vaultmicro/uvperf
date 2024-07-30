#ifndef FILEIO_H
#define FILEIO_H
#include "setting.h"
#include "param.h"

void FileIOOpen(PUVPERF_PARAM TestParams);
void FileIOBuffer(PUVPERF_PARAM TestParams, PUVPERF_TRANSFER_PARAM transferParam);
void FileIOLog(PUVPERF_PARAM TestParams);
void FileIOClose(PUVPERF_PARAM TestParams);

#endif // FILEIO_H
