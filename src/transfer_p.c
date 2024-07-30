#include <windows.h>
#include "lusbk_linked_list.h"

#include "log.h"
#include "k.h"
#include "transfer_p.h" 


void AppendLoopBuffer(PUVPERF_PARAM TestParams, unsigned char *buffer, unsigned int length) {

    if (TestParams->verify && TestParams->TestType == TestTypeLoop) {
        UVPERF_BUFFER *newVerifyBuf = malloc(sizeof(UVPERF_BUFFER) + length);

        memset(newVerifyBuf, 1, sizeof(UVPERF_BUFFER));

        newVerifyBuf->Data = (unsigned char *)newVerifyBuf + sizeof(UVPERF_BUFFER);
        newVerifyBuf->dataLenth = length;
        memcpy(newVerifyBuf->Data, buffer, length);

        VerifyListLock(TestParams);
        DL_APPEND(TestParams->VerifyList, newVerifyBuf);
        VerifyListUnlock(TestParams);
    }
}



int VerifyData(PUVPERF_TRANSFER_PARAM transferParam, BYTE *data, INT dataLength) {

    WORD verifyDataSize = transferParam->TestParams->verifyBufferSize;
    BYTE *verifyData = transferParam->TestParams->VerifyBuffer;
    BYTE keyC = 0;
    BOOL seedKey = TRUE;
    INT dataLeft = dataLength;
    INT dataIndex = 0;
    INT packetIndex = 0;
    INT verifyIndex = 0;

    while (dataLeft > 1) {
        verifyDataSize = dataLeft > transferParam->TestParams->verifyBufferSize
                             ? transferParam->TestParams->verifyBufferSize
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
        // Index 0 is always 0.
        // The key is always at index 1
        verifyData[1] = keyC;

        if (memcmp(&data[dataIndex], verifyData, verifyDataSize) != 0) {
            // Packet verification failed.

            // Reset the key byte on the next packet.
            seedKey = TRUE;
            //TODO
            // LOGVDAT("Packet=#%d Data=#%d\n", packetIndex, dataIndex);

            if (transferParam->TestParams->verifyDetails) {
                for (verifyIndex = 0; verifyIndex < verifyDataSize; verifyIndex++) {
                    if (verifyData[verifyIndex] == data[dataIndex + verifyIndex])
                        continue;

                    LOGVDAT("packet-offset=%d expected %02Xh got %02Xh\n", verifyIndex,
                            verifyData[verifyIndex], data[dataIndex + verifyIndex]);
                }
            }
        }

        // Move to the next packet.
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
        success = K.ReadPipe(transferParam->TestParams->InterfaceHandle,
                             transferParam->Ep.PipeId,
                             transferParam->Buffer,
                             transferParam->TestParams->readlenth,
                             &trasnferred,
                             NULL);
    } else {
        AppendLoopBuffer(transferParam->TestParams,
                         transferParam->Buffer,
                         transferParam->TestParams->writelength);
        success = K.WritePipe(transferParam->TestParams->InterfaceHandle,
                             transferParam->Ep.PipeId,
                              transferParam->Buffer,
                              transferParam->TestParams->writelength,
                              &trasnferred,
                              NULL);
    }

    return success ? (int)trasnferred : -labs(GetLastError());
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

    // Submit transfers until the maximum number of outstanding transfer(s) is reached.
    while (transferParam->outstandingTransferCount < transferParam->TestParams->bufferCount) {
        // Get the next available benchmark transfer handle.
        *handleRef = handle =
            &transferParam->TransferHandles[transferParam->transferHandleNextIndex];

        // If a libusb-win32 transfer context hasn't been setup for this benchmark transfer
        // handle, do it now.
        //
        if (!handle->Overlapped.hEvent) {
            handle->Overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
            // Data buffer(s) are located at the end of the transfer param.
            handle->Data = transferParam->Buffer + (transferParam->transferHandleNextIndex *
                                                    transferParam->TestParams->allocBufferSize);
        } else {
            // re-initialize and re-use the overlapped
            ResetEvent(handle->Overlapped.hEvent);
        }

        if (transferParam->Ep.PipeId & USB_ENDPOINT_DIRECTION_MASK) {
            handle->DataMaxLength = transferParam->TestParams->readlenth;
            if (transferParam->Ep.PipeType == UsbdPipeTypeIsochronous) {
                success = K.IsochReadPipe(handle->IsochHandle, handle->DataMaxLength,
                                          &transferParam->frameNumber, 0, &handle->Overlapped);
            } else {
                success =
                    K.ReadPipe(transferParam->TestParams->InterfaceHandle, transferParam->Ep.PipeId,
                               handle->Data, handle->DataMaxLength, NULL, &handle->Overlapped);
            }
        }

        // Isochronous write pipe -> doesn't need right now
        else {
            AppendLoopBuffer(transferParam->TestParams, handle->Data,
                             transferParam->TestParams->writelength);
            handle->DataMaxLength = transferParam->TestParams->writelength;
            if (transferParam->Ep.PipeType == UsbdPipeTypeIsochronous) {
                success = K.IsochWritePipe(handle->IsochHandle, handle->DataMaxLength,
                                           &transferParam->frameNumber, 0, &handle->Overlapped);
            } else {
                success =
                    K.WritePipe(transferParam->TestParams->InterfaceHandle, transferParam->Ep.PipeId,
                                handle->Data, handle->DataMaxLength, NULL, &handle->Overlapped);
            }
        }

        transferErrorCode = GetLastError();

        if (!success && transferErrorCode == ERROR_IO_PENDING) {
            transferErrorCode = ERROR_SUCCESS;
            success = TRUE;
        }

        // Submit this transfer now.
        handle->ReturnCode = ret = -labs(transferErrorCode);
        if (ret < 0) {
            handle->InUse = FALSE;
            goto Final;
        }

        // Mark this handle has InUse.
        handle->InUse = TRUE;

        // When transfers ir successfully submitted, OutstandingTransferCount goes up; when
        // they are completed it goes down.
        //
        transferParam->outstandingTransferCount++;

        // Move TransferHandleNextIndex to the next available transfer.
        INC_ROLL(transferParam->transferHandleNextIndex, transferParam->TestParams->bufferCount);
    }

    // If the number of outstanding transfers has reached the limit, wait for the
    // oldest outstanding transfer to complete.
    //
    if (transferParam->outstandingTransferCount == transferParam->TestParams->bufferCount) {
        UINT transferred;
        // TransferHandleWaitIndex is the index of the oldest outstanding transfer.
        *handleRef = handle =
            &transferParam->TransferHandles[transferParam->transferHandleWaitIndex];

        // Only wait, cancelling & freeing is handled by the caller.
        if (WaitForSingleObject(handle->Overlapped.hEvent, transferParam->TestParams->timeout) !=
            WAIT_OBJECT_0) {
            if (!transferParam->TestParams->isUserAborted) {
                ret = WinError(0);
            } else
                ret = -labs(GetLastError());

            handle->ReturnCode = ret;
            goto Final;
        }

        if (!K.GetOverlappedResult(transferParam->TestParams->InterfaceHandle, &handle->Overlapped,
                                   &transferred, FALSE)) {
            if (!transferParam->TestParams->isUserAborted) {
                ret = WinError(0);
            } else
                ret = -labs(GetLastError());

            handle->ReturnCode = ret;
            goto Final;
        }

        if (transferParam->Ep.PipeType == UsbdPipeTypeIsochronous &&
            transferParam->Ep.PipeId & 0x80) {
            // iso read pipe
            memset(&handle->IsochResults, 0, sizeof(handle->IsochResults));
            IsochK_EnumPackets(handle->IsochHandle, &IsoTransferCb, 0, &handle->IsochResults);
            transferParam->IsochResults.TotalPackets += handle->IsochResults.TotalPackets;
            transferParam->IsochResults.GoodPackets += handle->IsochResults.GoodPackets;
            transferParam->IsochResults.BadPackets += handle->IsochResults.BadPackets;
            transferParam->IsochResults.Length += handle->IsochResults.Length;
            transferred = handle->IsochResults.Length;
        }

        // Isochronous write pipe -> doesn't need right now
        else if (transferParam->Ep.PipeType == UsbdPipeTypeIsochronous) {
            // iso write pipe
            transferred = handle->DataMaxLength;

            transferParam->IsochResults.TotalPackets += transferParam->numberOFIsoPackets;
            transferParam->IsochResults.GoodPackets += transferParam->numberOFIsoPackets;
        }

        handle->ReturnCode = ret = (DWORD)transferred;

        if (ret < 0)
            goto Final;

        // Mark this handle has no longer InUse.
        handle->InUse = FALSE;

        // When transfers ir successfully submitted, OutstandingTransferCount goes up; when
        // they are completed it goes down.
        //
        transferParam->outstandingTransferCount--;

        // Move TransferHandleWaitIndex to the oldest outstanding transfer.
        INC_ROLL(transferParam->transferHandleWaitIndex, transferParam->TestParams->bufferCount);
    }

Final:
    return ret;
}

// TODO : later for Loop data
void VerifyLoopData() { 
    return; 
}



void FreeTransferParam(PUVPERF_TRANSFER_PARAM *transferParamRef) {
    PUVPERF_TRANSFER_PARAM pTransferParam;
    int i;
    if ((!transferParamRef) || !*transferParamRef)
        return;
    pTransferParam = *transferParamRef;

    if (pTransferParam->TestParams) {
        for (i = 0; i < pTransferParam->TestParams->bufferCount; i++) {
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

    /// Get Pipe Information
    for (pipeIndex = 0; pipeIndex < TestParam->InterfaceDescriptor.bNumEndpoints; pipeIndex++) {
        if (!(endpointID & USB_ENDPOINT_ADDRESS_MASK)) {
            // Use first endpoint that matches the direction
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
        goto Final;
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
        transferParam->TestParams = TestParam;

        memcpy(&transferParam->Ep, pipeInfo, sizeof(transferParam->Ep));
        transferParam->HasEpCompanionDescriptor = K.GetSuperSpeedPipeCompanionDescriptor(
            TestParam->InterfaceHandle, TestParam->InterfaceDescriptor.bAlternateSetting,
            (UCHAR)pipeIndex, &transferParam->EpCompanionDescriptor);

        if (ENDPOINT_TYPE(transferParam) == USB_ENDPOINT_TYPE_ISOCHRONOUS) {
            transferParam->TestParams->TransferMode = TRANSFER_MODE_ASYNC;

            if (!transferParam->Ep.MaximumBytesPerInterval) {
                LOG_ERROR(
                    "Unable to determine 'MaximumBytesPerInterval' for isochronous pipe %02X\n",
                    transferParam->Ep.PipeId);
                LOGERR0("- Device firmware may be incorrectly configured.");
                FreeTransferParam(&transferParam);
                goto Final;
            }
            numIsoPackets =
                transferParam->TestParams->bufferlength / transferParam->Ep.MaximumBytesPerInterval;
            transferParam->numberOFIsoPackets = numIsoPackets;
            if (numIsoPackets == 0 || ((numIsoPackets % 8)) ||
                transferParam->TestParams->bufferlength %
                    transferParam->Ep.MaximumBytesPerInterval) {
                const UINT minBufferSize = transferParam->Ep.MaximumBytesPerInterval * 8;
                LOG_ERROR("Buffer size is not correct for isochronous pipe 0x%02X\n",
                          transferParam->Ep.PipeId);
                LOG_ERROR("- Buffer size must be an interval of %u\n", minBufferSize);
                FreeTransferParam(&transferParam);
                goto Final;
            }

            for (bufferIndex = 0; bufferIndex < transferParam->TestParams->bufferCount;
                 bufferIndex++) {
                transferParam->TransferHandles[bufferIndex].Overlapped.hEvent =
                    CreateEvent(NULL, TRUE, FALSE, NULL);

                // Data buffer(s) are located at the end of the transfer param.
                transferParam->TransferHandles[bufferIndex].Data =
                    transferParam->Buffer + (bufferIndex * transferParam->TestParams->bufferlength);

                if (!IsochK_Init(&transferParam->TransferHandles[bufferIndex].IsochHandle,
                                 TestParam->InterfaceHandle, transferParam->Ep.PipeId,
                                 numIsoPackets, transferParam->TransferHandles[bufferIndex].Data,
                                 transferParam->TestParams->bufferlength)) {
                    DWORD ec = GetLastError();

                    LOG_ERROR("IsochK_Init failed for isochronous pipe %02X\n",
                              transferParam->Ep.PipeId);
                    LOG_ERROR("- ErrorCode = %u (%s)\n", ec, strerror(ec));
                    FreeTransferParam(&transferParam);
                    goto Final;
                }

                if (!IsochK_SetPacketOffsets(
                        transferParam->TransferHandles[bufferIndex].IsochHandle,
                        transferParam->Ep.MaximumBytesPerInterval)) {
                    DWORD ec = GetLastError();

                    LOG_ERROR("IsochK_SetPacketOffsets failed for isochronous pipe %02X\n",
                              transferParam->Ep.PipeId);
                    LOG_ERROR("- ErrorCode = %u (%s)\n", ec, strerror(ec));
                    FreeTransferParam(&transferParam);
                    goto Final;
                }
            }
        }

        transferParam->ThreadHandle =
            CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)TransferThread, transferParam,
                         CREATE_SUSPENDED, &transferParam->ThreadId);

        if (!transferParam->ThreadHandle) {
            LOGERR0("failed creating thread!\n");
            FreeTransferParam(&transferParam);
            goto Final;
        }

        // If verify mode is on, this is a loop test, and this is a write endpoint, fill
        // the buffers with the same test data sent by a benchmark device when running
        // a read only test.
        if (transferParam->TestParams->TestType == TestTypeLoop &&
            USB_ENDPOINT_DIRECTION_OUT(pipeInfo->PipeId)) {
            // Data Format:
            // [0][KeyByte] 2 3 4 5 ..to.. wMaxPacketSize (if data byte rolls it is incremented to
            // 1) Increment KeyByte and repeat
            //
            BYTE indexC = 0;
            INT bufferIndex = 0;
            WORD dataIndex;
            INT packetIndex;
            INT packetCount = ((transferParam->TestParams->bufferCount * TestParam->readlenth) /
                               pipeInfo->MaximumPacketSize);
            for (packetIndex = 0; packetIndex < packetCount; packetIndex++) {
                indexC = 2;
                for (dataIndex = 0; dataIndex < pipeInfo->MaximumPacketSize; dataIndex++) {
                    if (dataIndex == 0) // Start
                        transferParam->Buffer[bufferIndex] = 0;
                    else if (dataIndex == 1) // Key
                        transferParam->Buffer[bufferIndex] = packetIndex & 0xFF;
                    else // Data
                        transferParam->Buffer[bufferIndex] = indexC++;

                    // if wMaxPacketSize is > 255, indexC resets to 1.
                    if (indexC == 0)
                        indexC = 1;

                    bufferIndex++;
                }
            }
        }
    }

Final:
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
        if(transferParam->TotalTransferred == 0)
            *byteps = 0;
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

    // LOCK the display critical section
    EnterCriticalSection(&DisplayCriticalSection);

    if (readParam)
        memcpy(&gReadParamTransferParam, readParam, sizeof(UVPERF_TRANSFER_PARAM));

    if (writeParam)
        memcpy(&gWriteParamTransferParam, writeParam, sizeof(UVPERF_TRANSFER_PARAM));

    // UNLOCK the display critical section
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

    while (!transferParam->TestParams->isCancelled) {
        buffer = NULL;
        handle = NULL;

        if (transferParam->TestParams->TransferMode == TRANSFER_MODE_SYNC) {
            ret = TransferSync(transferParam);
            if (ret >= 0)
                buffer = transferParam->Buffer;
        } else if (transferParam->TestParams->TransferMode == TRANSFER_MODE_ASYNC) {
            ret = TransferAsync(transferParam, &handle);
            if ((handle) && ret >= 0)
                buffer = transferParam->Buffer;
        } else {
            LOG_ERROR("Invalid transfer mode %d\n", transferParam->TestParams->TransferMode);
            goto Final;
        }

        if (transferParam->TestParams->verify && transferParam->TestParams->VerifyList &&
            transferParam->TestParams->TestType == TestTypeLoop &&
            USB_ENDPOINT_DIRECTION_IN(transferParam->Ep.PipeId) && ret > 0) {
            VerifyLoopData(transferParam->TestParams, buffer);
        }

        if (ret < 0) {
            // user pressed 'Q' or 'ctrl+c'
            if (transferParam->TestParams->isUserAborted)
                break;

            // timeout
            if (ret == ERROR_SEM_TIMEOUT || ret == ERROR_OPERATION_ABORTED ||
                ret == ERROR_CANCELLED) {
                transferParam->TotalTimeoutCount++;
                transferParam->RunningTimeoutCount++;
                LOG_ERROR("Timeout #%d %s on EP%02Xh.. \n", transferParam->RunningTimeoutCount,
                          TRANSFER_DISPLAY(transferParam, "reading", "writing"),
                          transferParam->Ep.PipeId);

                if (transferParam->RunningTimeoutCount > transferParam->TestParams->retry)
                    break;
            }

            // other error
            else {
                transferParam->TotalErrorCount++;
                transferParam->RunningErrorCount++;
                LOG_ERROR("failed %s, %d of %d ret=%d, error message : %s\n",
                          TRANSFER_DISPLAY(transferParam, "reading", "writing"),
                          transferParam->RunningErrorCount, transferParam->TestParams->retry + 1,
                          ret, strerror(ret));
                K.ResetPipe(transferParam->TestParams->InterfaceHandle, transferParam->Ep.PipeId);

                if (transferParam->RunningErrorCount > transferParam->TestParams->retry)
                    break;
            }

            ret = 0;
        } else {
            transferParam->RunningTimeoutCount = 0;
            transferParam->RunningErrorCount = 0;
            // log the data to the file
            if (USB_ENDPOINT_DIRECTION_IN(transferParam->Ep.PipeId)) {
                // LOG_MSG("Read %d bytes\n", ret);
                if (transferParam->TestParams->verify &&
                    transferParam->TestParams->TestType != TestTypeLoop) {
                    VerifyData(transferParam, buffer, ret);
                }
            } else {
                // LOG_MSG("Wrote %d bytes\n", ret);
                if (transferParam->TestParams->verify &&
                    transferParam->TestParams->TestType != TestTypeLoop) {
                    // VerifyData(transferParam, buffer, ret);
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

Final:

    for (i = 0; i < transferParam->TestParams->bufferCount; i++) {
        if (transferParam->TransferHandles[i].Overlapped.hEvent) {
            if (transferParam->TransferHandles[i].InUse) {
                if (!K.AbortPipe(transferParam->TestParams->InterfaceHandle,
                                 transferParam->Ep.PipeId) &&
                    !transferParam->TestParams->isUserAborted) {
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

    for (i = 0; i < transferParam->TestParams->bufferCount; i++) {
        if (transferParam->TransferHandles[i].Overlapped.hEvent) {
            if (transferParam->TransferHandles[i].InUse) {
                WaitForSingleObject(transferParam->TransferHandles[i].Overlapped.hEvent,
                                    transferParam->TestParams->timeout);
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
                    TRANSFER_DISPLAY(transferParam, "Read", "Write"), transferParam->Ep.PipeId,
                    transferParam->Ep.MaximumBytesPerInterval,
                    transferParam->EpCompanionDescriptor.bMaxBurst + 1,
                    transferParam->EpCompanionDescriptor.bmAttributes.Isochronous.Mult + 1);
            } else if (transferParam->Ep.PipeType == UsbdPipeTypeBulk) {
                LOG_MSG("%s %s from Ep0x%02X Maximum Bytes Per Interval:%lu Max Bursts:%u Max "
                        "Streams:%u\n",
                        EndpointTypeDisplayString[ENDPOINT_TYPE(transferParam)],
                        TRANSFER_DISPLAY(transferParam, "Read", "Write"), transferParam->Ep.PipeId,
                        transferParam->Ep.MaximumBytesPerInterval,
                        transferParam->EpCompanionDescriptor.bMaxBurst + 1,
                        transferParam->EpCompanionDescriptor.bmAttributes.Bulk.MaxStreams + 1);
            } else {
                LOG_MSG("%s %s from Ep0x%02X Maximum Bytes Per Interval:%lu\n",
                        EndpointTypeDisplayString[ENDPOINT_TYPE(transferParam)],
                        TRANSFER_DISPLAY(transferParam, "Read", "Write"), transferParam->Ep.PipeId,
                        transferParam->Ep.MaximumBytesPerInterval);
            }
        } else {
            LOG_MSG("%s %s Ep0x%02X Maximum Packet Size:%d\n",
                    EndpointTypeDisplayString[ENDPOINT_TYPE(transferParam)],
                    TRANSFER_DISPLAY(transferParam, "Read", "Write"), transferParam->Ep.PipeId,
                    transferParam->Ep.MaximumPacketSize);
        }
    } else {
        LOG_MSG("%s %s Ep0x%02X Maximum Packet Size: %d\n",
                EndpointTypeDisplayString[ENDPOINT_TYPE(transferParam)],
                TRANSFER_DISPLAY(transferParam, "Read", "Write"), transferParam->Ep.PipeId,
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

        LOG_MSG("waiting for Ep%02Xh thread..\n", transferParam->Ep.PipeId);
        WaitForSingleObject(transferParam->ThreadHandle, 100);
        if (msToWait != INFINITE) {
            if ((msToWait - 100) == 0 || (msToWait - 100) > msToWait)
                return FALSE;
        }
    }

    return TRUE;
}
