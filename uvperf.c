/*!********************************************************************
 *   uvperf.c
 *   This is a simple utility to test the performance of USB transfers.
 *   It is designed to be used with the libusbK driver.
 *   The utility will perform a series of transfers to the specified endpoint
 *   and report the results.
 *
 *   Usage:
 *   uvperf -V VERBOSE-v VID -p PID -i INTERFACE -a AltInterface -e ENDPOINT -m TRANSFERMODE
 * -t TIMEOUT -b BUFFERCOUNT -l READLENGTH -w WRITELENGTH -r REPEAT -S -R|W|L
 *
 *   -VVERBOSE       Enable verbose output
 *   -vVID           USB Vendor ID
 *   -pPID           USB Product ID
 *   -iINTERFACE     USB Interface
 *   -aAltInterface  USB Alternate Interface
 *   -eENDPOINT      USB Endpoint
 *   -mTRANSFERMODE  0 = isochronous, 1 = bulk
 *   -tTIMEOUT       USB Transfer Timeout
 *   -bBUFFERCOUNT   Number of buffers to use
 *   -lREADLENGTH    Length of read transfers
 *   -wWRITELENGTH   Length of write transfers
 *   -rREPEAT        Number of transfers to perform
 *   -S              1 = Show transfer data, defulat = 0\n
 *   -R              Read Test
 *   -W              Write Test
 *   -L              Loop Test
 *
 *   Example:
 *   uvperf -v0x1004 -p0xa000 -i0 -a0 -e0x81 -m1 -t1000 -l1024 -r1000 -R
 *
 *   This will perform 1000 bulk transfers of 1024 bytes to endpoint 0x81
 *   on interface 0, alternate setting 0 of a device with VID 0x1004 and PID 0xA000.
 *   The transfers will have a timeout of 1000ms.
 *
 ********************************************************************!*/
#include <conio.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <windows.h>
#include <wtypes.h>

#include "include/libusbk.h"
#include "include/log.h"
#include "include/lusbk_linked_list.h"
#include "include/lusbk_shared.h"

#define MAX_OUTSTANDING_TRANSFERS 10

BOOL verbose = FALSE;

#define LOG_VERBOSE(format, ...)                                                                   \
    do {                                                                                           \
        if (verbose)                                                                               \
            printf(format, ##__VA_ARGS__);                                                         \
    } while (0)

// #define LOGVDAT(format, ...) printf("[data-mismatch] " format, ##__VA_ARGS__)

// #define LOG(LogTypeString, format, ...)                                                            \
//     printf("%s[%s] " format, LogTypeString, __FUNCTION__, __VA_ARGS__)
// #define LOG_NO_FN(LogTypeString, format, ...)                                                      \
//     printf("%s" format "%s", LogTypeString, ##__VA_ARGS__, "")

// #define LOG_ERROR(format, ...) LOG("ERROR: ", format, ##__VA_ARGS__)
// #define LOG_WARNING(format, ...) LOG("WARNING: ", format, ##__VA_ARGS__)
// #define LOG_MSG(format, ...) LOG_NO_FN("", format, ##__VA_ARGS__)
// #define LOG_DEBUG(format, ...) LOG_NO_FN("", format, ##__VA_ARGS__)

// #define LOGERR0(message) LOG_ERROR("%s\n", message)
// #define LOGWAR0(message) LOG_WARNING("%s\n", message)
// #define LOGMSG0(message) LOG_MSG("%s\n", message)
// #define LOGDBG0(message) LOG_DEBUG("%s\n", message)

#define VerifyListLock(mTest)                                                                      \
    while (InterlockedExchange(&((mTest)->verifyLock), 1) != 0)                                    \
    Sleep(0)
#define VerifyListUnlock(mTest) InterlockedExchange(&((mTest)->verifyLock), 0)

static LPCSTR DrvIdNames[8] = {"libusbK", "libusb0", "WinUSB", "libusb0 filter",
                               "Unknown", "Unknown", "Unknown"};
#define GetDrvIdString(DriverID)                                                                   \
    (DrvIdNames[((((LONG)(DriverID)) < 0) || ((LONG)(DriverID)) >= KUSB_DRVID_COUNT)               \
                    ? KUSB_DRVID_COUNT                                                             \
                    : (DriverID)])

KUSB_DRIVER_API K;
CRITICAL_SECTION DisplayCriticalSection;

typedef struct _UVPERF_BUFFER {
    PUCHAR Data;
    LONG dataLenth;
    LONG syncFailed;

    struct _UVPERF_BUFFER *next;
    struct _UVPERF_BUFFER *prev;
} UVPERF_BUFFER, *PUVPERF_BUFFER;

typedef enum _BENCHMARK_DEVICE_COMMAND {
    SET_TEST = 0x0E,
    GET_TEST = 0x0F,
} UVPERF_DEVICE_COMMAND,
    *PUVPERF_DEVICE_COMMAND;

typedef enum _UVPERF_DEVICE_TRANSFER_TYPE {
    TestTypeNone = 0x00,
    TestTypeRead = 0x01,
    TestTypeWrite = 0x02,
    TestTypeLoop = TestTypeRead | TestTypeWrite,
} UVPERF_DEVICE_TRANSFER_TYPE,
    *PUVPERF_DEVICE_TRANSFER_TYPE;

typedef enum _UVPERF_TRANSFER_MODE {
    TRANSFER_MODE_SYNC,
    TRANSFER_MODE_ASYNC,
} UVPERF_TRANSFER_MODE;

typedef struct _UVPERF_PARAM {
    int vid;
    int pid;
    int intf;
    int altf;
    int endpoint;
    int timeout;
    int refresh;
    int retry;
    int bufferlength;
    int allocBufferSize;
    int readlenth;
    int writelength;
    int bufferCount;
    int repeat;
    int fixedIsoPackets;
    int priority;
    BOOL fileIO;
    BOOL ShowTransfer;
    BOOL useList;
    BOOL verify;
    BOOL verifyDetails;
    UVPERF_DEVICE_TRANSFER_TYPE TestType;
    UVPERF_TRANSFER_MODE TransferMode;

    KLST_HANDLE DeviceList;
    KLST_DEVINFO_HANDLE SelectedDeviceProfile;
    HANDLE DeviceHandle;
    KUSB_HANDLE InterfaceHandle;
    USB_DEVICE_DESCRIPTOR DeviceDescriptor;
    USB_CONFIGURATION_DESCRIPTOR ConfigDescriptor;
    USB_INTERFACE_DESCRIPTOR InterfaceDescriptor;
    USB_ENDPOINT_DESCRIPTOR EndpointDescriptor;
    WINUSB_PIPE_INFORMATION_EX PipeInformation[32];
    BOOL isCancelled;
    BOOL isUserAborted;

    volatile long verifyLock;
    UVPERF_BUFFER *VerifyList;

    unsigned char verifyBuffer;
    unsigned short verifyBufferSize;
    BOOL use_UsbK_Init;
    BOOL listDevicesOnly;
    unsigned long deviceSpeed;

    BOOL ReadLogEnabled;
    FILE *ReadLogFile;
    BOOL WriteLogEnabled;
    FILE *WriteLogFile;
    FILE *ReadFile;
    FILE *WriteFile;
    char ReadFileName[MAX_PATH];
    char WriteFileName[MAX_PATH];

    UCHAR UseRawIO;
    UCHAR DefaultAltSetting;
    BOOL UseIsoAsap;
    BYTE *VerifyBuffer;

    unsigned char defaultAltSetting;
} UVPERF_PARAM, *PUVPERF_PARAM;

typedef struct _BENCHMARK_ISOCH_RESULTS {
    UINT GoodPackets;
    UINT BadPackets;
    UINT Length;
    UINT TotalPackets;
} BENCHMARK_ISOCH_RESULTS;

typedef struct _UVPERF_TRANSFER_HANDLE {
    KISOCH_HANDLE IsochHandle;
    OVERLAPPED Overlapped;
    BOOL InUse;
    PUCHAR Data;
    INT DataMaxLength;
    INT ReturnCode;
    BENCHMARK_ISOCH_RESULTS IsochResults;
} UVPERF_TRANSFER_HANDLE, *PUVPERF_TRANSFER_HANDLE;

typedef struct _UVPERF_TRANSFER_PARAM {
    PUVPERF_PARAM TestParms;
    unsigned int frameNumber;
    unsigned int numberOFIsoPackets;
    HANDLE ThreadHandle;
    DWORD ThreadId;
    WINUSB_PIPE_INFORMATION_EX Ep;
    USB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR EpCompanionDescriptor;
    BOOL HasEpCompanionDescriptor;
    BOOL isRunning;

    LONGLONG TotalTransferred;
    LONG LastTransferred;

    LONG Packets;
    struct timespec StartTick;
    struct timespec LastTick;
    struct timespec LastStartTick;

    int shortTrasnferred;

    int TotalTimeoutCount;
    int RunningTimeoutCount;

    int totalErrorCount;
    int runningErrorCount;

    int TotalErrorCount;
    int RunningErrorCount;

    int shortTransferCount;

    int transferHandleNextIndex;
    int transferHandleWaitIndex;
    int outstandingTransferCount;

    UVPERF_TRANSFER_HANDLE TransferHandles[MAX_OUTSTANDING_TRANSFERS];
    BENCHMARK_ISOCH_RESULTS IsochResults;

    UCHAR Buffer[0];
} UVPERF_TRANSFER_PARAM, *PUVPERF_TRANSFER_PARAM;

#include <pshpack1.h>
typedef struct _KBENCH_CONTEXT_LSTK {
    BYTE Selected;
} KBENCH_CONTEXT_LSTK, *PKBENCH_CONTEXT_LSTK;
#include <poppack.h>

BOOL Bench_Open(__in PUVPERF_PARAM TestParms);

BOOL Bench_Configure(__in KUSB_HANDLE handle, __in UVPERF_DEVICE_COMMAND command, __in UCHAR intf,
                     __inout PUVPERF_DEVICE_TRANSFER_TYPE testType);

void ShowUsage();
void SetParamsDefaults(PUVPERF_PARAM TestParms);
int GetDeviceInfoFromList(PUVPERF_PARAM TestParms);
int GetDeviceParam(PUVPERF_PARAM TestParms);

int ParseArgs(PUVPERF_PARAM TestParms, int argc, char **argv);
void ShowParms(PUVPERF_PARAM TestParms);
PUVPERF_TRANSFER_PARAM CreateTransferParam(PUVPERF_PARAM TestParms, int endpointID);
void GetAverageBytesSec(PUVPERF_TRANSFER_PARAM transferParam, DOUBLE *bps);
void GetCurrentBytesSec(PUVPERF_TRANSFER_PARAM transferParam, DOUBLE *bps);
void ShowRunningStatus(PUVPERF_TRANSFER_PARAM readParam, PUVPERF_TRANSFER_PARAM writeParam);
DWORD TransferThread(PUVPERF_TRANSFER_PARAM transferParam);
void FreeTransferParam(PUVPERF_TRANSFER_PARAM *transferParamRef);

void FileIOOpen(PUVPERF_PARAM TestParms);
void FileIORead(PUVPERF_PARAM TestParms, PUVPERF_TRANSFER_PARAM transferParam);
void FileIOWrite(PUVPERF_PARAM TestParms, PUVPERF_TRANSFER_PARAM transferParam);
void FileIOClose(PUVPERF_PARAM TestParms);

#define TRANSFER_DISPLAY(TransferParam, ReadingString, WritingString)                              \
    ((TransferParam->Ep.PipeId & USB_ENDPOINT_DIRECTION_MASK) ? ReadingString : WritingString)

#define INC_ROLL(IncField, RollOverValue)                                                          \
    if ((++IncField) >= RollOverValue)                                                             \
    IncField = 0

#define ENDPOINT_TYPE(TransferParam) (TransferParam->Ep.PipeType & 3)
const char *TestDisplayString[] = {"None", "Read", "Write", "Loop", NULL};
const char *EndpointTypeDisplayString[] = {"Control", "Isochronous", "Bulk", "Interrupt", NULL};

char *GetWinErrorMessage(DWORD errorCode) {
    char *buffer = NULL;

    errorCode = errorCode ? errorCode : GetLastError();
    if (!errorCode)
        return NULL;

    if (errorCode == ERROR_GEN_FAILURE || errorCode == ERROR_DEVICE_NOT_CONNECTED) {
        fprintf(stderr, "Device disconnected.\n");
    }

    DWORD flags =
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS;
    if (FormatMessageA(flags, NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       (LPSTR)&buffer, 0, NULL) == 0) {
        fprintf(stderr, "Failed to retrieve the error message for code %lu\n", errorCode);
        return NULL;
    }

    return buffer;
}

void FreeWinErrorMessage(char *message) {
    if (message) {
        LocalFree(message);
    }
}

LONG WinError(__in_opt DWORD errorCode) {
    LPSTR buffer = NULL;

    errorCode = errorCode ? labs(errorCode) : GetLastError();
    if (!errorCode)
        return errorCode;

    if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, errorCode,
                       0, (LPSTR)&buffer, 0, NULL) > 0) {
        LOG_ERROR("%s\n", buffer);
        SetLastError(0);
    } else {
        LOGERR0("FormatMessage error!\n");
    }

    if (buffer)
        LocalFree(buffer);

    return -labs(errorCode);
}

void AppendLoopBuffer(PUVPERF_PARAM TestParms, unsigned char *buffer, unsigned int length) {

    if (TestParms->verify && TestParms->TestType == TestTypeLoop) {
        UVPERF_BUFFER *newVerifyBuf = malloc(sizeof(UVPERF_BUFFER) + length);

        memset(newVerifyBuf, 1, sizeof(UVPERF_BUFFER));

        newVerifyBuf->Data = (unsigned char *)newVerifyBuf + sizeof(UVPERF_BUFFER);
        newVerifyBuf->dataLenth = length;
        memcpy(newVerifyBuf->Data, buffer, length);

        VerifyListLock(TestParms);
        DL_APPEND(TestParms->VerifyList, newVerifyBuf);
        VerifyListUnlock(TestParms);
    }
}

BOOL Bench_Open(__in PUVPERF_PARAM TestParams) {
    UCHAR altSetting;
    KUSB_HANDLE associatedHandle;
    UINT transferred;
    KLST_DEVINFO_HANDLE deviceInfo;

    TestParams->SelectedDeviceProfile = NULL;

    LstK_MoveReset(TestParams->DeviceList);

    while (LstK_MoveNext(TestParams->DeviceList, &deviceInfo)) {
        // enabled
        UINT userContext = (UINT)LibK_GetContext(deviceInfo, KLIB_HANDLE_TYPE_LSTINFOK);
        if (userContext != TRUE)
            continue;

        if (!LibK_LoadDriverAPI(&K, deviceInfo->DriverID)) {
            WinError(0);
            LOG_WARNING("can not load driver api %s\n", GetDrvIdString(deviceInfo->DriverID));
            continue;
        }
        if (!TestParams->use_UsbK_Init) {
            TestParams->DeviceHandle =
                CreateFileA(deviceInfo->DevicePath, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED, NULL);

            if (!TestParams->DeviceHandle || TestParams->DeviceHandle == INVALID_HANDLE_VALUE) {
                WinError(0);
                TestParams->DeviceHandle = NULL;
                LOG_WARNING("can not create device handle\n%s\n", deviceInfo->DevicePath);
                continue;
            }

            if (!K.Initialize(TestParams->DeviceHandle, &TestParams->InterfaceHandle)) {
                WinError(0);
                CloseHandle(TestParams->DeviceHandle);
                TestParams->DeviceHandle = NULL;
                TestParams->InterfaceHandle = NULL;
                LOG_WARNING("can not initialize device\n%s\n", deviceInfo->DevicePath);
                continue;
            }
        } else {
            if (!K.Init(&TestParams->InterfaceHandle, deviceInfo)) {
                WinError(0);
                TestParams->DeviceHandle = NULL;
                TestParams->InterfaceHandle = NULL;
                LOG_WARNING("can not open device\n%s\n", deviceInfo->DevicePath);
                continue;
            }
        }

        if (!K.GetDescriptor(TestParams->InterfaceHandle, USB_DESCRIPTOR_TYPE_DEVICE, 0, 0,
                             (PUCHAR)&TestParams->DeviceDescriptor,
                             sizeof(TestParams->DeviceDescriptor), &transferred)) {
            WinError(0);

            K.Free(TestParams->InterfaceHandle);
            TestParams->InterfaceHandle = NULL;

            if (!TestParams->use_UsbK_Init) {
                CloseHandle(TestParams->DeviceHandle);
                TestParams->DeviceHandle = NULL;
            }

            LOG_WARNING("can not get device descriptor\n%s\n", deviceInfo->DevicePath);
            continue;
        }
        TestParams->vid = (int)TestParams->DeviceDescriptor.idVendor;
        TestParams->pid = (int)TestParams->DeviceDescriptor.idProduct;

    NextInterface:

        // While searching for hardware specifics we are also gathering information and storing it
        // in our test.
        memset(&TestParams->InterfaceDescriptor, 0, sizeof(TestParams->InterfaceDescriptor));
        altSetting = 0;

        while (K.QueryInterfaceSettings(TestParams->InterfaceHandle, altSetting,
                                        &TestParams->InterfaceDescriptor)) {
            // found an interface
            UCHAR pipeIndex = 0;
            int hasIsoEndpoints = 0;
            int hasZeroMaxPacketEndpoints = 0;

            memset(&TestParams->PipeInformation, 0, sizeof(TestParams->PipeInformation));
            while (K.QueryPipeEx(TestParams->InterfaceHandle, altSetting, pipeIndex,
                                 &TestParams->PipeInformation[pipeIndex])) {
                // found a pipe
                if (TestParams->PipeInformation[pipeIndex].PipeType == UsbdPipeTypeIsochronous)
                    hasIsoEndpoints++;

                if (!TestParams->PipeInformation[pipeIndex].MaximumPacketSize)
                    hasZeroMaxPacketEndpoints++;

                pipeIndex++;
            }

            if (pipeIndex > 0) {
                // -1 means the user din't specifiy so we find the most suitable device.
                //
                if (((TestParams->intf == -1) ||
                     (TestParams->intf == TestParams->InterfaceDescriptor.bInterfaceNumber)) &&
                    ((TestParams->altf == -1) ||
                     (TestParams->altf == TestParams->InterfaceDescriptor.bAlternateSetting))) {
                    // if the user actually specifies an alt iso setting with zero
                    // MaxPacketEndpoints we let let them.
                    if (TestParams->altf == -1 && hasIsoEndpoints && hasZeroMaxPacketEndpoints) {
                        // user didn't specfiy and we know we can't tranfer with this alt setting so
                        // skip it.
                        LOG_MSG("skipping interface %02X:%02X. zero-length iso endpoints exist.\n",
                                TestParams->InterfaceDescriptor.bInterfaceNumber,
                                TestParams->InterfaceDescriptor.bAlternateSetting);
                    } else {
                        // this is the one we are looking for.
                        TestParams->intf = TestParams->InterfaceDescriptor.bInterfaceNumber;
                        TestParams->altf = TestParams->InterfaceDescriptor.bAlternateSetting;
                        TestParams->SelectedDeviceProfile = deviceInfo;

                        // some buffering is required for iso.
                        if (hasIsoEndpoints && TestParams->bufferCount == 1)
                            TestParams->bufferCount++;

                        TestParams->DefaultAltSetting = 0;
                        K.GetCurrentAlternateSetting(TestParams->InterfaceHandle,
                                                     &TestParams->DefaultAltSetting);
                        if (!K.SetCurrentAlternateSetting(
                                TestParams->InterfaceHandle,
                                TestParams->InterfaceDescriptor.bAlternateSetting)) {
                            LOG_ERROR("can not find alt interface %02X\n", TestParams->altf);
                            return FALSE;
                        }
                        return TRUE;
                    }
                }
            }

            altSetting++;
            memset(&TestParams->InterfaceDescriptor, 0, sizeof(TestParams->InterfaceDescriptor));
        }
        if (K.GetAssociatedInterface(TestParams->InterfaceHandle, 0, &associatedHandle)) {
            // this device has more interfaces to look at.
            //
            K.Free(TestParams->InterfaceHandle);
            TestParams->InterfaceHandle = associatedHandle;
            goto NextInterface;
        }

        // This one didn't match the test specifics; continue on to the next potential match.
        K.Free(TestParams->InterfaceHandle);
        TestParams->InterfaceHandle = NULL;
    }

    LOG_ERROR("device doesn't have %02X interface and %02X alt interface\n", TestParams->intf,
              TestParams->altf);
    return FALSE;
}

BOOL Bench_Configure(__in KUSB_HANDLE handle, __in UVPERF_DEVICE_COMMAND command, __in UCHAR intf,
                     __inout PUVPERF_DEVICE_TRANSFER_TYPE testType) {
    UCHAR buffer[1];
    UINT transferred = 0;
    WINUSB_SETUP_PACKET Pkt;
    KUSB_SETUP_PACKET *defPkt = (KUSB_SETUP_PACKET *)&Pkt;

    memset(&Pkt, 0, sizeof(Pkt));
    defPkt->BmRequest.Dir = BMREQUEST_DIR_DEVICE_TO_HOST;
    defPkt->BmRequest.Type = BMREQUEST_TYPE_VENDOR;
    defPkt->Request = (UCHAR)command;
    defPkt->Value = (UCHAR)*testType;
    defPkt->Index = intf;
    defPkt->Length = 1;

    if (!handle || handle == INVALID_HANDLE_VALUE) {
        return WinError(ERROR_INVALID_HANDLE);
    }

    if (K.ControlTransfer(handle, Pkt, buffer, 1, &transferred, NULL)) {
        if (transferred)
            return TRUE;
    }

    LOGERR0("can not configure device\n");
    return WinError(0);
}

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
        // Index 0 is always 0.
        // The key is always at index 1
        verifyData[1] = keyC;

        if (memcmp(&data[dataIndex], verifyData, verifyDataSize) != 0) {
            // Packet verification failed.

            // Reset the key byte on the next packet.
            seedKey = TRUE;

            // LOGVDAT("Packet=#%d Data=#%d\n", packetIndex, dataIndex);

            if (transferParam->TestParms->verifyDetails) {
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
    while (transferParam->outstandingTransferCount < transferParam->TestParms->bufferCount) {
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
                                                    transferParam->TestParms->allocBufferSize);
        } else {
            // re-initialize and re-use the overlapped
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
        }

        // Isochronous write pipe -> doesn't need right now
        else {
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

        // Submit this transfer now.
        handle->ReturnCode = ret = -labs(transferErrorCode);
        if (ret < 0) {
            handle->InUse = FALSE;
            goto Done;
        }

        // Mark this handle has InUse.
        handle->InUse = TRUE;

        // When transfers ir successfully submitted, OutstandingTransferCount goes up; when
        // they are completed it goes down.
        //
        transferParam->outstandingTransferCount++;

        // Move TransferHandleNextIndex to the next available transfer.
        INC_ROLL(transferParam->transferHandleNextIndex, transferParam->TestParms->bufferCount);
    }

    // If the number of outstanding transfers has reached the limit, wait for the
    // oldest outstanding transfer to complete.
    //
    if (transferParam->outstandingTransferCount == transferParam->TestParms->bufferCount) {
        UINT transferred;
        // TransferHandleWaitIndex is the index of the oldest outstanding transfer.
        *handleRef = handle =
            &transferParam->TransferHandles[transferParam->transferHandleWaitIndex];

        // Only wait, cancelling & freeing is handled by the caller.
        if (WaitForSingleObject(handle->Overlapped.hEvent, transferParam->TestParms->timeout) !=
            WAIT_OBJECT_0) {
            if (!transferParam->TestParms->isUserAborted) {
                ret = WinError(0);
            } else
                ret = -labs(GetLastError());

            handle->ReturnCode = ret;
            goto Done;
        }

        if (!K.GetOverlappedResult(transferParam->TestParms->InterfaceHandle, &handle->Overlapped,
                                   &transferred, FALSE)) {
            if (!transferParam->TestParms->isUserAborted) {
                ret = WinError(0);
                LOGMSG0("여기!!!\n");
            } else
                ret = -labs(GetLastError());

            handle->ReturnCode = ret;
            goto Done;
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
            goto Done;

        // Mark this handle has no longer InUse.
        handle->InUse = FALSE;

        // When transfers ir successfully submitted, OutstandingTransferCount goes up; when
        // they are completed it goes down.
        //
        transferParam->outstandingTransferCount--;

        // Move TransferHandleWaitIndex to the oldest outstanding transfer.
        INC_ROLL(transferParam->transferHandleWaitIndex, transferParam->TestParms->bufferCount);
    }

Done:
    return ret;
}

// todo : later for Loop data
void VerifyLoopData() { return; }

void ShowUsage() {
    LOG_MSG("Version : V0.2.0\n\n");
    LOG_MSG(
        "Usage: uvperf -v VID -p PID -i INTERFACE -a AltInterface -e ENDPOINT -m TRANSFERMODE "
        "-t TIMEOUT -f FileIO -b BUFFERCOUNT-l READLENGTH -w WRITELENGTH -r REPEAT -S -R|-W|-L\n");
    LOG_MSG("\t-v VID           USB Vendor ID\n");
    LOG_MSG("\t-p PID           USB Product ID\n");
    LOG_MSG("\t-i INTERFACE     USB Interface\n");
    LOG_MSG("\t-a AltInterface  USB Alternate Interface\n");
    LOG_MSG("\t-e ENDPOINT      USB Endpoint\n");
    LOG_MSG("\t-m TRANSFER      0 = isochronous, 1 = bulk\n");
    LOG_MSG("\t-t TIMEOUT       USB Transfer Timeout\n");
    LOG_MSG("\t-f FileIO        Use file I/O, default : FALSE\n");
    LOG_MSG("\t-b BUFFERCOUNT   Number of buffers to use\n");
    LOG_MSG("\t-l READLENGTH    Length of read transfers\n");
    LOG_MSG("\t-w WRITELENGTH   Length of write transfers\n");
    LOG_MSG("\t-r REPEAT        Number of transfers to perform\n");
    LOG_MSG("\t-S               Show transfer data, default : FALSE\n");
    LOG_MSG("\t-R               Read Test\n");
    LOG_MSG("\t-W               Write Test\n");
    LOG_MSG("\t-L               Loop Test\n");
    LOG_MSG("\n");
    LOG_MSG("Example:\n");
    LOG_MSG("uvperf -v 0x1004 -p 0xa000 -i 0 -a 0 -e 0x81 -m 0 -t 1000 -l 1024 -r 1000 -R\n");
    LOG_MSG("This will perform 1000 bulk transfers of 1024 bytes to endpoint 0x81\n");
    LOG_MSG("on interface 0, alternate setting 0 of a device with VID 0x1004 and PID 0xA000.\n");
    LOG_MSG("The transfers will have a timeout of 1000ms.\n");
}

void SetParamsDefaults(PUVPERF_PARAM TestParms) {
    memset(TestParms, 0, sizeof(*TestParms));

    TestParms->vid = 0x1004;
    TestParms->pid = 0xA000;
    TestParms->intf = -1;
    TestParms->altf = -1;
    TestParms->endpoint = 0x00;
    TestParms->TransferMode = TRANSFER_MODE_SYNC;
    TestParms->TestType = TestTypeRead;
    TestParms->timeout = 3000;
    TestParms->fileIO = FALSE;
    TestParms->bufferlength = 1024;
    TestParms->refresh = 1000;
    TestParms->readlenth = TestParms->bufferlength;
    TestParms->writelength = TestParms->bufferlength;
    TestParms->verify = 1;
    TestParms->bufferCount = 1;
    TestParms->ShowTransfer = FALSE;
    TestParms->UseRawIO = 0xFF;
}

int GetDeviceInfoFromList(PUVPERF_PARAM TestParms) {
    UCHAR selection;
    UCHAR count = 0;
    KLST_DEVINFO_HANDLE deviceInfo = NULL;

    LstK_MoveReset(TestParms->DeviceList);

    if (TestParms->listDevicesOnly) {
        while (LstK_MoveNext(TestParms->DeviceList, &deviceInfo)) {
            count++;
            LOG_MSG("%02u. %s (%s) [%s]\n", count, deviceInfo->DeviceDesc, deviceInfo->DeviceID,
                    GetDrvIdString(deviceInfo->DriverID));
        }

        return ERROR_SUCCESS;
    } else {
        while (LstK_MoveNext(TestParms->DeviceList, &deviceInfo) && count < 9) {
            LOG_MSG("%u. %s (%s) [%s]\n", count + 1, deviceInfo->DeviceDesc, deviceInfo->DeviceID,
                    GetDrvIdString(deviceInfo->DriverID));
            count++;

            // enabled
            LibK_SetContext(deviceInfo, KLIB_HANDLE_TYPE_LSTINFOK, (KLIB_USER_CONTEXT)TRUE);
        }

        if (!count) {
            LOG_ERROR("can not find vid : 0x%04X, pid : 0x%04X device\n", TestParms->vid,
                      TestParms->pid);
            return -1;
        }

        int validSelection = 0;

        do {
            LOG_MSG("Select device (1-%u): ", count);
            while (_kbhit()) {
                _getch();
            }

            selection = (CHAR)_getche() - (UCHAR)'0';
            LOGMSG0("\n");
            if (selection == 'q' - '0') {
                return -1;
            }
            if (selection > 0 && selection <= count) {
                count = 0;
                while (LstK_MoveNext(TestParms->DeviceList, &deviceInfo) && ++count != selection) {
                    // disabled
                    LibK_SetContext(deviceInfo, KLIB_HANDLE_TYPE_LSTINFOK,
                                    (KLIB_USER_CONTEXT)FALSE);
                }

                if (!deviceInfo) {
                    LOGERR0("Unknown selection\n");
                    continue;
                }

                TestParms->SelectedDeviceProfile = deviceInfo;
                validSelection = 1;
            } else {
                fprintf(stderr, "Invalid selection. Please select a number between 1 and %u\n",
                        count);
                fprintf(stderr, "Press 'q' to quit\n");
            }
        } while (!validSelection);

        return ERROR_SUCCESS;
    }

    return -1;
}

int GetDeviceParam(PUVPERF_PARAM TestParms) {
    char id[MAX_PATH];
    KLST_DEVINFO_HANDLE deviceInfo = NULL;

    LstK_MoveReset(TestParms->DeviceList);

    while (LstK_MoveNext(TestParms->DeviceList, &deviceInfo)) {
        int vid = -1;
        int pid = -1;
        int mi = -1;
        PCHAR chID;

        // disabled
        LibK_SetContext(deviceInfo, KLIB_HANDLE_TYPE_LSTINFOK, (KLIB_USER_CONTEXT)FALSE);

        memset(id, 0, sizeof(id));
        strcpy_s(id, MAX_PATH - 1, deviceInfo->DeviceID);
        _strlwr_s(id, MAX_PATH);

        if ((chID = strstr(id, "vid_")) != NULL)
            sscanf_s(chID, "vid_%04x", &vid);
        if ((chID = strstr(id, "pid_")) != NULL)
            sscanf_s(chID, "pid_%04x", &pid);
        if ((chID = strstr(id, "mi_")) != NULL)
            sscanf_s(chID, "mi_%02x", &mi);

        if (TestParms->vid == vid && TestParms->pid == pid) {
            // enabled
            LibK_SetContext(deviceInfo, KLIB_HANDLE_TYPE_LSTINFOK, (KLIB_USER_CONTEXT)TRUE);
        }
    }

    return ERROR_SUCCESS;
}

int ParseArgs(PUVPERF_PARAM TestParms, int argc, char **argv) {
    int i;
    int arg;
    char *temp;
    int value;
    int status = 0;

    int c;
    while ((c = getopt(argc, argv, "Vv:p:i:a:e:m:t:f:b:l:w:r:SRWL")) != -1) {
        switch (c) {
        case 'V':
            verbose = TRUE;
            break;
        case 'v':
            TestParms->vid = strtol(optarg, NULL, 0);
            break;
        case 'p':
            TestParms->pid = strtol(optarg, NULL, 0);
            break;
        case 'i':
            TestParms->intf = strtol(optarg, NULL, 0);
            break;
        case 'a':
            TestParms->altf = strtol(optarg, NULL, 0);
            break;
        case 'e':
            TestParms->endpoint = strtol(optarg, NULL, 0);
            break;
        case 'm':
            TestParms->TransferMode =
                (strtol(optarg, NULL, 0) ? TRANSFER_MODE_ASYNC : TRANSFER_MODE_SYNC);
            break;
        case 't':
            TestParms->timeout = strtol(optarg, NULL, 0);
            break;
        case 'f':
            TestParms->fileIO = TRUE;
            break;
        case 'b':
            TestParms->bufferCount = strtol(optarg, NULL, 0);
            if (TestParms->bufferCount > 1) {
                TestParms->TransferMode = TRANSFER_MODE_ASYNC;
            }
            break;
        case 'l':
            TestParms->readlenth = strtol(optarg, NULL, 0);
            break;
        case 'w':
            TestParms->writelength = strtol(optarg, NULL, 0);
            break;
        case 'r':
            TestParms->repeat = strtol(optarg, NULL, 0);
            break;
        case 'S':
            TestParms->ShowTransfer = TRUE;
            break;
        case 'R':
            TestParms->TestType = TestTypeRead;
            break;
        case 'W':
            TestParms->TestType = TestTypeWrite;
            break;
        case 'L':
            TestParms->TestType = TestTypeLoop;
            break;
        default:
            LOGERR0("Invalid argument\n");
            status = -1;
            break;
        }
    }

    if (optind < argc) {
        printf("Non-option arguments: ");
        while (optind < argc)
            printf("%s ", argv[optind++]);
        printf("\n");
    }

    return status;
}

void ShowParms(PUVPERF_PARAM TestParms) {
    if (!TestParms)
        return;

    LOG_MSG("\tDriver         :  %s\n", GetDrvIdString(TestParms->SelectedDeviceProfile->DriverID));
    LOG_MSG("\tVID:           :  0x%04X\n", TestParms->vid);
    LOG_MSG("\tPID:           :  0x%04X\n", TestParms->pid);
    LOG_MSG("\tInterface:     :  %d\n", TestParms->intf);
    LOG_MSG("\tAlt Interface: :  %d\n", TestParms->altf);
    LOG_MSG("\tEndpoint:      :  0x%02X\n", TestParms->endpoint);
    LOG_MSG("\tTransfer mode  :  %s\n", TestParms->TransferMode ? "Isochronous" : "Bulk");
    LOG_MSG("\tTimeout:       :  %d\n", TestParms->timeout);
    LOG_MSG("\tRead Length:   :  %d\n", TestParms->readlenth);
    LOG_MSG("\tWrite Length:  :  %d\n", TestParms->writelength);
    LOG_MSG("\tRepeat:        :  %d\n", TestParms->repeat);
    LOG_MSG("\n");
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
    }

    if (writeParam != NULL && (!gWriteParamTransferParam.StartTick.tv_nsec ||
                               (gWriteParamTransferParam.StartTick.tv_sec +
                                gWriteParamTransferParam.StartTick.tv_nsec / 1000000000.0) >
                                   (gWriteParamTransferParam.LastTick.tv_sec +
                                    gWriteParamTransferParam.LastTick.tv_nsec / 1000000000.0))) {
        LOG_MSG("Synchronizing Write %d..\n", abs(gWriteParamTransferParam.Packets));
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
            LOG_MSG("Average %.2f Mbps\n", (bpsReadOverall + bpsWriteOverall) * 8 / 1000 / 1000);
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
                LOG_MSG("\n\n");
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
            // user pressed 'Q' or 'ctrl+c'
            if (transferParam->TestParms->isUserAborted)
                break;

            // timeout
            if (ret == ERROR_SEM_TIMEOUT || ret == ERROR_OPERATION_ABORTED ||
                ret == ERROR_CANCELLED) {
                transferParam->TotalTimeoutCount++;
                transferParam->RunningTimeoutCount++;
                LOG_ERROR("Timeout #%d %s on EP%02Xh.. \n", transferParam->RunningTimeoutCount,
                          TRANSFER_DISPLAY(transferParam, "reading", "writing"),
                          transferParam->Ep.PipeId);

                if (transferParam->RunningTimeoutCount > transferParam->TestParms->retry)
                    break;
            }

            // other error
            else {
                transferParam->TotalErrorCount++;
                transferParam->RunningErrorCount++;
                LOG_ERROR("failed %s, %d of %d ret=%d\n",
                          TRANSFER_DISPLAY(transferParam, "reading", "writing"),
                          transferParam->RunningErrorCount, transferParam->TestParms->retry + 1,
                          ret);

                char *buffer = GetWinErrorMessage(-ret);
                LOG_ERROR("error message : %s\n", buffer);
                FreeWinErrorMessage(buffer);
                K.ResetPipe(transferParam->TestParms->InterfaceHandle, transferParam->Ep.PipeId);

                if (transferParam->RunningErrorCount > transferParam->TestParms->retry)
                    break;
            }

            ret = 0;
        } else {
            transferParam->RunningTimeoutCount = 0;
            transferParam->RunningErrorCount = 0;
            // TODO : log the data to the file
            if (USB_ENDPOINT_DIRECTION_IN(transferParam->Ep.PipeId)) {
                // LOG_MSG("Read %d bytes\n", ret);
                if (transferParam->TestParms->verify &&
                    transferParam->TestParms->TestType != TestTypeLoop) {
                    VerifyData(transferParam, buffer, ret);
                }
            } else {
                // LOG_MSG("Wrote %d bytes\n", ret);
                if (transferParam->TestParms->verify &&
                    transferParam->TestParms->TestType != TestTypeLoop) {
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

Done:

    for (i = 0; i < transferParam->TestParms->bufferCount; i++) {
        if (transferParam->TransferHandles[i].Overlapped.hEvent) {
            if (transferParam->TransferHandles[i].InUse) {
                if (!K.AbortPipe(transferParam->TestParms->InterfaceHandle,
                                 transferParam->Ep.PipeId) &&
                    !transferParam->TestParms->isUserAborted) {
                    ret = WinError(0);
                    LOG_ERROR("failed cancelling transfer ret = %d\n", ret);
                    char *buffer = GetWinErrorMessage(-ret);
                    LOG_ERROR("error message : %s", buffer);
                    FreeWinErrorMessage(buffer);
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
                LOG_ERROR("Buffer size is not correct for isochronous pipe %02X\n",
                          transferParam->Ep.PipeId);
                LOG_ERROR("- Buffer size must be an interval of %u\n", minBufferSize);
                FreeTransferParam(&transferParam);
                goto Done;
            }

            for (bufferIndex = 0; bufferIndex < transferParam->TestParms->bufferCount;
                 bufferIndex++) {
                transferParam->TransferHandles[bufferIndex].Overlapped.hEvent =
                    CreateEvent(NULL, TRUE, FALSE, NULL);

                // Data buffer(s) are located at the end of the transfer param.
                transferParam->TransferHandles[bufferIndex].Data =
                    transferParam->Buffer + (bufferIndex * transferParam->TestParms->bufferlength);

                if (!IsochK_Init(&transferParam->TransferHandles[bufferIndex].IsochHandle,
                                 TestParam->InterfaceHandle, transferParam->Ep.PipeId,
                                 numIsoPackets, transferParam->TransferHandles[bufferIndex].Data,
                                 transferParam->TestParms->bufferlength)) {
                    LOG_ERROR("IsochK_Init failed for isochronous pipe %02X\n",
                              transferParam->Ep.PipeId);
                    LOG_ERROR("- ErrorCode = %u\n", GetLastError());
                    FreeTransferParam(&transferParam);
                    goto Done;
                }

                if (!IsochK_SetPacketOffsets(
                        transferParam->TransferHandles[bufferIndex].IsochHandle,
                        transferParam->Ep.MaximumBytesPerInterval)) {
                    LOG_ERROR("IsochK_SetPacketOffsets failed for isochronous pipe %02X\n",
                              transferParam->Ep.PipeId);
                    LOG_ERROR("- ErrorCode = %u\n", GetLastError());
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

        // If verify mode is on, this is a loop test, and this is a write endpoint, fill
        // the buffers with the same test data sent by a benchmark device when running
        // a read only test.
        if (transferParam->TestParms->TestType == TestTypeLoop &&
            USB_ENDPOINT_DIRECTION_OUT(pipeInfo->PipeId)) {
            // Data Format:
            // [0][KeyByte] 2 3 4 5 ..to.. wMaxPacketSize (if data byte rolls it is incremented to
            // 1) Increment KeyByte and repeat
            //
            BYTE indexC = 0;
            INT bufferIndex = 0;
            WORD dataIndex;
            INT packetIndex;
            INT packetCount = ((transferParam->TestParms->bufferCount * TestParam->readlenth) /
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

Done:
    if (!transferParam)
        LOGERR0("failed creating transfer param!\n");

    return transferParam;
}

void GetAverageBytesSec(PUVPERF_TRANSFER_PARAM transferParam, DOUBLE *byteps) {
    DWORD elapsedSeconds;
    if (!transferParam)
        return;

    if (transferParam->StartTick.tv_nsec &&
        (transferParam->StartTick.tv_sec + transferParam->StartTick.tv_nsec / 1000000000.0) <
            (transferParam->LastTick.tv_sec + transferParam->LastTick.tv_nsec / 1000000000.0)) {

        elapsedSeconds =
            (transferParam->LastTick.tv_sec - transferParam->StartTick.tv_sec) +
            (transferParam->LastTick.tv_nsec - transferParam->StartTick.tv_nsec) / 1000000000.0;

        *byteps = (DOUBLE)transferParam->TotalTransferred / elapsedSeconds;
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
                    TRANSFER_DISPLAY(transferParam, "Read", "Write"), transferParam->Ep.PipeId,
                    transferParam->Ep.MaximumBytesPerInterval,
                    transferParam->EpCompanionDescriptor.bMaxBurst + 1,
                    transferParam->EpCompanionDescriptor.bmAttributes.Isochronous.Mult + 1);
            }

            else if (transferParam->Ep.PipeType == UsbdPipeTypeBulk) {
                LOG_MSG("%s %s from Ep0x%02X Maximum Bytes Per Interval:%lu Max Bursts:%u Max "
                        "Streams:%u\n",
                        EndpointTypeDisplayString[ENDPOINT_TYPE(transferParam)],
                        TRANSFER_DISPLAY(transferParam, "Read", "Write"), transferParam->Ep.PipeId,
                        transferParam->Ep.MaximumBytesPerInterval,
                        transferParam->EpCompanionDescriptor.bMaxBurst + 1,
                        transferParam->EpCompanionDescriptor.bmAttributes.Bulk.MaxStreams + 1);
            }

            else {
                LOG_MSG("%s %s from Ep0x%02X Maximum Bytes Per Interval:%lu\n",
                        EndpointTypeDisplayString[ENDPOINT_TYPE(transferParam)],
                        TRANSFER_DISPLAY(transferParam, "Read", "Write"), transferParam->Ep.PipeId,
                        transferParam->Ep.MaximumBytesPerInterval);
            }
        }

        else {
            LOG_MSG("%s %s Ep0x%02X Maximum Packet Size:%d\n",
                    EndpointTypeDisplayString[ENDPOINT_TYPE(transferParam)],
                    TRANSFER_DISPLAY(transferParam, "Read", "Write"), transferParam->Ep.PipeId,
                    transferParam->Ep.MaximumPacketSize);
        }
    }

    else {
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

void FileIOOpen(PUVPERF_PARAM TestParms) {
    strncpy(TestParms->ReadFileName, "uvperf_read.dat", MAX_PATH - 1);
    TestParms->ReadFileName[MAX_PATH - 1] = '\0';
    strncpy(TestParms->WriteFileName, "uvperf_write.dat", MAX_PATH - 1);
    TestParms->WriteFileName[MAX_PATH - 1] = '\0';

    if (TestParms->fileIO) {
        if (TestParms->TestType & TestTypeRead) {
            TestParms->ReadFile =
                CreateFile(TestParms->ReadFileName, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_ALWAYS, // Open the file if it exists; otherwise, create it
                           FILE_ATTRIBUTE_NORMAL, NULL);

            if (TestParms->ReadFile == INVALID_HANDLE_VALUE) {
                LOG_ERROR("failed creating %s\n", TestParms->ReadFileName);
                TestParms->fileIO = FALSE;
            }
        }

        if (TestParms->TestType & TestTypeWrite) {
            TestParms->WriteFile =
                CreateFile(TestParms->WriteFileName, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, // Open the file if it exists; otherwise, create it
                           FILE_ATTRIBUTE_NORMAL, NULL);

            if (TestParms->WriteFile == INVALID_HANDLE_VALUE) {
                LOG_ERROR("failed opening %s\n", TestParms->WriteFileName);
                TestParms->fileIO = FALSE;
            }
        }
    }
}

void FileIORead(PUVPERF_PARAM TestParms, PUVPERF_TRANSFER_PARAM transferParam) {
    DWORD bytesRead;
    if (TestParms->fileIO && TestParms->ReadFile != INVALID_HANDLE_VALUE) {
        if (!ReadFile(TestParms->ReadFile, transferParam->Buffer, TestParms->readlenth, &bytesRead,
                      NULL)) {
            LOG_ERROR("failed reading %s\n", TestParms->ReadFileName);
        }
    }
}

void FileIOWrite(PUVPERF_PARAM TestParms, PUVPERF_TRANSFER_PARAM transferParam) {
    DWORD bytesWritten;
    if (TestParms->fileIO && TestParms->WriteFile != INVALID_HANDLE_VALUE) {
        if (!WriteFile(TestParms->WriteFile, transferParam->Buffer, TestParms->writelength,
                       &bytesWritten, NULL)) {
            DWORD errorCode = GetLastError(); // Retrieve the error code
            LOG_ERROR("failed writing %s, Error code: %lu\n", TestParms->WriteFileName, errorCode);
        }
    }
}

void FileIOClose(PUVPERF_PARAM TestParms) {
    if (TestParms->fileIO) {
        if (TestParms->ReadFile != INVALID_HANDLE_VALUE) {
            CloseHandle(TestParms->ReadFile);
            TestParms->ReadFile = INVALID_HANDLE_VALUE;
        }

        if (TestParms->WriteFile != INVALID_HANDLE_VALUE) {
            CloseHandle(TestParms->WriteFile);
            TestParms->WriteFile = INVALID_HANDLE_VALUE;
        }
    }
}

int main(int argc, char **argv) {
    UVPERF_PARAM TestParms;
    PUVPERF_TRANSFER_PARAM ReadTest = NULL;
    PUVPERF_TRANSFER_PARAM WriteTest = NULL;
    int key;
    long ec;
    unsigned int count;
    UCHAR bIsoAsap;

    if (argc == 1) {
        LOG_VERBOSE("No arguments\n");
        ShowUsage();
        return -1;
    }

    LOG_VERBOSE("SetParamsDefaults\n");
    SetParamsDefaults(&TestParms);

    LOG_VERBOSE("ParseArgs\n");
    if (ParseArgs(&TestParms, argc, argv) < 0)
        return -1;

    LOG_VERBOSE("InitializeCriticalSection\n");
    InitializeCriticalSection(&DisplayCriticalSection);

    LOG_VERBOSE("LibusbK device List Initialize\n");
    if (!LstK_Init(&TestParms.DeviceList, 0)) {
        ec = GetLastError();
        char *buffer = GetWinErrorMessage(ec);
        LOG_ERROR("Failed to initialize device list ec=%08Xh, message %s: \n", ec, buffer);
        FreeWinErrorMessage(buffer);
        goto Done;
    }

    count = 0;
    LstK_Count(TestParms.DeviceList, &count);
    if (count == 0) {
        LOGERR0("No devices found\n");
        goto Done;
    }

    // todo : make a flag for this
    LOG_VERBOSE("GetDeviceInfoFromList\n");
    if (TestParms.intf == -1 || TestParms.altf == -1 || TestParms.endpoint == 0x00) {
        if (GetDeviceInfoFromList(&TestParms) < 0) {
            goto Done;
        }

        fprintf(stderr, "select Read or Write or Loop\n");
        fprintf(stderr, "R - Read\n");
        fprintf(stderr, "W - Write\n");
        fprintf(stderr, "L - Loop\n");
        fprintf(stderr, "Selection: ");

        int validInput = 0; // Flag to check for valid input
        do {
            key = _getch(); // Read character without echoing
            switch (key) {
            case 'Q':
            case 'q':
                return 0;
            case 'R':
            case 'r':
                TestParms.TestType = TestTypeRead;
                validInput = 1; // Set flag to valid
                break;
            case 'W':
            case 'w':
                TestParms.TestType = TestTypeWrite;
                validInput = 1; // Set flag to valid
                break;
            case 'L':
            case 'l':
                TestParms.TestType = TestTypeLoop;
                validInput = 1; // Set flag to valid
                break;
            default:
                fprintf(stderr, "Invalid selection. Please choose again\n");
                fprintf(stderr, "Q - Quit\n");
                fprintf(stderr, "R - Read\n");
                fprintf(stderr, "W - Write\n");
                fprintf(stderr, "L - Loop\n");
                fprintf(stderr, "Selection: ");
                break;
            }
        } while (!validInput); // Continue until a valid input is entered
    } else {
        LOG_VERBOSE("GetDeviceParam\n");
        if (GetDeviceParam(&TestParms) < 0) {
            goto Done;
        }
    }

    LOG_VERBOSE("Open Bench\n");
    if (!Bench_Open(&TestParms))
        goto Done;

    if (TestParms.TestType & TestTypeRead) {
        LOG_VERBOSE("CreateTransferParam for ReadTest\n");
        ReadTest =
            CreateTransferParam(&TestParms, TestParms.endpoint | USB_ENDPOINT_DIRECTION_MASK);
        if (!ReadTest)
            goto Done;
        if (TestParms.UseRawIO != 0xFF) {
            if (!K.SetPipePolicy(TestParms.InterfaceHandle, ReadTest->Ep.PipeId, RAW_IO, 1,
                                 &TestParms.UseRawIO)) {
                ec = GetLastError();
                char *buffer = GetWinErrorMessage(ec);
                LOG_ERROR("SetPipePolicy:RAW_IO failed. ErrorCode=%08Xh message : %s\n", ec,
                          buffer);
                FreeWinErrorMessage(buffer);
                goto Done;
            }
        }
    }

    if (TestParms.TestType & TestTypeWrite) {
        LOG_VERBOSE("CreateTransferParam for WriteTest\n");
        WriteTest = CreateTransferParam(&TestParms, TestParms.endpoint & 0x0F);
        if (!WriteTest)
            goto Done;
        if (TestParms.fixedIsoPackets) {
            if (!K.SetPipePolicy(TestParms.InterfaceHandle, WriteTest->Ep.PipeId,
                                 ISO_NUM_FIXED_PACKETS, 2, &TestParms.fixedIsoPackets)) {
                ec = GetLastError();
                char *buffer = GetWinErrorMessage(ec);
                LOG_ERROR(
                    "SetPipePolicy:ISO_NUM_FIXED_PACKETS failed. ErrorCode=0x%08X, message : %s\n",
                    ec, buffer);
                FreeWinErrorMessage(buffer);
                goto Done;
            }
        }
        if (TestParms.UseRawIO != 0xFF) {
            if (!K.SetPipePolicy(TestParms.InterfaceHandle, WriteTest->Ep.PipeId, RAW_IO, 1,
                                 &TestParms.UseRawIO)) {
                ec = GetLastError();
                char *buffer = GetWinErrorMessage(ec);
                LOG_ERROR("SetPipePolicy:RAW_IO failed. ErrorCode=0x%08X\n", ec, buffer);
                FreeWinErrorMessage(buffer);
                goto Done;
            }
        }
    }

    if (TestParms.verify) {
        if (ReadTest && WriteTest) {
            LOG_VERBOSE("CreateVerifyBuffer for WriteTest\n");
            if (CreateVerifyBuffer(&TestParms, WriteTest->Ep.MaximumPacketSize) < 0)
                goto Done;
        } else if (ReadTest) {
            LOG_VERBOSE("CreateVerifyBuffer for ReadTest\n");
            if (CreateVerifyBuffer(&TestParms, ReadTest->Ep.MaximumPacketSize) < 0)
                goto Done;
        }
    }

    if ((WriteTest && WriteTest->Ep.PipeType == UsbdPipeTypeIsochronous) ||
        (ReadTest && ReadTest->Ep.PipeType == UsbdPipeTypeIsochronous)) {
        UINT frameNumber;
        LOG_VERBOSE("GetCurrentFrameNumber\n");
        if (!K.GetCurrentFrameNumber(TestParms.InterfaceHandle, &frameNumber)) {
            ec = GetLastError();
            char *buffer = GetWinErrorMessage(ec);
            LOG_ERROR("GetCurrentFrameNumber Failed. ErrorCode=%u, message : %s", ec, buffer);
            FreeWinErrorMessage(buffer);
            goto Done;
        }
        frameNumber += TestParms.bufferCount * 2;
        if (WriteTest) {
            WriteTest->frameNumber = frameNumber;
            frameNumber++;
        }
        if (ReadTest) {
            ReadTest->frameNumber = frameNumber;
            frameNumber++;
        }
    }

    LOG_VERBOSE("ShowParms\n");
    ShowParms(&TestParms);
    if (ReadTest)
        ShowTransfer(ReadTest);
    if (WriteTest)
        ShowTransfer(WriteTest);

    bIsoAsap = (UCHAR)TestParms.UseIsoAsap;
    if (ReadTest)
        K.SetPipePolicy(TestParms.InterfaceHandle, ReadTest->Ep.PipeId, ISO_ALWAYS_START_ASAP, 1,
                        &bIsoAsap);
    if (WriteTest)
        K.SetPipePolicy(TestParms.InterfaceHandle, WriteTest->Ep.PipeId, ISO_ALWAYS_START_ASAP, 1,
                        &bIsoAsap);

    FileIOOpen(&TestParms);

    if (ReadTest) {
        LOG_VERBOSE("ResumeThread for ReadTest\n");
        SetThreadPriority(ReadTest->ThreadHandle, TestParms.priority);
        ResumeThread(ReadTest->ThreadHandle);
    }

    if (WriteTest) {
        LOG_VERBOSE("ResumeThread for WriteTest\n");
        SetThreadPriority(WriteTest->ThreadHandle, TestParms.priority);
        ResumeThread(WriteTest->ThreadHandle);
    }

    LOGMSG0("Press 'Q' to abort\n");

    while (!TestParms.isCancelled) {
        Sleep(TestParms.refresh);
        if (_kbhit()) {
            // A key was pressed.
            key = _getch();
            switch (key) {
            case 'Q':
            case 'q':
                LOG_VERBOSE("User Aborted\n");
                TestParms.isUserAborted = TRUE;
                TestParms.isCancelled = TRUE;
            }

            if ((ReadTest) && !ReadTest->isRunning) {
                LOG_VERBOSE("ReadTest is not running\n");
                TestParms.isCancelled = TRUE;
                break;
            }

            if ((WriteTest) && !WriteTest->isRunning) {
                LOG_VERBOSE("WriteTest is not running\n");
                TestParms.isCancelled = TRUE;
                break;
            }
        }

        // if (ReadTest && ReadTest->LastTick.tv_sec - ReadTest->StartTick.tv_sec >= 300.0) {
        //     LOG_VERBOSE("Over 60 seconds\n");
        //     DWORD elapsedSeconds =
        //         ((ReadTest->LastTick.tv_sec - ReadTest->StartTick.tv_sec) +
        //          (ReadTest->LastTick.tv_nsec - ReadTest->StartTick.tv_nsec) / 100000000.0);
        //     LOG_MSG("Elapsed Time %.2f  seconds\n", elapsedSeconds);
        //     TestParms.isUserAborted = TRUE;
        //     TestParms.isCancelled = TRUE;
        // }

        // if (WriteTest && WriteTest->LastTick.tv_sec - WriteTest->StartTick.tv_sec >= 300.0) {
        //     LOG_VERBOSE("Over 60 seconds\n");
        //     DWORD elapsedSeconds =
        //         ((ReadTest->LastTick.tv_sec - ReadTest->StartTick.tv_sec) +
        //          (ReadTest->LastTick.tv_nsec - ReadTest->StartTick.tv_nsec) / 100000000.0);
        //     LOG_MSG("Elapsed Time %.2f  seconds\n", elapsedSeconds);
        //     TestParms.isUserAborted = TRUE;
        //     TestParms.isCancelled = TRUE;
        // }

        if (ReadTest && TestParms.fileIO)
            FileIORead(&TestParms, ReadTest);
        if (WriteTest && TestParms.fileIO)
            FileIOWrite(&TestParms, WriteTest);

        LOG_VERBOSE("ShowRunningStatus\n");
        ShowRunningStatus(ReadTest, WriteTest);
        // Only one key at a time.
        while (_kbhit())
            _getch();
    }

    FileIOClose(&TestParms);

    LOG_VERBOSE("WaitForTestTransfer\n");
    WaitForTestTransfer(ReadTest, 1000);
    if ((ReadTest) && ReadTest->isRunning) {
        LOG_WARNING("Aborting Read Pipe 0x%02X..", ReadTest->Ep.PipeId);
        K.AbortPipe(TestParms.InterfaceHandle, ReadTest->Ep.PipeId);
    }

    WaitForTestTransfer(WriteTest, 1000);
    if ((WriteTest) && WriteTest->isRunning) {
        LOG_WARNING("Aborting Write Pipe 0x%02X..", WriteTest->Ep.PipeId);
        K.AbortPipe(TestParms.InterfaceHandle, WriteTest->Ep.PipeId);
    }

    if ((ReadTest) && ReadTest->isRunning)
        WaitForTestTransfer(ReadTest, INFINITE);
    if ((WriteTest) && WriteTest->isRunning)
        WaitForTestTransfer(WriteTest, INFINITE);

    LOG_VERBOSE("Show Transfer\n");
    if (ReadTest)
        ShowTransfer(ReadTest);
    if (WriteTest)
        ShowTransfer(WriteTest);

Done:
    LOG_VERBOSE("Free TransferParam\n");
    if (TestParms.InterfaceHandle) {
        LOG_VERBOSE("ResetPipe\n");
        K.SetAltInterface(TestParms.InterfaceHandle, TestParms.InterfaceDescriptor.bInterfaceNumber,
                          FALSE, TestParms.defaultAltSetting);
        K.Free(TestParms.InterfaceHandle);

        TestParms.InterfaceHandle = NULL;
    }

    LOG_VERBOSE("Close Handle\n");
    if (!TestParms.use_UsbK_Init) {
        if (TestParms.DeviceHandle) {
            CloseHandle(TestParms.DeviceHandle);
            TestParms.DeviceHandle = NULL;
        }
    }

    LOG_VERBOSE("Close Bench\n");
    if (TestParms.VerifyBuffer) {
        PUVPERF_BUFFER verifyBuffer, verifyListTemp;

        free(TestParms.VerifyBuffer);
        TestParms.VerifyBuffer = NULL;

        // DL_FOREACH_SAFE(TestParms.VerifyList, verifyBuffer, verifyListTemp) {
        //     DL_DELETE(TestParms.VerifyList, verifyBuffer);
        //     free(verifyBuffer);
        // }
    }

    LOG_VERBOSE("Free TransferParam\n");
    LstK_Free(TestParms.DeviceList);
    FreeTransferParam(&ReadTest);
    FreeTransferParam(&WriteTest);

    DeleteCriticalSection(&DisplayCriticalSection);

    if (!TestParms.listDevicesOnly) {
        LOGMSG0("Press any key to exit\n");
        _getch();
        LOGMSG0("\n");
    }

    return 0;
}
