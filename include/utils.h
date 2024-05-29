#ifndef UTILS_H
#define UTILS_H

#include "uvperf.h"

void AppendLoopBuffer(PUVPERF_PARAM TestParms, unsigned char *buffer, unsigned int length);

LONG WinError(__in_opt DWORD errorCode);

int ParseArgs(PUVPERF_PARAM TestParms, int argc, char **argv);

void ShowUsage();

void SetParamsDefaults(PUVPERF_PARAM TestParms);

int GetDeviceInfoFromList(PUVPERF_PARAM TestParms);

int GetEndpointFromList(PUVPERF_PARAM TestParms);

int GetDeviceParam(PUVPERF_PARAM TestParms);

void ShowParms(PUVPERF_PARAM TestParms);

void FileIOOpen(PUVPERF_PARAM TestParms);

void FileIOLog(PUVPERF_PARAM TestParms);

void FileIOClose(PUVPERF_PARAM TestParms);

#endif // UTILS_H
