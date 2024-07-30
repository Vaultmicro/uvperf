#ifndef SETTING_H
#define SETTING_H

#include <conio.h>
//#include <stdarg.h>
//#include <stdio.h>
//#include <stdlib.h>
//#include <string.h>
#include <time.h>
//#include <unistd.h>
#include <windows.h>
//#include <wtypes.h>

#include "libusbk.h"
#include "log.h"

#define MAX_OUTSTANDING_TRANSFERS 10

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
    TestTypeIn = 0x01,
    TestTypeOut = 0x02,
    TestTypeLoop = TestTypeIn | TestTypeOut,
} UVPERF_DEVICE_TRANSFER_TYPE,
    *PUVPERF_DEVICE_TRANSFER_TYPE;

typedef enum _UVPERF_TRANSFER_MODE {
    TRANSFER_MODE_SYNC,
    TRANSFER_MODE_ASYNC,
} UVPERF_TRANSFER_MODE;

typedef struct _BENCHMARK_ISOCH_RESULTS {
    UINT GoodPackets;
    UINT BadPackets;
    UINT Length;
    UINT TotalPackets;
} BENCHMARK_ISOCH_RESULTS;

typedef struct _UVPERF_PARAM {
    int vid;
    int pid;
    int intf;
    int altf;
    int endpoint;
    int Timer;
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

    FILE *BufferFile;
    FILE *LogFile;
    char BufferFileName[MAX_PATH];
    char LogFileName[MAX_PATH];

    UCHAR UseRawIO;
    UCHAR DefaultAltSetting;
    BOOL UseIsoAsap;
    BYTE *VerifyBuffer;

    unsigned char defaultAltSetting;
} UVPERF_PARAM, *PUVPERF_PARAM;


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
    PUVPERF_PARAM TestParams;
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

LONG WinError(__in_opt DWORD errorCode);


#endif // SETTING_H
