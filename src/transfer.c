#include "transfer.h"
#include "bench.h"
#include "log.h"
#include "utils.h"
#include "uvperf.h"

#define INC_ROLL(IncField, RollOverValue)                                                          \
    if ((++IncField) >= RollOverValue)                                                             \
    IncField = 0

int VerifyData(PUVPERF_TRANSFER_PARAM transferParam, BYTE *data, INT dataLength) {
    WORD verifyDataSize = transferParam->TestParms->verifyBufferSize;
    BYTE *verifyData = transferParam->TestParms->VerifyBuffer;
    BYTE keyC = 0;
    BOOL seedKey = TRUE;
    INT dataLeft = dataLength;
    INT dataIndex = 0;
    INT packetIndex = 0;
    INT verifyIndex = 0;

    while (dataLeft > 1) {
        verifyDataSize = dataLeft > transferParam->TestParms->verifyBufferSize
                             ? transferParam->TestParms->verifyBufferSize
                             : (WORD)dataLeft;

        if (seedKey)
            keyC = data[dataIndex + 1];
        else {
            if (data[dataIndex + 1] == 0) {
                keyC = 0;
            } else {
                keyC++;
            }
        }
        seedKey = FALSE;
        verifyData[1] = keyC;

        if (memcmp(&data[dataIndex], verifyData, verifyDataSize) != 0) {
            seedKey = TRUE;

            if (transferParam->TestParms->verifyDetails) {
                for (verifyIndex = 0; verifyIndex < verifyDataSize; verifyIndex++) {
                    if (verifyData[verifyIndex] == data[dataIndex + verifyIndex])
                        continue;

                    LOGVDAT("packet-offset=%d expected %02Xh got %02Xh\n", verifyIndex,
                            verifyData[verifyIndex], data[dataIndex + verifyIndex]);
                }
            }
        }

        packetIndex++;
        dataLeft -= verifyDataSize;
        dataIndex += verifyDataSize;
    }

    return 0;
}

int TransferSync(PUVPERF_TRANSFER_PARAM transferParam) {
    unsigned int trasnferred;
    BOOL success;

    if (transferParam->Ep.PipeId & USB_ENDPOINT_DIRECTION_MASK) {
        success = K.ReadPipe(transferParam->TestParms->InterfaceHandle, transferParam->Ep.PipeId,
                             transferParam->Buffer, transferParam->TestParms->readlenth,
                             &trasnferred, NULL);
    } else {
        AppendLoopBuffer(transferParam->TestParms, transferParam->Buffer,
                         transferParam->TestParms->writelength);
        success = K.WritePipe(transferParam->TestParms->InterfaceHandle, transferParam->Ep.PipeId,
                              transferParam->Buffer, transferParam->TestParms->writelength,
                              &trasnferred, NULL);
    }

    return success ? (int)trasnferred : -(int)GetLastError();
}

BOOL WINAPI IsoTransferCb(_in unsigned int packetIndex, _ref unsigned int *offset,
                          _ref unsigned int *length, _ref unsigned int *status,
                          _in void *userState) {
    BENCHMARK_ISOCH_RESULTS *isochResults = (BENCHMARK_ISOCH_RESULTS *)userState;

    UNREFERENCED_PARAMETER(packetIndex);
    UNREFERENCED_PARAMETER(offset);

    if (*status)
        isochResults->BadPackets++;
    else {
        if (*length) {
            isochResults->GoodPackets++;
            isochResults->Length += *length;
        }
    }
    isochResults->TotalPackets++;

    return TRUE;
}

int TransferAsync(PUVPERF_TRANSFER_PARAM transferParam, PUVPERF_TRANSFER_HANDLE *handleRef) {
    int ret = 0;
    BOOL success;
    PUVPERF_TRANSFER_HANDLE handle = NULL;
    DWORD transferErrorCode;

    *handleRef = NULL;

    while (transferParam->outstandingTransferCount < transferParam->TestParms->bufferCount) {
        *handleRef = handle =
            &transferParam->TransferHandles[transferParam->transferHandleNextIndex];

        if (!handle->Overlapped.hEvent) {
            handle->Overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
            handle->Data = transferParam->Buffer + (transferParam->transferHandleNextIndex *
                                                    transferParam->TestParms->allocBufferSize);
        } else {
            ResetEvent(handle->Overlapped.hEvent);
        }

        if (transferParam->Ep.PipeId & USB_ENDPOINT_DIRECTION_MASK) {
            handle->DataMaxLength = transferParam->TestParms->readlenth;
            if (transferParam->Ep.PipeType == UsbdPipeTypeIsochronous) {
                success = K.IsochReadPipe(handle->IsochHandle, handle->DataMaxLength,
                                          &transferParam->frameNumber, 0, &handle->Overlapped);
            } else {
                success =
                    K.ReadPipe(transferParam->TestParms->InterfaceHandle, transferParam->Ep.PipeId,
                               handle->Data, handle->DataMaxLength, NULL, &handle->Overlapped);
            }
        } else {
            AppendLoopBuffer(transferParam->TestParms, handle->Data,
                             transferParam->TestParms->writelength);
            handle->DataMaxLength = transferParam->TestParms->writelength;
            if (transferParam->Ep.PipeType == UsbdPipeTypeIsochronous) {
                success = K.IsochWritePipe(handle->IsochHandle, handle->DataMaxLength,
                                           &transferParam->frameNumber, 0, &handle->Overlapped);
            } else {
                success =
                    K.WritePipe(transferParam->TestParms->InterfaceHandle, transferParam->Ep.PipeId,
                                handle->Data, handle->DataMaxLength, NULL, &handle->Overlapped);
            }
        }

        transferErrorCode = GetLastError();

        if (!success && transferErrorCode == ERROR_IO_PENDING) {
            transferErrorCode = ERROR_SUCCESS;
            success = TRUE;
        }

        handle->ReturnCode = ret = -(int)transferErrorCode;
        if (ret < 0) {
            handle->InUse = FALSE;
            goto Done;
        }

        handle->InUse = TRUE;
        transferParam->outstandingTransferCount++;
        INC_ROLL(transferParam->transferHandleNextIndex, transferParam->TestParms->bufferCount);
    }

    if (transferParam->outstandingTransferCount == transferParam->TestParms->bufferCount) {
        UINT transferred;
        *handleRef = handle =
            &transferParam->TransferHandles[transferParam->transferHandleWaitIndex];

        if (WaitForSingleObject(handle->Overlapped.hEvent, transferParam->TestParms->timeout) !=
            WAIT_OBJECT_0) {
            if (!transferParam->TestParms->isUserAborted) {
                ret = GetLastError();
            } else
                ret = -(int)GetLastError();

            handle->ReturnCode = ret;
            goto Done;
        }

        if (!K.GetOverlappedResult(transferParam->TestParms->InterfaceHandle, &handle->Overlapped,
                                   &transferred, FALSE)) {
            if (!transferParam->TestParms->isUserAborted) {
                ret = GetLastError();
            } else
                ret = -(int)GetLastError();

            handle->ReturnCode = ret;
            goto Done;
        }
        LOG_MSG("IsochResults: TotalPackets=%u GoodPackets=%u BadPackets=%u Length=%u\n",
                handle->IsochResults.TotalPackets, handle->IsochResults.GoodPackets,
                handle->IsochResults.BadPackets, handle->IsochResults.Length);

        if (transferParam->Ep.PipeType == UsbdPipeTypeIsochronous &&
            transferParam->Ep.PipeId & 0x80) {
            memset(&handle->IsochResults, 0, sizeof(handle->IsochResults));
            IsochK_EnumPackets(handle->IsochHandle, &IsoTransferCb, 0, &handle->IsochResults);

            transferParam->IsochResults.TotalPackets += handle->IsochResults.TotalPackets;
            transferParam->IsochResults.GoodPackets += handle->IsochResults.GoodPackets;
            transferParam->IsochResults.BadPackets += handle->IsochResults.BadPackets;
            transferParam->IsochResults.Length += handle->IsochResults.Length;
            transferred = handle->IsochResults.Length;
        } else if (transferParam->Ep.PipeType == UsbdPipeTypeIsochronous) {
            transferred = handle->DataMaxLength;
            transferParam->IsochResults.TotalPackets += transferParam->numberOFIsoPackets;
            transferParam->IsochResults.GoodPackets += transferParam->numberOFIsoPackets;
        }

        handle->ReturnCode = ret = (int)transferred;

        if (ret < 0)
            goto Done;

        handle->InUse = FALSE;
        transferParam->outstandingTransferCount--;
        INC_ROLL(transferParam->transferHandleWaitIndex, transferParam->TestParms->bufferCount);
    }

Done:
    return ret;
}

void VerifyLoopData() { return; }

void ShowRunningStatus(PUVPERF_TRANSFER_PARAM readParam, PUVPERF_TRANSFER_PARAM writeParam) {
    static UVPERF_TRANSFER_PARAM gReadParamTransferParam, gWriteParamTransferParam;
    DOUBLE bpsReadOverall = 0;
    DOUBLE bpsReadLastTransfer = 0;
    DOUBLE bpsWriteOverall = 0;
    DOUBLE bpsWriteLastTransfer = 0;
    UINT zlp = 0;
    UINT totalPackets = 0;
    UINT totalIsoPackets = 0;
    UINT goodIsoPackets = 0;
    UINT badIsoPackets = 0;
    UINT errorCount = 0;

    EnterCriticalSection(&DisplayCriticalSection);

    if (readParam)
        memcpy(&gReadParamTransferParam, readParam, sizeof(UVPERF_TRANSFER_PARAM));

    if (writeParam)
        memcpy(&gWriteParamTransferParam, writeParam, sizeof(UVPERF_TRANSFER_PARAM));

    LeaveCriticalSection(&DisplayCriticalSection);

    if (readParam != NULL && (!gReadParamTransferParam.StartTick.tv_nsec ||
                              (gReadParamTransferParam.StartTick.tv_sec +
                               gReadParamTransferParam.StartTick.tv_nsec / 1000000000.0) >
                                  (gReadParamTransferParam.LastTick.tv_sec +
                                   gReadParamTransferParam.LastTick.tv_nsec / 1000000000.0))) {
        LOG_MSG("Synchronizing Read %d..\n", abs(gReadParamTransferParam.Packets));
        errorCount++;
        if (errorCount > 5) {
            LOGERR0("Too many errors, exiting..\n");
            return;
        }
    }

    if (writeParam != NULL && (!gWriteParamTransferParam.StartTick.tv_nsec ||
                               (gWriteParamTransferParam.StartTick.tv_sec +
                                gWriteParamTransferParam.StartTick.tv_nsec / 1000000000.0) >
                                   (gWriteParamTransferParam.LastTick.tv_sec +
                                    gWriteParamTransferParam.LastTick.tv_nsec / 1000000000.0))) {
        LOG_MSG("Synchronizing Write %d..\n", abs(gWriteParamTransferParam.Packets));
        errorCount++;
        if (errorCount > 5) {
            LOGERR0("Too many errors, exiting..\n");
            return;
        }

    } else {
        if (readParam) {
            GetAverageBytesSec(&gReadParamTransferParam, &bpsReadOverall);
            GetCurrentBytesSec(&gReadParamTransferParam, &bpsReadLastTransfer);
            if (gReadParamTransferParam.LastTransferred == 0)
                zlp++;
            readParam->LastStartTick.tv_nsec = 0.0;
            totalPackets += gReadParamTransferParam.Packets;
            totalIsoPackets += gReadParamTransferParam.IsochResults.TotalPackets;
            goodIsoPackets += gReadParamTransferParam.IsochResults.GoodPackets;
            badIsoPackets += gReadParamTransferParam.IsochResults.BadPackets;
        }

        if (writeParam) {
            GetAverageBytesSec(&gWriteParamTransferParam, &bpsWriteOverall);
            GetCurrentBytesSec(&gWriteParamTransferParam, &bpsWriteLastTransfer);

            if (gWriteParamTransferParam.LastTransferred == 0) {
                zlp++;
            }

            writeParam->LastStartTick.tv_nsec = 0.0;
            totalPackets += gWriteParamTransferParam.Packets;
            totalIsoPackets += gWriteParamTransferParam.IsochResults.TotalPackets;
            goodIsoPackets += gWriteParamTransferParam.IsochResults.GoodPackets;
            badIsoPackets += gWriteParamTransferParam.IsochResults.BadPackets;
        }
        if (totalIsoPackets) {
            LOG_MSG("Average %.2f Mbps\n", ((bpsReadOverall + bpsWriteOverall) * 8) / 1000 / 1000);
            LOG_MSG("Total %d Transfer\n", totalPackets);
            LOG_MSG("ISO-Packets (Total/Good/Bad) : %u/%u/%u\n", totalIsoPackets, goodIsoPackets,
                    badIsoPackets);
        } else {
            if (zlp) {
                LOG_MSG("Average %.2f Mbps\n",
                        (bpsReadOverall + bpsWriteOverall) * 8 / 1000 / 1000);
                LOG_MSG("Transfers: %u\n", totalPackets);
                LOG_MSG("Zero-length-transfer(s)\n", zlp);
            } else {
                LOG_MSG("Average %.2f Mbps\n",
                        (bpsReadOverall + bpsWriteOverall) * 8 / 1000 / 1000);
                LOG_MSG("Total %d Transfers\n", totalPackets);
                LOG_MSG("\n");
            }
        }
    }
}

DWORD TransferThread(PUVPERF_TRANSFER_PARAM transferParam) {
    int ret, i;
    PUVPERF_TRANSFER_HANDLE handle;
    unsigned char *buffer;

    transferParam->isRunning = TRUE;

    while (!transferParam->TestParms->isCancelled) {
        buffer = NULL;
        handle = NULL;

        if (transferParam->TestParms->TransferMode == TRANSFER_MODE_SYNC) {
            ret = TransferSync(transferParam);
            if (ret >= 0)
                buffer = transferParam->Buffer;
        } else if (transferParam->TestParms->TransferMode == TRANSFER_MODE_ASYNC) {
            ret = TransferAsync(transferParam, &handle);
            if ((handle) && ret >= 0)
                buffer = transferParam->Buffer;
        } else {
            LOG_ERROR("Invalid transfer mode %d\n", transferParam->TestParms->TransferMode);
            goto Done;
        }

        if (transferParam->TestParms->verify && transferParam->TestParms->VerifyList &&
            transferParam->TestParms->TestType == TestTypeLoop &&
            USB_ENDPOINT_DIRECTION_IN(transferParam->Ep.PipeId) && ret > 0) {
            VerifyLoopData(transferParam->TestParms, buffer);
        }

        if (ret < 0) {
            if (transferParam->TestParms->isUserAborted)
                break;

            if (ret == ERROR_SEM_TIMEOUT || ret == ERROR_OPERATION_ABORTED ||
                ret == ERROR_CANCELLED) {
                transferParam->TotalTimeoutCount++;
                transferParam->RunningTimeoutCount++;
                LOG_ERROR("Timeout #%d %s on EP%02Xh.. \n", transferParam->RunningTimeoutCount,
                          TRANSFER_DISPLAY(transferParam, "in", "out"), transferParam->Ep.PipeId);

                if (transferParam->RunningTimeoutCount > transferParam->TestParms->retry)
                    break;
            } else {
                transferParam->TotalErrorCount++;
                transferParam->RunningErrorCount++;
                LOG_ERROR("failed %s, %d of %d ret=%d, error message : %s\n",
                          TRANSFER_DISPLAY(transferParam, "in", "out"),
                          transferParam->RunningErrorCount, transferParam->TestParms->retry + 1,
                          ret, strerror(ret));
                K.ResetPipe(transferParam->TestParms->InterfaceHandle, transferParam->Ep.PipeId);

                if (transferParam->RunningErrorCount > transferParam->TestParms->retry)
                    break;
            }

            ret = 0;
        } else {
            transferParam->RunningTimeoutCount = 0;
            transferParam->RunningErrorCount = 0;
            if (USB_ENDPOINT_DIRECTION_IN(transferParam->Ep.PipeId)) {
                if (transferParam->TestParms->verify &&
                    transferParam->TestParms->TestType != TestTypeLoop) {
                    VerifyData(transferParam, buffer, ret);
                }
            } else {
                if (transferParam->TestParms->verify &&
                    transferParam->TestParms->TestType != TestTypeLoop) {
                }
            }
        }

        EnterCriticalSection(&DisplayCriticalSection);

        if (!transferParam->StartTick.tv_nsec && transferParam->Packets >= 0) {
            clock_gettime(CLOCK_MONOTONIC, &transferParam->StartTick);
            transferParam->LastStartTick = transferParam->StartTick;
            transferParam->LastTick = transferParam->StartTick;

            transferParam->LastTransferred = 0;
            transferParam->TotalTransferred = 0;
            transferParam->Packets = 0;
        } else {
            if (!transferParam->LastStartTick.tv_nsec) {
                transferParam->LastStartTick = transferParam->LastTick;
                transferParam->LastTransferred = 0;
            }
            clock_gettime(CLOCK_MONOTONIC, &transferParam->LastTick);

            transferParam->LastTransferred += ret;
            transferParam->TotalTransferred += ret;
            transferParam->Packets++;
        }

        LeaveCriticalSection(&DisplayCriticalSection);
    }

Done:

    for (i = 0; i < transferParam->TestParms->bufferCount; i++) {
        if (transferParam->TransferHandles[i].Overlapped.hEvent) {
            if (transferParam->TransferHandles[i].InUse) {
                if (!K.AbortPipe(transferParam->TestParms->InterfaceHandle,
                                 transferParam->Ep.PipeId) &&
                    !transferParam->TestParms->isUserAborted) {
                    ret = WinError(0);
                    LOG_ERROR("failed cancelling transfer ret = %d, error message : %s\n", ret,
                              strerror(ret));
                } else {
                    CloseHandle(transferParam->TransferHandles[i].Overlapped.hEvent);
                    transferParam->TransferHandles[i].Overlapped.hEvent = NULL;
                    transferParam->TransferHandles[i].InUse = FALSE;
                }
            }
            Sleep(0);
        }
    }

    for (i = 0; i < transferParam->TestParms->bufferCount; i++) {
        if (transferParam->TransferHandles[i].Overlapped.hEvent) {
            if (transferParam->TransferHandles[i].InUse) {
                WaitForSingleObject(transferParam->TransferHandles[i].Overlapped.hEvent,
                                    transferParam->TestParms->timeout);
            } else {
                WaitForSingleObject(transferParam->TransferHandles[i].Overlapped.hEvent, 0);
            }
            CloseHandle(transferParam->TransferHandles[i].Overlapped.hEvent);
            transferParam->TransferHandles[i].Overlapped.hEvent = NULL;
        }
        transferParam->TransferHandles[i].InUse = FALSE;
    }

    transferParam->isRunning = FALSE;
    return 0;
}

int CreateVerifyBuffer(PUVPERF_PARAM TestParam, WORD endpointMaxPacketSize) {
    int i;
    BYTE indexC = 0;
    TestParam->VerifyBuffer = malloc(endpointMaxPacketSize);
    if (!TestParam->VerifyBuffer) {
        LOG_ERROR("memory allocation failure at line %d!\n", __LINE__);
        return -1;
    }

    TestParam->verifyBufferSize = endpointMaxPacketSize;

    for (i = 0; i < endpointMaxPacketSize; i++) {
        TestParam->VerifyBuffer[i] = indexC++;
        if (indexC == 0)
            indexC = 1;
    }

    return 0;
}

void FreeTransferParam(PUVPERF_TRANSFER_PARAM *transferParamRef) {
    PUVPERF_TRANSFER_PARAM pTransferParam;
    int i;
    if ((!transferParamRef) || !*transferParamRef)
        return;
    pTransferParam = *transferParamRef;

    if (pTransferParam->TestParms) {
        for (i = 0; i < pTransferParam->TestParms->bufferCount; i++) {
            if (pTransferParam->TransferHandles[i].IsochHandle) {
                IsochK_Free(pTransferParam->TransferHandles[i].IsochHandle);
            }
        }
    }
    if (pTransferParam->ThreadHandle) {
        CloseHandle(pTransferParam->ThreadHandle);
        pTransferParam->ThreadHandle = NULL;
    }

    free(pTransferParam);

    *transferParamRef = NULL;
}

PUVPERF_TRANSFER_PARAM CreateTransferParam(PUVPERF_PARAM TestParam, int endpointID) {
    PUVPERF_TRANSFER_PARAM transferParam = NULL;
    int pipeIndex, bufferIndex;
    int allocSize;

    PWINUSB_PIPE_INFORMATION_EX pipeInfo = NULL;

    for (pipeIndex = 0; pipeIndex <= TestParam->InterfaceDescriptor.bNumEndpoints; pipeIndex++) {
        if (!(endpointID & USB_ENDPOINT_ADDRESS_MASK)) {
            if ((TestParam->PipeInformation[pipeIndex].PipeId & USB_ENDPOINT_DIRECTION_MASK) ==
                endpointID) {
                pipeInfo = &TestParam->PipeInformation[pipeIndex];
                break;
            }
        } else {
            if ((int)TestParam->PipeInformation[pipeIndex].PipeId == endpointID) {
                pipeInfo = &TestParam->PipeInformation[pipeIndex];
                break;
            }
        }
    }

    if (!pipeInfo) {
        LOG_ERROR("failed locating EP0x%02X\n", endpointID);
        goto Done;
    }

    if (!pipeInfo->MaximumPacketSize) {
        LOG_WARNING("MaximumPacketSize=0 for EP%02Xh. check alternate settings.\n",
                    pipeInfo->PipeId);
    }

    TestParam->bufferlength = max(TestParam->bufferlength, TestParam->readlenth);
    TestParam->bufferlength = max(TestParam->bufferlength, TestParam->writelength);

    allocSize = sizeof(UVPERF_TRANSFER_PARAM) + (TestParam->bufferlength * TestParam->bufferCount);
    transferParam = (PUVPERF_TRANSFER_PARAM)malloc(allocSize);

    if (transferParam) {
        UINT numIsoPackets;
        memset(transferParam, 0, allocSize);
        transferParam->TestParms = TestParam;

        memcpy(&transferParam->Ep, pipeInfo, sizeof(transferParam->Ep));
        transferParam->HasEpCompanionDescriptor = K.GetSuperSpeedPipeCompanionDescriptor(
            TestParam->InterfaceHandle, TestParam->InterfaceDescriptor.bAlternateSetting,
            (UCHAR)pipeIndex, &transferParam->EpCompanionDescriptor);

        if (ENDPOINT_TYPE(transferParam) == USB_ENDPOINT_TYPE_ISOCHRONOUS) {
            transferParam->TestParms->TransferMode = TRANSFER_MODE_ASYNC;

            if (!transferParam->Ep.MaximumBytesPerInterval) {
                LOG_ERROR(
                    "Unable to determine 'MaximumBytesPerInterval' for isochronous pipe %02X\n",
                    transferParam->Ep.PipeId);
                LOGERR0("- Device firmware may be incorrectly configured.");
                FreeTransferParam(&transferParam);
                goto Done;
            }
            numIsoPackets =
                transferParam->TestParms->bufferlength / transferParam->Ep.MaximumBytesPerInterval;
            transferParam->numberOFIsoPackets = numIsoPackets;
            if (numIsoPackets == 0 || ((numIsoPackets % 8)) ||
                transferParam->TestParms->bufferlength %
                    transferParam->Ep.MaximumBytesPerInterval) {
                const UINT minBufferSize = transferParam->Ep.MaximumBytesPerInterval * 8;
                LOG_ERROR("Buffer size is not correct for isochronous pipe 0x%02X\n",
                          transferParam->Ep.PipeId);
                LOG_ERROR("- Buffer size must be an interval of %u\n", minBufferSize);
                FreeTransferParam(&transferParam);
                goto Done;
            }

            for (bufferIndex = 0; bufferIndex < transferParam->TestParms->bufferCount;
                 bufferIndex++) {
                transferParam->TransferHandles[bufferIndex].Overlapped.hEvent =
                    CreateEvent(NULL, TRUE, FALSE, NULL);

                transferParam->TransferHandles[bufferIndex].Data =
                    transferParam->Buffer + (bufferIndex * transferParam->TestParms->bufferlength);

                if (!IsochK_Init(&transferParam->TransferHandles[bufferIndex].IsochHandle,
                                 TestParam->InterfaceHandle, transferParam->Ep.PipeId,
                                 numIsoPackets, transferParam->TransferHandles[bufferIndex].Data,
                                 transferParam->TestParms->bufferlength)) {
                    DWORD ec = GetLastError();

                    LOG_ERROR("IsochK_Init failed for isochronous pipe %02X\n",
                              transferParam->Ep.PipeId);
                    LOG_ERROR("- ErrorCode = %u (%s)\n", ec, strerror(ec));
                    FreeTransferParam(&transferParam);
                    goto Done;
                }

                if (!IsochK_SetPacketOffsets(
                        transferParam->TransferHandles[bufferIndex].IsochHandle,
                        transferParam->Ep.MaximumBytesPerInterval)) {
                    DWORD ec = GetLastError();

                    LOG_ERROR("IsochK_SetPacketOffsets failed for isochronous pipe %02X\n",
                              transferParam->Ep.PipeId);
                    LOG_ERROR("- ErrorCode = %u (%s)\n", ec, strerror(ec));
                    FreeTransferParam(&transferParam);
                    goto Done;
                }
            }
        }

        transferParam->ThreadHandle =
            CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)TransferThread, transferParam,
                         CREATE_SUSPENDED, &transferParam->ThreadId);

        if (!transferParam->ThreadHandle) {
            LOGERR0("failed creating thread!\n");
            FreeTransferParam(&transferParam);
            goto Done;
        }

        if (transferParam->TestParms->TestType == TestTypeLoop &&
            USB_ENDPOINT_DIRECTION_OUT(pipeInfo->PipeId)) {
            BYTE indexC = 0;
            INT bufferIndex = 0;
            WORD dataIndex;
            INT packetIndex;
            INT packetCount = ((transferParam->TestParms->bufferCount * TestParam->readlenth) /
                               pipeInfo->MaximumPacketSize);
            for (packetIndex = 0; packetIndex < packetCount; packetIndex++) {
                indexC = 2;
                for (dataIndex = 0; dataIndex < pipeInfo->MaximumPacketSize; dataIndex++) {
                    if (dataIndex == 0)
                        transferParam->Buffer[bufferIndex] = 0;
                    else if (dataIndex == 1)
                        transferParam->Buffer[bufferIndex] = packetIndex & 0xFF;
                    else
                        transferParam->Buffer[bufferIndex] = indexC++;

                    if (indexC == 0)
                        indexC = 1;

                    bufferIndex++;
                }
            }
        }
    }

Done:
    if (!transferParam)
        LOGERR0("failed creating transfer param!\n");

    return transferParam;
}

void GetAverageBytesSec(PUVPERF_TRANSFER_PARAM transferParam, DOUBLE *byteps) {
    DWORD elapsedSeconds = 0.0;
    if (!transferParam)
        return;

    if (transferParam->StartTick.tv_nsec &&
        (transferParam->StartTick.tv_sec + transferParam->StartTick.tv_nsec / 1000000000.0) <
            (transferParam->LastTick.tv_sec + transferParam->LastTick.tv_nsec / 1000000000.0)) {

        elapsedSeconds =
            (transferParam->LastTick.tv_sec - transferParam->StartTick.tv_sec) +
            (transferParam->LastTick.tv_nsec - transferParam->StartTick.tv_nsec) / 1000000000.0;

        *byteps = (DOUBLE)transferParam->TotalTransferred / elapsedSeconds;
        if (transferParam->TotalTransferred == 0) {
            *byteps = 0;
        }
        if (elapsedSeconds == 0) {
            *byteps = 0;
        }

    } else {
        *byteps = 0;
    }
}
void GetCurrentBytesSec(PUVPERF_TRANSFER_PARAM transferParam, DOUBLE *byteps) {
    DWORD elapsedSeconds;
    if (!transferParam)
        return;

    if (transferParam->LastStartTick.tv_nsec &&
        (transferParam->LastStartTick.tv_sec +
         transferParam->LastStartTick.tv_nsec / 1000000000.0) <
            (transferParam->LastTick.tv_sec + transferParam->LastTick.tv_nsec / 1000000000.0)) {

        elapsedSeconds =
            (transferParam->LastTick.tv_sec - transferParam->LastStartTick.tv_sec) +
            (transferParam->LastTick.tv_nsec - transferParam->LastStartTick.tv_nsec) / 1000000000.0;

        *byteps = (DOUBLE)transferParam->LastTransferred / elapsedSeconds;
    } else {
        *byteps = 0;
    }
}

void ShowTransfer(PUVPERF_TRANSFER_PARAM transferParam) {
    DOUBLE BytepsAverage;
    DOUBLE BytepsCurrent;
    DOUBLE elapsedSeconds;

    if (!transferParam)
        return;

    if (transferParam->HasEpCompanionDescriptor) {
        if (transferParam->EpCompanionDescriptor.wBytesPerInterval) {
            if (transferParam->Ep.PipeType == UsbdPipeTypeIsochronous) {
                LOG_MSG(
                    "%s %s from Ep0x%02X Maximum Bytes Per Interval:%lu Max Bursts:%u Multi:%u\n",
                    EndpointTypeDisplayString[ENDPOINT_TYPE(transferParam)],
                    TRANSFER_DISPLAY(transferParam, "in", "out"), transferParam->Ep.PipeId,
                    transferParam->Ep.MaximumBytesPerInterval,
                    transferParam->EpCompanionDescriptor.bMaxBurst + 1,
                    transferParam->EpCompanionDescriptor.bmAttributes.Isochronous.Mult + 1);
            } else if (transferParam->Ep.PipeType == UsbdPipeTypeBulk) {
                LOG_MSG("%s %s from Ep0x%02X Maximum Bytes Per Interval:%lu Max Bursts:%u Max "
                        "Streams:%u\n",
                        EndpointTypeDisplayString[ENDPOINT_TYPE(transferParam)],
                        TRANSFER_DISPLAY(transferParam, "in", "out"), transferParam->Ep.PipeId,
                        transferParam->Ep.MaximumBytesPerInterval,
                        transferParam->EpCompanionDescriptor.bMaxBurst + 1,
                        transferParam->EpCompanionDescriptor.bmAttributes.Bulk.MaxStreams + 1);
            } else {
                LOG_MSG("%s %s from Ep0x%02X Maximum Bytes Per Interval:%lu\n",
                        EndpointTypeDisplayString[ENDPOINT_TYPE(transferParam)],
                        TRANSFER_DISPLAY(transferParam, "in", "out"), transferParam->Ep.PipeId,
                        transferParam->Ep.MaximumBytesPerInterval);
            }
        } else {
            LOG_MSG("%s %s Ep0x%02X Maximum Packet Size:%d\n",
                    EndpointTypeDisplayString[ENDPOINT_TYPE(transferParam)],
                    TRANSFER_DISPLAY(transferParam, "in", "out"), transferParam->Ep.PipeId,
                    transferParam->Ep.MaximumPacketSize);
        }
    } else {
        LOG_MSG("%s %s Ep0x%02X Maximum Packet Size: %d\n",
                EndpointTypeDisplayString[ENDPOINT_TYPE(transferParam)],
                TRANSFER_DISPLAY(transferParam, "in", "out"), transferParam->Ep.PipeId,
                transferParam->Ep.MaximumPacketSize);
    }

    if (transferParam->StartTick.tv_nsec) {
        GetAverageBytesSec(transferParam, &BytepsAverage);
        GetCurrentBytesSec(transferParam, &BytepsCurrent);
        LOG_MSG("\tTotal %I64d Bytes\n", transferParam->TotalTransferred);
        LOG_MSG("\tTotal %d Transfers\n", transferParam->Packets);

        if (transferParam->shortTrasnferred) {
            LOG_MSG("\tShort %d Transfers\n", transferParam->shortTrasnferred);
        }

        if (transferParam->TotalTimeoutCount) {
            LOG_MSG("\tTimeout %d Errors\n", transferParam->TotalTimeoutCount);
        }

        if (transferParam->TotalErrorCount) {
            LOG_MSG("\tOther %d Errors\n", transferParam->TotalErrorCount);
        }

        LOG_MSG("\tAverage %.2f Mbps/sec\n", (BytepsAverage * 8) / 1000 / 1000);

        if (transferParam->StartTick.tv_nsec &&
            (transferParam->LastStartTick.tv_sec +
             transferParam->LastStartTick.tv_nsec / 1000000000.0) <
                (transferParam->LastTick.tv_sec + transferParam->LastTick.tv_nsec / 1000000000.0)) {
            elapsedSeconds =
                (transferParam->LastTick.tv_sec - transferParam->StartTick.tv_sec) +
                (transferParam->LastTick.tv_nsec - transferParam->StartTick.tv_nsec) / 1000000000.0;
            LOG_MSG("\tElapsed Time %.2f seconds\n", elapsedSeconds);
        }

        LOG_MSG("\n");
    }
}

BOOL WaitForTestTransfer(PUVPERF_TRANSFER_PARAM transferParam, UINT msToWait) {
    DWORD exitCode;

    while (transferParam) {
        if (!transferParam->isRunning) {
            if (GetExitCodeThread(transferParam->ThreadHandle, &exitCode)) {
                LOG_MSG("stopped Ep0x%02X thread \tExitCode=%d\n", transferParam->Ep.PipeId,
                        exitCode);
                break;
            }

            LOG_ERROR("failed getting Ep0x%02X thread exit code!\n", transferParam->Ep.PipeId);
            break;
        }

        LOG_MSG("waiting for Ep0x%02X thread..\n", transferParam->Ep.PipeId);
        WaitForSingleObject(transferParam->ThreadHandle, 100);
        if (msToWait != INFINITE) {
            if ((msToWait - 100) == 0 || (msToWait - 100) > msToWait)
                return FALSE;
        }
    }

    return TRUE;
}