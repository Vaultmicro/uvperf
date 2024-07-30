#ifndef TRANSFER_P_H
#define TRANSFER_P_H

#include "setting.h"


#define TRANSFER_DISPLAY(TransferParam, ReadingString, WritingString)                              \
    ((TransferParam->Ep.PipeId & USB_ENDPOINT_DIRECTION_MASK) ? ReadingString : WritingString)

#define INC_ROLL(IncField, RollOverValue)                                                          \
    if ((++IncField) >= RollOverValue)                                                             \
    IncField = 0


void AppendLoopBuffer(PUVPERF_PARAM TestParams, unsigned char *buffer, unsigned int length);

int VerifyData(PUVPERF_TRANSFER_PARAM transferParam, BYTE *data, INT dataLength);


int TransferSync(PUVPERF_TRANSFER_PARAM transferParam);
BOOL WINAPI IsoTransferCb(_in unsigned int packetIndex, _ref unsigned int *offset,
                          _ref unsigned int *length, _ref unsigned int *status,
                          _in void *userState);
int TransferAsync(PUVPERF_TRANSFER_PARAM transferParam, PUVPERF_TRANSFER_HANDLE *handleRef);
void VerifyLoopData();


void FreeTransferParam(PUVPERF_TRANSFER_PARAM *transferParamRef);

PUVPERF_TRANSFER_PARAM CreateTransferParam(PUVPERF_PARAM TestParams, int endpointID);

void GetAverageBytesSec(PUVPERF_TRANSFER_PARAM transferParam, DOUBLE *bps);

void GetCurrentBytesSec(PUVPERF_TRANSFER_PARAM transferParam, DOUBLE *bps);

void ShowRunningStatus(PUVPERF_TRANSFER_PARAM readParam, PUVPERF_TRANSFER_PARAM writeParam);

DWORD TransferThread(PUVPERF_TRANSFER_PARAM transferParam);

void ShowTransfer(PUVPERF_TRANSFER_PARAM transferParam);

BOOL WaitForTestTransfer(PUVPERF_TRANSFER_PARAM transferParam, UINT msToWait);



#endif // TRANSFER_P_H
 