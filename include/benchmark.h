#ifndef BENCHMARK_H
#define BENCHMARK_H

#include "setting.h"

BOOL Bench_Open(__in PUVPERF_PARAM TestParams);


BOOL Bench_Configure(__in KUSB_HANDLE handle, __in UVPERF_DEVICE_COMMAND command, __in UCHAR intf,
                     __inout PUVPERF_DEVICE_TRANSFER_TYPE testType);



#endif // BENCHMARK_H