/*!********************************************************************
*   uvperf.c
*   This is a simple utility to test the performance of USB transfers.
*   It is designed to be used with the libusbK driver.
*   The utility will perform a series of transfers to the specified endpoint
*   and report the results.
*
*   Usage:
*   uvperf -vVID -pPID -iINTERFACE -aAltInterface -eENDPOINT -tTRANSFER -oTIMEOUT -rlLENGTH -wlLENGTH -rREPEAT
*
*   -vVID           USB Vendor ID
*   -pPID           USB Product ID
*   -iINTERFACE     USB Interface
*   -aAltInterface  USB Alternate Interface
*   -eENDPOINT      USB Endpoint
*   -tTRANSFER      0 = isochronous, 1 = bulk
*   -oTIMEOUT       USB Transfer Timeout
*   -rlLENGTH       Length of read transfers
*   -wlLENGTH       Length of write transfers
*   -rREPEAT        Number of transfers to perform
*
*   Example:
*   uvperf -v0x1004 -p0xa000 -i0 -a0 -e0x81 -t1 -o1000 -l1024 -r1000 -x1
*
*   This will perform 1000 bulk transfers of 1024 bytes to endpoint 0x81
*   on interface 0, alternate setting 0 of a device with VID 0x1004 and PID 0xA000.
*   The transfers will have a timeout of 1000ms.
*
********************************************************************!*/
// #include <window.h>
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <wtypes.h>
#include <stdarg.h>

#include "libusbk.h"
#include "lusbk_shared.h"
#include "lusbk_linked_list.h"

#define MAX_OUTSTANDING_TRANSFERS 10

#pragma warning(disable: 4127)
#pragma warning(disable: 4996)
#pragma comment(lib, "libusbK.lib")

// #define LOG(LogTypeString, format, ...) printf("%s[" __FUNCTION__"] "format, LogTypeString, ##__VA_ARGS__)
#define LOG(LogTypeString, format, ...) printf("%s[%s] " format, LogTypeString, __FUNCTION__, __VA_ARGS__)
#define LOG_NO_FN(LogTypeString, format, ...) printf("%s" format "%s", LogTypeString, ##__VA_ARGS__,"")

#define LOG_ERROR(format, ...) LOG("ERROR: ", format, ##__VA_ARGS__)
#define LOG_WARNING(format, ...) LOG("WARNING: ", format, ##__VA_ARGS__)
#define LOG_MSG(format, ...) LOG_NO_FN("", format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) LOG_NO_FN("", format, ##__VA_ARGS__)

#define LOGERR0(message) LOG_ERROR("%s\n", message)
#define LOGWAR0(message) LOG_WARNING("%s\n", message)
#define LOGMSG0(message) LOG_MSG("%s\n", message)
#define LOGDBG0(message) LOG_DEBUG("%s\n", message)

#define VerifyListLock(mTest) while(InterlockedExchange(&((mTest)->verifyLock),1) != 0) Sleep(0)
#define VerifyListUnlock(mTest) InterlockedExchange(&((mTest)->verifyLock),0)

static LPCSTR DrvIdNames[8] = { "libusbK", "libusb0", "WinUSB", "libusb0 filter", "Unknown", "Unknown", "Unknown" };
#define GetDrvIdString(DriverID)	(DrvIdNames[((((LONG)(DriverID))<0) || ((LONG)(DriverID)) >= KUSB_DRVID_COUNT)?KUSB_DRVID_COUNT:(DriverID)])

KUSB_DRIVER_API K;
CRITICAL_SECTION DisplayCriticalSection;

typedef struct _BENCHMARK_BUFFER {
    PUCHAR Data;
    LONG            dataLenth;
    LONG            syncFailed;

    struct _BENCHMARK_BUFFER* next;
    struct _BENCHMARK_BUFFER* prev;
} BENCHMARK_BUFFER, * PBENCHMARK_BUFFER;

typedef enum _BENCHMARK_DEVICE_TEST_TYPE {
    TestTypeNone = 0x00,
    TestTypeRead = 0x01,
    TestTypeWrite = 0x02,
    TestTypeLoop = TestTypeRead | TestTypeWrite,
} BENCHMARK_DEVICE_TEST_TYPE, * PBENCHMARK_DEVICE_TEST_TYPE;

typedef enum _BENCHMARK_TRANSFER_MODE {
    TRANSFER_MODE_SYNC,
    TRANSFER_MODE_ASYNC,
} BENCHMARK_TRANSFER_MODE;

typedef struct _PARAM {
    int vid;
    int pid;
    int intf;
    int altf;
    int endpoint;
    int transfer;
    int timeout;
    int retry;
    int bufferlength;
    int readlenth;
    int writelength;
    int bufferCount;
    int repeat;
    int fixedIsoPackets;
    int priority;
    BOOL useList;
    BOOL verify;
    BOOL verifyDetails;
    enum BENCHMARK_DEVICE_TEST_TYPE TestType;
    enum BENCHMARK_TRANSFER_MODE TransferMode;

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
    BENCHMARK_BUFFER* VerifyList;

    unsigned char verifyBuffer;
    unsigned short verifyBufferSize;
    BOOL use_UsbK_Init;
    BOOL listDevicesOnly;
    unsigned long deviceSpeed;

    BOOL ReadLogEnabled;
    FILE* ReadLogFile;
    BOOL WriteLogEnabled;
    FILE* WriteLogFile;

    UCHAR UseRawIO;
    UCHAR DefaultAltSetting;
    BOOL UseIsoAsap;
    BYTE* VerifyBuffer;

    unsigned char defaultAltSetting;
} PARAM, * PPARAM;

typedef struct _BENCHMARK_ISOCH_RESULTS
{
    UINT GoodPackets;
    UINT BadPackets;
    UINT Length;
    UINT TotalPackets;
}BENCHMARK_ISOCH_RESULTS;

typedef struct _BENCHMARK_TRANSFER_HANDLE
{
    KISOCH_HANDLE IsochHandle;
    OVERLAPPED Overlapped;
    BOOL InUse;
    PUCHAR Data;
    INT DataMaxLength;
    INT ReturnCode;
    BENCHMARK_ISOCH_RESULTS IsochResults;
} BENCHMARK_TRANSFER_HANDLE, * PBENCHMARK_TRANSFER_HANDLE;

typedef struct _BENCHMARK_TRANSFER_PARAM {
    PPARAM TestParms;
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
	DWORD StartTick;
	DWORD LastTick;
	DWORD LastStartTick;

    int shortTrasnferred;

    int TotalTimeoutCount;
    int RunningTimeoutCount;

    int TotalErrorCount;
    int RunningErrorCount;

    BENCHMARK_TRANSFER_HANDLE TransferHandles[MAX_OUTSTANDING_TRANSFERS];
    BENCHMARK_ISOCH_RESULTS IsochResults;

    unsigned char Buffer[0];
} BENCHMARK_TRANSFER_PARAM, * PBENCHMARK_TRANSFER_PARAM;

#include <pshpack1.h>
typedef struct _KBENCH_CONTEXT_LSTK
{
    BYTE Selected;
} KBENCH_CONTEXT_LSTK, * PKBENCH_CONTEXT_LSTK;
#include <poppack.h>

BOOL Bench_Open(__in PPARAM TestParms);


void ShowUsage();
void SetParamsDefaults(PPARAM TestParms);
int GetParamsDevice(PPARAM TestParms);

int ParseArgs(PPARAM TestParms, int argc, char** argv);
void ShowParms(PPARAM TestParms);
PBENCHMARK_TRANSFER_PARAM CreateTransferParam(PPARAM TestParms, int endpointID);
void GetAverageBytesSec(PBENCHMARK_TRANSFER_PARAM transferParam, DOUBLE* bps);
void GetCurrentBytesSec(PBENCHMARK_TRANSFER_PARAM transferParam, DOUBLE* bps);
DWORD TransferThread(PBENCHMARK_TRANSFER_PARAM transferParam);
void FreeTransferParam(PBENCHMARK_TRANSFER_PARAM* transferParamRef);

#define TRANSFER_DISPLAY(TransferParam, ReadingString, WritingString) \
	((TransferParam->Ep.PipeId & USB_ENDPOINT_DIRECTION_MASK) ? ReadingString : WritingString)

#define ENDPOINT_TYPE(TransferParam) (TransferParam->Ep.PipeType & 3)
const char* TestDisplayString[] = { "None", "Read", "Write", "Loop", NULL };
const char* EndpointTypeDisplayString[] = { "Control", "Isochronous", "Bulk", "Interrupt", NULL };

LONG WinError(__in_opt DWORD errorCode) {
    LPSTR buffer = NULL;

    errorCode = errorCode ? labs(errorCode) : GetLastError();
    if (!errorCode) return errorCode;

    if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
        NULL, errorCode, 0, (LPSTR)&buffer, 0, NULL) > 0)
    {
        LOG_ERROR("%s\n", buffer);
        SetLastError(0);
    }
    else {
        LOGERR0("FormatMessage error!\n");
    }

    if (buffer)
        LocalFree(buffer);

    return -labs(errorCode);
}

void AppendLoopBuffer(
    PPARAM TestParms,
    unsigned char* buffer,
    unsigned int length)
{
    if (TestParms->verify && TestParms->TestType == TestTypeLoop) {
        BENCHMARK_BUFFER* newVerifyBuf = malloc(
            sizeof(BENCHMARK_BUFFER) + length
        );

        memset(newVerifyBuf, 0, sizeof(BENCHMARK_BUFFER));

        newVerifyBuf->Data = (unsigned char*)newVerifyBuf
            + sizeof(BENCHMARK_BUFFER);
        newVerifyBuf->dataLenth = length;
        memcpy(newVerifyBuf->Data, buffer, length);

        VerifyListLock(TestParms);
        DL_APPEND(TestParms->VerifyList, newVerifyBuf);
        VerifyListUnlock(TestParms);
    }
}


BOOL Bench_Open(__in PPARAM TestParams) {
    UCHAR altSetting;
    KUSB_HANDLE associatedHandle;
    UINT transferred;
    KLST_DEVINFO_HANDLE deviceInfo;

    TestParams->SelectedDeviceProfile = NULL;

    LstK_MoveReset(TestParams->DeviceList);

    while (LstK_MoveNext(TestParams->DeviceList, &deviceInfo)) {
        // enabled
        UINT userContext = (UINT)LibK_GetContext(deviceInfo, KLIB_HANDLE_TYPE_LSTINFOK);
        if (userContext != TRUE) continue;

        if (!LibK_LoadDriverAPI(&K, deviceInfo->DriverID)) {
            WinError(0);
            LOG_WARNING("could not load driver api %s.\n", GetDrvIdString(deviceInfo->DriverID));
            continue;
        }
        if (!TestParams->use_UsbK_Init) {
            TestParams->DeviceHandle = CreateFileA(deviceInfo->DevicePath,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,
                NULL);

            if (!TestParams->DeviceHandle || TestParams->DeviceHandle == INVALID_HANDLE_VALUE) {
                WinError(0);
                TestParams->DeviceHandle = NULL;
                LOG_WARNING("could not create device handle.\n%s\n", deviceInfo->DevicePath);
                continue;
            }

            if (!K.Initialize(TestParams->DeviceHandle, &TestParams->InterfaceHandle)) {
                WinError(0);
                CloseHandle(TestParams->DeviceHandle);
                TestParams->DeviceHandle = NULL;
                TestParams->InterfaceHandle = NULL;
                LOG_WARNING("could not initialize device.\n%s\n", deviceInfo->DevicePath);
                continue;
            }
        }
        else {
            if (!K.Init(&TestParams->InterfaceHandle, deviceInfo)) {
                WinError(0);
                TestParams->DeviceHandle = NULL;
                TestParams->InterfaceHandle = NULL;
                LOG_WARNING("could not open device.\n%s\n", deviceInfo->DevicePath);
                continue;
            }

        }

        if (!K.GetDescriptor(TestParams->InterfaceHandle,
            USB_DESCRIPTOR_TYPE_DEVICE,
            0, 0,
            (PUCHAR)&TestParams->DeviceDescriptor,
            sizeof(TestParams->DeviceDescriptor),
            &transferred))
        {
            WinError(0);

            K.Free(TestParams->InterfaceHandle);
            TestParams->InterfaceHandle = NULL;

            if (!TestParams->use_UsbK_Init) {
                CloseHandle(TestParams->DeviceHandle);
                TestParams->DeviceHandle = NULL;
            }

            LOG_WARNING("could not get device descriptor.\n%s\n", deviceInfo->DevicePath);
            continue;
        }
        TestParams->vid = (int)TestParams->DeviceDescriptor.idVendor;
        TestParams->pid = (int)TestParams->DeviceDescriptor.idProduct;

    NextInterface:

        // While searching for hardware specifics we are also gathering information and storing it
        // in our test.
        memset(&TestParams->InterfaceDescriptor, 0, sizeof(TestParams->InterfaceDescriptor));
        altSetting = 0;

        while (K.QueryInterfaceSettings(TestParams->InterfaceHandle, altSetting, &TestParams->InterfaceDescriptor)) {
            // found an interface
            UCHAR pipeIndex = 0;
            int hasIsoEndpoints = 0;
            int hasZeroMaxPacketEndpoints = 0;

            memset(&TestParams->PipeInformation, 0, sizeof(TestParams->PipeInformation));
            while (K.QueryPipeEx(TestParams->InterfaceHandle, altSetting, pipeIndex, &TestParams->PipeInformation[pipeIndex])) {
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
                if (((TestParams->intf == -1) || (TestParams->intf == TestParams->InterfaceDescriptor.bInterfaceNumber)) &&
                    ((TestParams->altf == -1) || (TestParams->altf == TestParams->InterfaceDescriptor.bAlternateSetting)))
                {
                    // if the user actually specifies an alt iso setting with zero MaxPacketEndpoints
                    // we let let them.
                    if (TestParams->altf == -1 && hasIsoEndpoints && hasZeroMaxPacketEndpoints) {
                        // user didn't specfiy and we know we can't tranfer with this alt setting so skip it.
                        LOG_MSG("skipping interface %02X:%02X. zero-length iso endpoints exist.\n",
                            TestParams->InterfaceDescriptor.bInterfaceNumber,
                            TestParams->InterfaceDescriptor.bAlternateSetting);
                    }
                    else {
                        // this is the one we are looking for.
                        TestParams->intf = TestParams->InterfaceDescriptor.bInterfaceNumber;
                        TestParams->altf = TestParams->InterfaceDescriptor.bAlternateSetting;
                        TestParams->SelectedDeviceProfile = deviceInfo;

                        // some buffering is required for iso.
                        if (hasIsoEndpoints && TestParams->bufferCount == 1)
                            TestParams->bufferCount++;

                        TestParams->DefaultAltSetting = 0;
                        K.GetCurrentAlternateSetting(TestParams->InterfaceHandle, &TestParams->DefaultAltSetting);
                        if (!K.SetCurrentAlternateSetting(TestParams->InterfaceHandle, TestParams->InterfaceDescriptor.bAlternateSetting))
                        {
                            LOG_ERROR("failed selecting alternate setting %02Xh\n", TestParams->altf);
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

    LOG_ERROR("device interface/alt interface not found (%02Xh/%02Xh).\n", TestParams->intf, TestParams->altf);
    return FALSE;
}

int TransferSync(PBENCHMARK_TRANSFER_PARAM transferParam) {
    unsigned int trasnferred;
    BOOL success;

    if (transferParam->Ep.PipeType & USB_ENDPOINT_DIRECTION_MASK) {
        success = K.ReadPipe(
            transferParam->TestParms->InterfaceHandle,
            transferParam->Ep.PipeId,
            transferParam->Buffer,
            transferParam->TestParms->readlenth,
            &trasnferred, NULL
        );
    }
    else {
        AppendLoopBuffer(
            transferParam->TestParms,
            transferParam->Buffer,
            transferParam->TestParms->writelength
        );
        success = K.WritePipe(
            transferParam->TestParms->InterfaceHandle,
            transferParam->Ep.PipeId,
            transferParam->Buffer,
            transferParam->TestParms->writelength,
            &trasnferred,
            NULL
        );
    }

    return success ? (int)trasnferred : -labs(GetLastError());
}

//TODO : IsoTransferCb
BOOL WINAPI IsoTransferCb(
    _in unsigned int packetIndex,
    _ref unsigned int* offset,
    _ref unsigned int* length,
    _ref unsigned int* status,
    _in void* userState
) {
    return TRUE;
}

//TODO : TransferAsync
int TransferAsync(PBENCHMARK_TRANSFER_PARAM transferParam) {
    return 0;
}

void VerifyLoopData() {
    return;
}

void ShowUsage(){
    LOG_MSG("Usage: uvperf -vVID -pPID -iINTERFACE -aAltInterface -eENDPOINT -tTRANSFER -oTIMEOUT -rlLENGTH -wlLENGTH -rREPEAT\n");
    LOG_MSG("\t-vVID           USB Vendor ID\n");
    LOG_MSG("\t-pPID           USB Product ID\n");
    LOG_MSG("\t-iINTERFACE     USB Interface\n");
    LOG_MSG("\t-aAltInterface  USB Alternate Interface\n");
    LOG_MSG("\t-eENDPOINT      USB Endpoint\n");
    LOG_MSG("\t-tTRANSFER      0 = isochronous, 1 = bulk\n");
    LOG_MSG("\t-oTIMEOUT       USB Transfer Timeout\n");
    LOG_MSG("\t-rlLENGTH       Length of read transfers\n");
    LOG_MSG("\t-wlLENGTH       Length of write transfers\n");
    LOG_MSG("\t-rREPEAT        Number of transfers to perform\n");
    LOG_MSG("\n");
    LOG_MSG("Example:\n");
    LOG_MSG("uvperf -v0x1004 -p0xa000 -i0 -a0 -e0x81 -t1 -o1000 -l1024 -r1000 -x1\n");
    LOG_MSG("This will perform 1000 bulk transfers of 1024 bytes to endpoint 0x81\n");
    LOG_MSG("on interface 0, alternate setting 0 of a device with VID 0x1004 and PID 0xA000.\n");
    LOG_MSG("The transfers will have a timeout of 1000ms.\n");
}

void SetParamsDefaults(PPARAM TestParms) {
    memset(TestParms, 0, sizeof(*TestParms));

    TestParms->vid = 0x1004;
    TestParms->pid = 0xA000;
    TestParms->intf = -1;
    TestParms->altf = -1;
    TestParms->endpoint = 0x81;
    TestParms->transfer = 1;
    TestParms->timeout = 3000;
    TestParms->bufferlength = 1024;
    TestParms->readlenth = TestParms->bufferlength;
    TestParms->writelength = TestParms->bufferlength;
}

int GetParamsDevice(PPARAM TestParms) {
    char id[MAX_PATH];
    KLST_DEVINFO_HANDLE deviceInfo = NULL;

    LstK_MoveReset(TestParms->DeviceList);

    while (LstK_MoveNext(TestParms->DeviceList, &deviceInfo))
    {
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

        if (TestParms->vid == vid && TestParms->pid == pid)
        {
            // enabled
            LibK_SetContext(deviceInfo, KLIB_HANDLE_TYPE_LSTINFOK, (KLIB_USER_CONTEXT)TRUE);
        }
    }

    return ERROR_SUCCESS;
}

int ParseArgs(PPARAM TestParms, int argc, char** argv) {
    int i;
    int arg;
    char* temp;
    int value;
    int status = 0;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            arg = argv[i][1];
            temp = argv[i]+2;
            value = strtol(temp, NULL, 0);
            switch (arg) {
            case 'v':
                TestParms->vid = value;
                break;
            case 'p':
                TestParms->pid = value;
                break;
            case 'i':
                TestParms->intf = value;
                break;
            case 'a':
                TestParms->altf = value;
                break;
            case 'e':
                TestParms->endpoint = value;
                break;
            case 't':
                TestParms->transfer = value;
                break;
            case 'o':
                TestParms->timeout = value;
                break;
            case 'r':
                TestParms->repeat = value;
                break;
            case 'l':
                TestParms->readlenth = value;
                break;
            case 'w':
                TestParms->writelength = value;
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
    }
    return status;
}

void ShowParms(PPARAM TestParms) {
    if (!TestParms)
        return;

    // LOG_MSG("%s Test Parameters:\n", );
    LOG_MSG("\tVID:           :  %04X\n", TestParms->vid);
    LOG_MSG("\tPID:           :  %04X\n", TestParms->pid);
    LOG_MSG("\tInterface:     :  %d\n", TestParms->intf);
    LOG_MSG("\tAlt Interface: :  %d\n", TestParms->altf);
    LOG_MSG("\tEndpoint:      :  %02X\n", TestParms->endpoint);
    LOG_MSG("\tTransfer:      :  %d\n", TestParms->transfer);
    LOG_MSG("\tTimeout:       :  %d\n", TestParms->timeout);
    LOG_MSG("\tRead Length:   :  %d\n", TestParms->readlenth);
    LOG_MSG("\tWrite Length:  :  %d\n", TestParms->writelength);
    LOG_MSG("\tRepeat:        :  %d\n", TestParms->repeat);
    LOG_MSG("\n");
}

DWORD TransferThread(PBENCHMARK_TRANSFER_PARAM transferParam) {
    int ret, i;
    PBENCHMARK_TRANSFER_HANDLE handle;
    unsigned char* buffer;

    transferParam->isRunning = TRUE;

    while (!transferParam->TestParms->isCancelled) {
        buffer = NULL;
        handle = NULL;

        if (transferParam->TestParms->TransferMode == TransferSync) {
            ret = TransferSync(transferParam);
            if (ret >= 0)
                buffer = transferParam->Buffer;
        }
        else if (transferParam->TestParms->TransferMode == TransferAsync) {
            ret = TransferAsync(transferParam);
            if ((handle) && ret >= 0)
                buffer = transferParam->Buffer;
        }
        else {
            LOG_ERROR("Invalid transfer mode %d\n",
                transferParam->TestParms->TransferMode);
            goto Done;
        }

        if (transferParam->TestParms->verify &&
            transferParam->TestParms->VerifyList &&
            transferParam->TestParms->TestType == TestTypeLoop &&
            USB_ENDPOINT_DIRECTION_IN(transferParam->Ep.PipeId) && ret > 0)
        {
            VerifyLoopData(transferParam->TestParms, buffer);
        }

        if (ret < 0) {
            // user pressed 'Q' or 'ctrl+c'
            if (transferParam->TestParms->isUserAborted)
                break;

            // timeout
            if (ret == ERROR_SEM_TIMEOUT
                || ret == ERROR_OPERATION_ABORTED
                || ret == ERROR_CANCELLED)
            {
                transferParam->TotalTimeoutCount++;
                transferParam->RunningTimeoutCount++;
                LOG_ERROR("Timeout #%d %s on EP%02Xh.. \n",
                    transferParam->RunningTimeoutCount,
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
                    transferParam->RunningErrorCount,
                    transferParam->TestParms->retry + 1,
                    ret);

                K.ResetPipe(transferParam->TestParms->InterfaceHandle, transferParam->Ep.PipeId);

                if (transferParam->RunningErrorCount > transferParam->TestParms->retry)
                    break;
            }

            ret = 0;
        }
        else {
            transferParam->RunningTimeoutCount = 0;
            transferParam->RunningErrorCount = 0;
            //TODO : log the data to the file
            if (USB_ENDPOINT_DIRECTION_IN(transferParam->Ep.PipeId)) {
                LOG_MSG("Read %d bytes\n", ret);
            }
            else {
                LOG_MSG("Wrote %d bytes\n", ret);
            }

        }

        //TODO : get the time
    }

Done:

    for (i = 0; i < transferParam->TestParms->bufferCount; i++) {
        if (transferParam->TransferHandles[i].Overlapped.hEvent) {
            if (transferParam->TransferHandles[i].InUse) {
                if (!K.AbortPipe(
                    transferParam->TestParms->InterfaceHandle,
                    transferParam->Ep.PipeId) &&
                    !transferParam->TestParms->isUserAborted)
                {
                    ret = WinError(0);
                    LOG_ERROR("failed cancelling transfer ret = %d\n", ret);
                }
                else {
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
                WaitForSingleObject(
                    transferParam->TransferHandles[i].Overlapped.hEvent,
                    transferParam->TestParms->timeout
                );
            }
            else {
                WaitForSingleObject(
                    transferParam->TransferHandles[i].Overlapped.hEvent,
                    0
                );
            }
            CloseHandle(transferParam->TransferHandles[i].Overlapped.hEvent);
            transferParam->TransferHandles[i].Overlapped.hEvent = NULL;
        }
        transferParam->TransferHandles[i].InUse = FALSE;
    }

    transferParam->isRunning = FALSE;
    return 0;
}

int CreateVerifyBuffer(PPARAM TestParam, WORD endpointMaxPacketSize)
{
    int i;
    BYTE indexC = 0;
    TestParam->VerifyBuffer = malloc(endpointMaxPacketSize);
    if (!TestParam->VerifyBuffer)
    {
        LOG_ERROR("memory allocation failure at line %d!\n", __LINE__);
        return -1;
    }

    TestParam->verifyBufferSize = endpointMaxPacketSize;

    for (i = 0; i < endpointMaxPacketSize; i++)
    {
        TestParam->VerifyBuffer[i] = indexC++;
        if (indexC == 0) indexC = 1;
    }

    return 0;
}

void FreeTransferParam(PBENCHMARK_TRANSFER_PARAM* transferParamRef) {
    PBENCHMARK_TRANSFER_PARAM pTransferParam;
    int i;
    if ((!transferParamRef) || !*transferParamRef) return;
    pTransferParam = *transferParamRef;

    if (pTransferParam->TestParms)
    {
        for (i = 0; i < pTransferParam->TestParms->bufferCount; i++)
        {
            if (pTransferParam->TransferHandles[i].IsochHandle)
            {
                IsochK_Free(pTransferParam->TransferHandles[i].IsochHandle);
            }
        }
    }
    if (pTransferParam->ThreadHandle)
    {
        CloseHandle(pTransferParam->ThreadHandle);
        pTransferParam->ThreadHandle = NULL;
    }

    free(pTransferParam);

    *transferParamRef = NULL;
}

PBENCHMARK_TRANSFER_PARAM CreateTransferParam(PPARAM TestParam, int endpointID)
{
    PBENCHMARK_TRANSFER_PARAM transferParam = NULL;
    int pipeIndex, bufferIndex;
    int allocSize;

    PWINUSB_PIPE_INFORMATION_EX pipeInfo = NULL;

    /// Get Pipe Information
    for (pipeIndex = 0; pipeIndex < TestParam->InterfaceDescriptor.bNumEndpoints; pipeIndex++)
    {
        if (!(endpointID & USB_ENDPOINT_ADDRESS_MASK))
        {
            // Use first endpoint that matches the direction
            if ((TestParam->PipeInformation[pipeIndex].PipeId & USB_ENDPOINT_DIRECTION_MASK) == endpointID)
            {
                pipeInfo = &TestParam->PipeInformation[pipeIndex];
                break;
            }
        }
        else
        {
            if ((int)TestParam->PipeInformation[pipeIndex].PipeId == endpointID)
            {
                pipeInfo = &TestParam->PipeInformation[pipeIndex];
                break;
            }
        }
    }

    if (!pipeInfo)
    {
        LOG_ERROR("failed locating EP%02Xh!\n", endpointID);
        goto Done;
    }

    if (!pipeInfo->MaximumPacketSize)
    {
        LOG_WARNING("MaximumPacketSize=0 for EP%02Xh. check alternate settings.\n", pipeInfo->PipeId);
    }

    TestParam->bufferlength = max(TestParam->bufferlength, TestParam->readlenth);
    TestParam->bufferlength = max(TestParam->bufferlength, TestParam->writelength);

    allocSize = sizeof(BENCHMARK_TRANSFER_PARAM) + (TestParam->bufferlength * TestParam->bufferCount);
    transferParam = (PBENCHMARK_TRANSFER_PARAM)malloc(allocSize);

    if (transferParam)
    {
        UINT numIsoPackets;
        memset(transferParam, 0, allocSize);
        transferParam->TestParms = TestParam;

        memcpy(&transferParam->Ep, pipeInfo, sizeof(transferParam->Ep));
        transferParam->HasEpCompanionDescriptor = K.GetSuperSpeedPipeCompanionDescriptor(TestParam->InterfaceHandle, TestParam->InterfaceDescriptor.bAlternateSetting, (UCHAR)pipeIndex, &transferParam->EpCompanionDescriptor);

        if (ENDPOINT_TYPE(transferParam) == USB_ENDPOINT_TYPE_ISOCHRONOUS)
        {
            transferParam->TestParms->TransferMode = TRANSFER_MODE_ASYNC;

            if (!transferParam->Ep.MaximumBytesPerInterval)
            {
                LOG_ERROR("Unable to determine 'MaximumBytesPerInterval' for isochornous pipe %02X\n", transferParam->Ep.PipeId);
                LOGERR0("- Device firmware may be incorrectly configured.");
                FreeTransferParam(&transferParam);
                goto Done;
            }
            numIsoPackets = transferParam->TestParms->bufferlength / transferParam->Ep.MaximumBytesPerInterval;
            transferParam->numberOFIsoPackets = numIsoPackets;
            if (numIsoPackets == 0 || ((numIsoPackets % 8)) || transferParam->TestParms->bufferlength % transferParam->Ep.MaximumBytesPerInterval)
            {
                const UINT minBufferSize = transferParam->Ep.MaximumBytesPerInterval * 8;
                LOG_ERROR("Buffer size is not correct for isochornous pipe %02X\n", transferParam->Ep.PipeId);
                LOG_ERROR("- Buffer size must be an interval of %u\n", minBufferSize);
                FreeTransferParam(&transferParam);
                goto Done;
            }

            for (bufferIndex = 0; bufferIndex < transferParam->TestParms->bufferCount; bufferIndex++)
            {
                transferParam->TransferHandles[bufferIndex].Overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

                // Data buffer(s) are located at the end of the transfer param.
                transferParam->TransferHandles[bufferIndex].Data = transferParam->Buffer + (bufferIndex * transferParam->TestParms->bufferlength);

                if (!IsochK_Init(&transferParam->TransferHandles[bufferIndex].IsochHandle, TestParam->InterfaceHandle, transferParam->Ep.PipeId, numIsoPackets, transferParam->TransferHandles[bufferIndex].Data, transferParam->TestParms->bufferlength))
                {
                    LOG_ERROR("IsochK_Init failed for isochornous pipe %02X\n", transferParam->Ep.PipeId);
                    LOG_ERROR("- ErrorCode = %u\n", GetLastError());
                    FreeTransferParam(&transferParam);
                    goto Done;
                }

                if (!IsochK_SetPacketOffsets(transferParam->TransferHandles[bufferIndex].IsochHandle, transferParam->Ep.MaximumBytesPerInterval))
                {
                    LOG_ERROR("IsochK_SetPacketOffsets failed for isochornous pipe %02X\n", transferParam->Ep.PipeId);
                    LOG_ERROR("- ErrorCode = %u\n", GetLastError());
                    FreeTransferParam(&transferParam);
                    goto Done;
                }
            }
        }

        transferParam->ThreadHandle = CreateThread(
            NULL,
            0,
            (LPTHREAD_START_ROUTINE)TransferThread,
            transferParam,
            CREATE_SUSPENDED,
            &transferParam->ThreadId);

        if (!transferParam->ThreadHandle)
        {
            LOGERR0("failed creating thread!\n");
            FreeTransferParam(&transferParam);
            goto Done;
        }

        // If verify mode is on, this is a loop test, and this is a write endpoint, fill
        // the buffers with the same test data sent by a benchmark device when running
        // a read only test.
        if (transferParam->TestParms->TestType == TestTypeLoop && USB_ENDPOINT_DIRECTION_OUT(pipeInfo->PipeId))
        {
            // Data Format:
            // [0][KeyByte] 2 3 4 5 ..to.. wMaxPacketSize (if data byte rolls it is incremented to 1)
            // Increment KeyByte and repeat
            //
            BYTE indexC = 0;
            INT bufferIndex = 0;
            WORD dataIndex;
            INT packetIndex;
            INT packetCount = ((transferParam->TestParms->bufferCount * TestParam->readlenth) / pipeInfo->MaximumPacketSize);
            for (packetIndex = 0; packetIndex < packetCount; packetIndex++)
            {
                indexC = 2;
                for (dataIndex = 0; dataIndex < pipeInfo->MaximumPacketSize; dataIndex++)
                {
                    if (dataIndex == 0)			// Start
                        transferParam->Buffer[bufferIndex] = 0;
                    else if (dataIndex == 1)	// Key
                        transferParam->Buffer[bufferIndex] = packetIndex & 0xFF;
                    else						// Data
                        transferParam->Buffer[bufferIndex] = indexC++;

                    // if wMaxPacketSize is > 255, indexC resets to 1.
                    if (indexC == 0) indexC = 1;

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

void GetAverageBytesSec(PBENCHMARK_TRANSFER_PARAM transferParam, DOUBLE* bps){
    DWORD elapsedSeconds;
    if (!transferParam) return;

    if (transferParam->StartTick &&transferParam->StartTick < transferParam->LastTick){
        elapsedSeconds = (transferParam->LastTick - transferParam->StartTick) / 1000.0;
        *bps = (DOUBLE)transferParam->TotalTransferred / elapsedSeconds;
    }
    else{
        *bps = 0;
    }
}
void GetCurrentBytesSec(PBENCHMARK_TRANSFER_PARAM transferParam, DOUBLE* bps){
    DWORD elapsedSeconds;
    if (!transferParam) return;

    if (transferParam->LastStartTick && transferParam->LastStartTick < transferParam->LastTick){
        elapsedSeconds = (transferParam->LastTick - transferParam->LastStartTick) / 1000.0;
        *bps = (DOUBLE)transferParam->LastTransferred / elapsedSeconds;
    }
    else{
        *bps = 0;
    }
}

void ShowTransfer(PBENCHMARK_TRANSFER_PARAM transferParam){
	DOUBLE bpsAverage;
	DOUBLE bpsCurrent;
	DOUBLE elapsedSeconds;

	if (!transferParam) return;

	if (transferParam->HasEpCompanionDescriptor){
		if (transferParam->EpCompanionDescriptor.wBytesPerInterval){
			if (transferParam->Ep.PipeType == UsbdPipeTypeIsochronous){
				LOG_MSG("%s %s (Ep%02Xh) Maximum Bytes Per Interval:%lu Max Bursts:%u Multi:%u\n",
					EndpointTypeDisplayString[ENDPOINT_TYPE(transferParam)],
					TRANSFER_DISPLAY(transferParam, "Read", "Write"),
					transferParam->Ep.PipeId,
					transferParam->Ep.MaximumBytesPerInterval,
					transferParam->EpCompanionDescriptor.bMaxBurst+1,
					transferParam->EpCompanionDescriptor.bmAttributes.Isochronous.Mult+1);

			}

			else if (transferParam->Ep.PipeType == UsbdPipeTypeBulk){
				LOG_MSG("%s %s (Ep%02Xh) Maximum Bytes Per Interval:%lu Max Bursts:%u Max Streams:%u\n",
					EndpointTypeDisplayString[ENDPOINT_TYPE(transferParam)],
					TRANSFER_DISPLAY(transferParam, "Read", "Write"),
					transferParam->Ep.PipeId,
					transferParam->Ep.MaximumBytesPerInterval,
					transferParam->EpCompanionDescriptor.bMaxBurst + 1,
					transferParam->EpCompanionDescriptor.bmAttributes.Bulk.MaxStreams + 1);

			}

			else{
				LOG_MSG("%s %s (Ep%02Xh) Maximum Bytes Per Interval:%lu\n",
					EndpointTypeDisplayString[ENDPOINT_TYPE(transferParam)],
					TRANSFER_DISPLAY(transferParam, "Read", "Write"),
					transferParam->Ep.PipeId,
					transferParam->Ep.MaximumBytesPerInterval);

			}
		}

		else{
			LOG_MSG("%s %s (Ep%02Xh) Maximum Packet Size:%d\n",
				EndpointTypeDisplayString[ENDPOINT_TYPE(transferParam)],
				TRANSFER_DISPLAY(transferParam, "Read", "Write"),
				transferParam->Ep.PipeId,
				transferParam->Ep.MaximumPacketSize);
		}
	}

	else{
		LOG_MSG("%s %s (Ep%02Xh) Maximum Packet Size: %d\n",
			EndpointTypeDisplayString[ENDPOINT_TYPE(transferParam)],
			TRANSFER_DISPLAY(transferParam, "Read", "Write"),
			transferParam->Ep.PipeId,
			transferParam->Ep.MaximumPacketSize);
	}

	if (transferParam->StartTick){
		GetAverageBytesSec(transferParam, &bpsAverage);
		GetCurrentBytesSec(transferParam, &bpsCurrent);
		LOG_MSG("\tTotal Bytes     : %I64d\n", transferParam->TotalTransferred);
		LOG_MSG("\tTotal Transfers : %d\n", transferParam->Packets);

		if (transferParam->shortTrasnferred){
			LOG_MSG("\tShort Transfers : %d\n", transferParam->shortTrasnferred);
		}

		if (transferParam->TotalTimeoutCount){
			LOG_MSG("\tTimeout Errors  : %d\n", transferParam->TotalTimeoutCount);
		}

		if (transferParam->TotalErrorCount){
			LOG_MSG("\tOther Errors    : %d\n", transferParam->TotalErrorCount);
		}

		LOG_MSG("\tAvg. Bytes/sec  : %.2f\n", bpsAverage);

		if (transferParam->StartTick && transferParam->StartTick < transferParam->LastTick){
			elapsedSeconds = (transferParam->LastTick - transferParam->StartTick) / 1000.0;

			LOG_MSG("\tElapsed Time    : %.2f seconds\n", elapsedSeconds);
		}

		LOG_MSG("\n");
	}

}

BOOL WaitForTestTransfer(PBENCHMARK_TRANSFER_PARAM transferParam, UINT msToWait)
{
    DWORD exitCode;

    while (transferParam)
    {
        if (!transferParam->isRunning)
        {
            if (GetExitCodeThread(transferParam->ThreadHandle, &exitCode))
            {
                LOG_MSG("stopped Ep%02Xh thread.\tExitCode=%d\n",
                    transferParam->Ep.PipeId, exitCode);
                break;


            }

            LOG_ERROR("failed getting Ep%02Xh thread exit code!\n", transferParam->Ep.PipeId);
            break;
        }

        LOG_MSG("waiting for Ep%02Xh thread..\n", transferParam->Ep.PipeId);
        WaitForSingleObject(transferParam->ThreadHandle, 100);
        if (msToWait != INFINITE)
        {
            if ((msToWait - 100) == 0 || (msToWait - 100) > msToWait)
                return FALSE;
        }
    }

    return TRUE;
}

int main(int argc, char** argv) {
    PARAM TestParms;
    PBENCHMARK_TRANSFER_PARAM ReadTest = NULL;
    PBENCHMARK_TRANSFER_PARAM WriteTest = NULL;
    int key;
    long ec;
    unsigned int count;
    UCHAR bIsoAsap;

    if (argc == 1) {
        ShowUsage();
    }

    if (ParseArgs(&TestParms, argc, argv) < 0)
        return -1;

    InitializeCriticalSection(&DisplayCriticalSection);

    if (!LstK_Init(&TestParms.DeviceList, 0)) {
        ec = GetLastError();
        LOG_ERROR("Failed to initialize device list ec=%08Xh\n", ec);
        goto Done;
    }

    count = 0;
    LstK_Count(TestParms.DeviceList, &count);
    if (count == 0) {
        LOGERR0("No devices found\n");
        goto Done;
    }

    if (GetParamsDevice(&TestParms) < 0) {
        goto Done;
    }


    if (!Bench_Open(&TestParms))
       goto Done;

    if (TestParms.TestType & TestTypeRead) {
        ReadTest = CreateTransferParam(&TestParms, TestParms.endpoint | USB_ENDPOINT_DIRECTION_MASK);
        if (!ReadTest) goto Done;
        if (TestParms.UseRawIO != 0xFF)
        {
            if (!K.SetPipePolicy(TestParms.InterfaceHandle, ReadTest->Ep.PipeId, RAW_IO, 1, &TestParms.UseRawIO))
            {
                LOG_ERROR("SetPipePolicy:RAW_IO failed. ErrorCode=%08Xh\n", GetLastError());
                goto Done;
            }
        }
    }

    if (TestParms.TestType & TestTypeWrite) {
        WriteTest = CreateTransferParam(&TestParms, TestParms.endpoint);
        if (!WriteTest) goto Done;
        if (TestParms.fixedIsoPackets)
        {
            if (!K.SetPipePolicy(TestParms.InterfaceHandle, WriteTest->Ep.PipeId, ISO_NUM_FIXED_PACKETS, 2, &TestParms.fixedIsoPackets))
            {
                LOG_ERROR("SetPipePolicy:ISO_NUM_FIXED_PACKETS failed. ErrorCode=%08Xh\n", GetLastError());
                goto Done;
            }
        }
        if (TestParms.UseRawIO != 0xFF)
        {
            if (!K.SetPipePolicy(TestParms.InterfaceHandle, WriteTest->Ep.PipeId, RAW_IO, 1, &TestParms.UseRawIO))
            {
                LOG_ERROR("SetPipePolicy:RAW_IO failed. ErrorCode=%08Xh\n", GetLastError());
                goto Done;
            }
        }
    }

    if (TestParms.verify){
		if (ReadTest && WriteTest)
		{
			if (CreateVerifyBuffer(&TestParms, WriteTest->Ep.MaximumPacketSize) < 0)
				goto Done;
		}
		else if (ReadTest)
		{
			if (CreateVerifyBuffer(&TestParms, ReadTest->Ep.MaximumPacketSize) < 0)
				goto Done;
		}
	}

    bIsoAsap = (UCHAR)TestParms.UseIsoAsap;
    if (ReadTest) K.SetPipePolicy(TestParms.InterfaceHandle, ReadTest->Ep.PipeId, ISO_ALWAYS_START_ASAP, 1, &bIsoAsap);
    if (WriteTest) K.SetPipePolicy(TestParms.InterfaceHandle, WriteTest->Ep.PipeId, ISO_ALWAYS_START_ASAP, 1, &bIsoAsap);

    // Set the thread priority and start it.
    if (ReadTest)
    {
        SetThreadPriority(ReadTest->ThreadHandle, TestParms.priority);
        ResumeThread(ReadTest->ThreadHandle);
    }

    // Set the thread priority and start it.
    if (WriteTest)
    {
        SetThreadPriority(WriteTest->ThreadHandle, TestParms.priority);
        ResumeThread(WriteTest->ThreadHandle);
    }

    WaitForTestTransfer(ReadTest, 1000);
	if ((ReadTest) && ReadTest->isRunning){
		LOG_WARNING("Aborting Read Pipe 0x%02X..", ReadTest->Ep.PipeId);
		K.AbortPipe(TestParms.InterfaceHandle, ReadTest->Ep.PipeId);
	}
	
	WaitForTestTransfer(WriteTest, 1000);
	if ((WriteTest) && WriteTest->isRunning){
		LOG_WARNING("Aborting Write Pipe 0x%02X..", WriteTest->Ep.PipeId);
		K.AbortPipe(TestParms.InterfaceHandle, WriteTest->Ep.PipeId);
	}

	if ((ReadTest) && ReadTest->isRunning) 
		WaitForTestTransfer(ReadTest, INFINITE);
	if ((WriteTest) && WriteTest->isRunning) 
		WaitForTestTransfer(WriteTest, INFINITE);

        
	ShowParms(&TestParms);

    while(!TestParms.isCancelled){
        if (ReadTest)
            ShowTransfer(ReadTest);
        
        if (WriteTest)
            ShowTransfer(WriteTest);

    }
	if (ReadTest) ShowTransfer(ReadTest);
	if (WriteTest) ShowTransfer(WriteTest);

Done:
    if (TestParms.InterfaceHandle) {
        K.SetAltInterface(
            TestParms.InterfaceHandle,
            TestParms.InterfaceDescriptor.bInterfaceNumber,
            FALSE,
            TestParms.defaultAltSetting
        );
        K.Free(TestParms.InterfaceHandle);

        TestParms.InterfaceHandle = NULL;
    }

    if (!TestParms.use_UsbK_Init) {
        if (TestParms.DeviceHandle) {
            CloseHandle(TestParms.DeviceHandle);
            TestParms.DeviceHandle = NULL;
        }
    }

    if (TestParms.VerifyBuffer) {
        PBENCHMARK_BUFFER verifyBuffer, verifyListTemp;

        free(TestParms.VerifyBuffer);
        TestParms.VerifyBuffer = NULL;

        // DL_FOREACH_SAFE(TestParms.VerifyList, verifyBuffer, verifyListTemp) {
        //     DL_DELETE(TestParms.VerifyList, verifyBuffer);
        //     free(verifyBuffer);
        // }
    }

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
