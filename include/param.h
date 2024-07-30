#ifndef PARAM_H
#define PARAM_H

#include "setting.h"

void ShowParams(PUVPERF_PARAM TestParams);

int GetDeviceParam(PUVPERF_PARAM TestParams);

void SetParamsDefaults(PUVPERF_PARAM TestParms);

int GetDeviceInfoFromList(PUVPERF_PARAM TestParams);

int CreateVerifyBuffer(PUVPERF_PARAM TestParam, WORD endpointMaxPacketSize);

#endif // PARAM_H
 