#ifndef TRANSFER_H
#define TRANSFER_H

#include "uvperf.h"

int VerifyData(PUVPERF_TRANSFER_PARAM transferParam, BYTE *data, INT dataLength);

int TransferSync(PUVPERF_TRANSFER_PARAM transferParam);

BOOL WINAPI IsoTransferCb(_in unsigned int packetIndex, _ref unsigned int *offset,
                          _ref unsigned int *length, _ref unsigned int *status,
                          _in void *userState);

int TransferAsync(PUVPERF_TRANSFER_PARAM transferParam, PUVPERF_TRANSFER_HANDLE *handleRef);

void VerifyLoopData();

void ShowRunningStatus(PUVPERF_TRANSFER_PARAM readParam, PUVPERF_TRANSFER_PARAM writeParam);

DWORD TransferThread(PUVPERF_TRANSFER_PARAM transferParam);

int CreateVerifyBuffer(PUVPERF_PARAM TestParam, WORD endpointMaxPacketSize);

void FreeTransferParam(PUVPERF_TRANSFER_PARAM *transferParamRef);

PUVPERF_TRANSFER_PARAM CreateTransferParam(PUVPERF_PARAM TestParam, int endpointID);

void GetAverageBytesSec(PUVPERF_TRANSFER_PARAM transferParam, DOUBLE *byteps);

void GetCurrentBytesSec(PUVPERF_TRANSFER_PARAM transferParam, DOUBLE *byteps);

void ShowTransfer(PUVPERF_TRANSFER_PARAM transferParam);

BOOL WaitForTestTransfer(PUVPERF_TRANSFER_PARAM transferParam, UINT msToWait);

#endif // TRANSFER_H
