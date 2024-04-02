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

#include "libusbk/libusbk.h"
#include "libusbk/lusbk_shared.h"
#include "libusbk/lusbk_linked_list.h"

#define MAX_OUTSTANDING_TRANSFERS 10

#define LOG(LogTypeString, format, ...) printf("%s[" __FUNCTION__"] "format, LogTypeString, ##__VA_ARGS__)
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

KUSB_DRIVER_API K;
CRITICAL_SECTION DisplayCriticalSection;

typedef struct _BENCHMARK_BUFFER{
    unsigned char*  Data;
    long            dataLenth;
    long            syncFailed;
    
    struct _BENCHMARK_BUFFER* next;
    struct _BENCHMARK_BUFFER* prev;
} BENCHMARK_BUFFER, *PBENCHMARK_BUFFER;

typedef enum _BENCHMARK_DEVICE_TEST_TYPE{
    TestTypeNone = 0x00,
    TestTypeRead = 0x01,
    TestTypeWrite = 0x02,
    TestTypeLoop = TestTypeRead | TestTypeWrite,
} BENCHMARK_DEVICE_TEST_TYPE, *PBENCHMARK_DEVICE_TEST_TYPE;

typedef enum _BENCHMARK_TRANSFER_MODE{
    TRANSFER_MODE_SYNC,
    TRANSFER_MODE_ASYNC,
} BENCHMARK_TRANSFER_MODE;

typedef struct _PARAM{
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
    BOOL useList;
    BOOL verify;
    BOOL verifyDetails;
    enum BENCHMARK_DEVICE_TEST_TYPE TestType;
    enum BENCHMARK_TRANSFER_MODE TransferMode;

    BOOL use_UsbK_Init;
    BOOL listDevicesOnly;
    
    KLST_HANDLE DeviceList;
    KLST_DEVINFO_HANDLE SelectedDeviceProfile;
    HANDLE DeviceHandle;
    KUSB_HANDLE InterfaceHandle;
    USB_DEVICE_DESCRIPTOR DeviceDescriptor;
    USB_CONFIGURATION_DESCRIPTOR ConfigDescriptor;
    USB_INTERFACE_DESCRIPTOR InterfaceDescriptor;
    USB_ENDPOINT_DESCRIPTOR EndpointDescriptor;
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

    PBENCHMARK_BUFFER VerifyBuffer;
    PBENCHMARK_BUFFER VerifyList;

    unsigned char defaultAltSetting
} PARAM, *PPARAM;

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

typedef struct _BENCHMARK_TRANSFER_PARAM{
    PPARAM TestParms;
    unsigned int frameNumber;
    unsigned int numberOFIsoPackets;
    HANDLE ThreadHandle;
    DWORD ThreadId;
    WINUSB_PIPE_INFORMATION_EX Ep;
    BOOL isRunning;

    long long TotalTransferred;
    long LastTransferred;

    int TotalTimeoutCount;
    int RunningTimeoutCount;

    int TotalErrorCount;
    int RunningErrorCount;

	BENCHMARK_TRANSFER_HANDLE TransferHandles[MAX_OUTSTANDING_TRANSFERS];
	BENCHMARK_ISOCH_RESULTS IsochResults;

    unsigned char Buffer[0]
} BENCHMARK_TRANSFER_PARAM, *PBENCHMARK_TRANSFER_PARAM;

void SetParamsDefaults(PPARAM TestParms);
int GetParamsDevice(PPARAM TestParms);

int ParseArgs(PARAM TestParms, int argc, char** argv);
void ShowParms(PARAM TestParms);
PBENCHMARK_TRANSFER_PARAM CreateTransferParam(PPARAM TestParms, int endpointID);
DWORD TransferThread(PBENCHMARK_TRANSFER_PARAM transferParam);
void FreeTransferParam(PBENCHMARK_TRANSFER_PARAM* transferParam);

#define TRANSFER_DISPLAY(TransferParam, ReadingString, WritingString) \
	((TransferParam->Ep.PipeId & USB_ENDPOINT_DIRECTION_MASK) ? ReadingString : WritingString)

void AppendLoopBuffer(
    PPARAM TestParms,
    unsigned char* buffer,
    unsigned int length
    ){
        if(TestParms->verify && TestParms->TestType == TestTypeLoop){
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

int TransferSync(PBENCHMARK_TRANSFER_PARAM transferParam){
    unsigned int trasnferred;
    BOOL success;

    if(transferParam->Ep.PipeType & USB_ENDPOINT_DIRECTION_MASK){
        success = K.ReadPipe(
            transferParam->TestParms->InterfaceHandle,
            transferParam->Ep.PipeId,
            transferParam->Buffer,
            transferParam->TestParms->readlenth,
            &trasnferred, NULL
        );
    }
    else{
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
    );

//TODO : TransferAsync
int TransferAsync(PBENCHMARK_TRANSFER_PARAM transferParam){
    return 0;
}

//Todo : Prepare the Data from the file
void VerifyLoopData();

void SetParamsDefaults(PPARAM TestParms){
    memset(TestParms, 0, sizeof(*TestParms));

    TestParms->vid          = 0x1004;
    TestParms->pid          = 0xA000;
    TestParms->intf         = -1;
    TestParms->altf         = -1;
    TestParms->endpoint     = 0x81;
    TestParms->transfer     = 1;
    TestParms->timeout      = 3000;
    TestParms->bufferlength = 1024;
    TestParms->readlenth    = TestParms->bufferlength;
    TestParms->writelength  = TestParms->bufferlength;
}

int ParseArgs(PPARAM TestParms, int argc, char** argv){
    int i;
    int arg;
    int value;
    int status = 0;

    for (i = 1; i < argc; i++){
        if (argv[i][0] == '-'){
            arg = argv[i][1];
            value = atoi(&argv[i][2]);
            switch (arg){
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
                default:
                    LOGERR0("Invalid argument\n");
                    status = -1;
                    break;
            }
        }
    }
    return status;
}

void ShowParms(PPARAM TestParms){
    if(!TestParms)
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

DWORD TransferThread(PBENCHMARK_TRANSFER_PARAM transferParam){
    int ret, i;
    PBENCHMARK_TRANSFER_HANDLE handle;
    unsigned char* buffer;

    transferParam->isRunning = TRUE;

    while(!transferParam->TestParms->isCancelled){
        buffer = NULL;
        handle = NULL;

        if(transferParam->TestParms->TransferMode == TransferSync){
            ret = TransferSync(transferParam);
            if(ret >= 0)
                buffer = transferParam->Buffer;
        }
        else if(transferParam->TestParms->TransferMode == TransferAsync){
            ret = TransferAsync(transferParam);
            if((handle) && ret >= 0)
                buffer = transferParam->Buffer;
        }
        else{
            LOG_ERROR("Invalid transfer mode %d\n",
                transferParam->TestParms->TransferMode);
            goto Done;
        }

        if( transferParam->TestParms->verify &&
            transferParam->TestParms->VerifyList &&
            transferParam->TestParms->TestType == TestTypeLoop &&
            USB_ENDPOINT_DIRECTION_IN(transferParam->Ep.PipeId) && ret > 0)
        {
            VerifyLoopData(transferParam->TestParms, buffer);
        }

        if(ret < 0){
            // user pressed 'Q' or 'ctrl+c'
            if(transferParam->TestParms->isUserAborted)
                break;
            
            // timeout
            if( ret == ERROR_SEM_TIMEOUT
                || ret == ERROR_OPERATION_ABORTED
                || ret == ERROR_CANCELLED)
            {
                transferParam->TotalTimeoutCount++;
                transferParam->RunningTimeoutCount++;
                LOG_ERROR("Timeout #%d %s on EP%02Xh.. \n",
                    transferParam->RunningTimeoutCount,
                    TRANSFER_DISPLAY(transferParam, "reading", "writing"),
                    transferParam->Ep.PipeId);

                if(transferParam->RunningTimeoutCount > transferParam->TestParms->retry)
                    break;
            }

            // other error
            else{
                transferParam->TotalErrorCount++;
                transferParam->RunningErrorCount++;
                LOG_ERROR("failed %s, %d of %d ret=%d\n",
                    TRANSFER_DISPLAY(transferParam, "reading", "writing"),
                    transferParam->RunningErrorCount,
                    transferParam->TestParms->retry + 1,
                    ret);

                K.ResetPipe(transferParam->TestParms->InterfaceHandle, transferParam->Ep.PipeId);

                if(transferParam->RunningErrorCount > transferParam->TestParms->retry)
                    break;
            }

            ret = 0;
        }
        else{
            transferParam->RunningTimeoutCount = 0;
            transferParam->RunningErrorCount = 0;
            //TODO : log the data to the file
            if(USB_ENDPOINT_DIRECTION_IN(transferParam->Ep.PipeId)){
                LOG_MSG("Read %d bytes\n", ret);
            }
            else{
                LOG_MSG("Wrote %d bytes\n", ret);
            }

        }

        //TODO : get the time
    }

Done:

    for(i = 0; i < transferParam->TestParms->bufferCount; i++){
        if(transferParam->TransferHandles[i].Overlapped.hEvent){
            if(transferParam->TransferHandles[i].InUse){
                if(!K.AbortPipe(
                    transferParam->TestParms->InterfaceHandle,
                    transferParam->Ep.PipeId) &&
                    !transferParam->TestParms->isUserAborted)
                {
                    ret = WinError(0);
                    LOG_ERR("failed cancelling transfer ret = %d\n", ret);
                }
                else{
                    CloseHandle(transferParam->TransferHandles[i].Overlapped.hEvent);
                    transferParam->TransferHandles[i].Overlapped.hEvent = NULL;
                    transferParam->TransferHandles[i].InUse = FALSE;
                }
            }
            Sleep(0);
        }
    }

    for(i = 0; i < transferParam->TestParms->bufferCount; i++){
        if(transferParam->TransferHandles[i].Overlapped.hEvent){
            if(transferParam->TransferHandles[i].InUse){
                WaitForSingledObject(
                    transferParam->TransferHandles[i].Overlapped.hEvent,
                    transferParam->TestParms->timeout
                );
            }
            else{
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

int main(int argc, char** argv){
    PARAM TestParms;
    PBENCHMARK_TRANSFER_PARAM ReadTest = NULL;
    PBENCHMARK_TRANSFER_PARAM WriteTest = NULL;
    int key;
    long ec;
    unsigned int count, length;

    // todo : when argc == 1, print usage message
    if(argc == 1){
        // print usage message
        LOGERR0("Invalid argument\n");
        return -1;
    }

    if(ParseArgs(&TestParms, argc, argv) < 0)
        return -1;

    InitializeCriticalSection(&DisplayCriticalSection);

    if(!LstK_Init(&TestParms.DeviceList, 0)){
        ec = GetLastError();
        LOG_ERROR("Failed to initialize device list ec=%08Xh\n", ec);
        goto Done;
    }

    count = 0;
    LstK_Count(TestParms.DeviceList, &count);
    if(count == 0){
        LOGERR0("No devices found\n");
        goto Done;
    }

    if(TestParms.useList){
        if(GetTestDeviceFromList(&TestParms) < 0){
            goto Done;
        }
    }
    else{
        if(GetTestDeviceFromArgs(&TestParms) < 0){
            goto Done;
        }
    }

    if(TestParms.listDevicesOnly)
        goto Done;
    
Done:
    if(TestParms.InterfaceHandle){
        K.SetAltInterface(
                TestParms.InterfaceHandle,
                TestParms.InterfaceDescriptor.bInterfaceNumber,
                FALSE,
                TestParms.defaultAltSetting
        );
        K.Free(TestParms.InterfaceHandle);

        TestParms.InterfaceHandle = NULL;
    }

    if(!TestParms.use_UsbK_Init){
        if(TestParms.DeviceHandle){
            CloseHandle(TestParms.DeviceHandle);
            TestParms.DeviceHandle = NULL;
        }
    }

    if(TestParms.VerifyBuffer){
        PBENCHMARK_BUFFER verifyBuffer, verifyListTemp;

        free(TestParms.VerifyBuffer);
        TestParms.VerifyBuffer = NULL;

        DL_FOREACH_SAFE(TestParms.VerifyList, verifyBuffer, verifyListTemp){
            DL_DELETE(TestParms.VerifyList, verifyBuffer);
            free(verifyBuffer);
        }
    }

    if(TestParms.ReadLogFile){
        fflush(TestParms.ReadLogFile);
        fclose(TestParms.ReadLogFile);
        TestParms.ReadLogFile = NULL;
    }

    if(TestParms.WriteLogFile){
        fflush(TestParms.WriteLogFile);
        fclose(TestParms.WriteLogFile);
        TestParms.WriteLogFile = NULL;
    }

    LstK_Free(TestParms.DeviceList);
    FreeTransferParam(&ReadTest);
    FreeTransferParam(&WriteTest);

    DeleteCriticalSection(&DisplayCriticalSection);

    if(!TestParms.listDevicesOnly){
        LOGMSG0("Press any key to exit\n");
        _getch();
        LOGMSG0("\n");
    }

    return 0;
}
