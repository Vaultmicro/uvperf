#ifndef BENCH_H
#define BENCH_H

#include "uvperf.h"

BOOL Bench_Open(__in PUVPERF_PARAM TestParms);

BOOL Bench_Configure(__in KUSB_HANDLE handle, __in UVPERF_DEVICE_COMMAND command, __in UCHAR intf,
                     __inout PUVPERF_DEVICE_TRANSFER_TYPE testType);

#endif // BENCH_H
