/*!*********************************************************************
 *   uvperf.c
 *   Version : V1.1.1
 *   Author : gh-t-vaultmicro
 *   This is a simple utility to test the performance of USB transfers.
 *   It is designed to be used with the libusbK driver.
 *   The utility will perform a series of transfers to the specified endpoint
 *   and report the results.
 *
 *   Usage:
 *   uvperf -V VERBOSE-v VID -p PID -i INTERFACE -a AltInterface -e ENDPOINT -m TRANSFERMODE
 * -t TIMEOUT -b BUFFERCOUNT -l READLENGTH -w WRITELENGTH -r REPEAT -S
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
 *
 *   Example:
 *   uvperf -v0x1004 -p0xa000 -i0 -a0 -e0x81 -m1 -t1000 -l1024 -r1000
 *
 *   This will perform 1000 bulk transfers of 1024 bytes to endpoint 0x81
 *   on interface 0, alternate setting 0 of a device with VID 0x1004 and PID 0xA000.
 *   The transfers will have a timeout of 1000ms.
 *
 ********************************************************************!*/
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <wtypes.h>

#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

//included libusbk //erase one by one
#include "libusbk.h"
#include "lusbk_linked_list.h"
#include "lusbk_shared.h"

//included print out format
#include "log.h"

//include k.h
#include "k.h"

//included structure and functions
#include "setting.h"
#include "param.h"
#include "transfer_p.h"
#include "benchmark.h"

//included fileio
#include "fileio.h"

//included libusb headerfile
#include "libusb.h"
#include "usb_descriptor.h"

BOOL verbose = FALSE;

const char *TestDisplayString[] = {"None", "Read", "Write", "Loop", NULL};
const char *EndpointTypeDisplayString[] = {"Control", "Isochronous", "Bulk", "Interrupt", NULL};

KUSB_DRIVER_API K;
CRITICAL_SECTION DisplayCriticalSection;


#include <pshpack1.h>

typedef struct _KBENCH_CONTEXT_LSTK {
    BYTE Selected;
} KBENCH_CONTEXT_LSTK, *PKBENCH_CONTEXT_LSTK;

#include <poppack.h>

int ParseArgs(PUVPERF_PARAM TestParams, int argc, char **argv);

int ParseArgs(PUVPERF_PARAM TestParams, int argc, char **argv) {
    int i;
    int arg;
    char *temp;
    int value;
    int status = 0;

    int c;
    while ((c = getopt(argc, argv, "Vv:p:i:a:e:m:t:fb:l:w:r:SRWL")) != -1) {
        switch (c) {
        case 'V':
            verbose = TRUE;
            break;
        case 'v':
            TestParams->vid = strtol(optarg, NULL, 0);
            break;
        case 'p':
            TestParams->pid = strtol(optarg, NULL, 0);
            break;
        case 'i':
            TestParams->intf = strtol(optarg, NULL, 0);
            break;
        case 'a':
            TestParams->altf = strtol(optarg, NULL, 0);
            break;
        case 'e':
            TestParams->endpoint = strtol(optarg, NULL, 0);
            break;
        case 'm':
            TestParams->TransferMode =
                (strtol(optarg, NULL, 0) ? TRANSFER_MODE_ASYNC : TRANSFER_MODE_SYNC);
            break;
        case 'T':
            TestParams->Timer = strtol(optarg, NULL, 0);
            break;
        case 't':
            TestParams->timeout = strtol(optarg, NULL, 0);
            break;
        case 'f':
            TestParams->fileIO = TRUE;
            break;
        case 'b':
            TestParams->bufferCount = strtol(optarg, NULL, 0);
            if (TestParams->bufferCount > 1) {
                TestParams->TransferMode = TRANSFER_MODE_ASYNC;
            }
            break;
        case 'l':
            TestParams->readlenth = strtol(optarg, NULL, 0);
            break;
        case 'w':
            TestParams->writelength = strtol(optarg, NULL, 0);
            break;
        case 'r':
            TestParams->repeat = strtol(optarg, NULL, 0);
            break;
        case 'S':
            TestParams->ShowTransfer = TRUE;
            break;
        case 'R':
            TestParams->TestType = TestTypeIn;
            break;
        case 'W':
            TestParams->TestType = TestTypeOut;
            break;
        case 'L':
            TestParams->TestType = TestTypeLoop;
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

int main(int argc, char **argv) {
    UVPERF_PARAM TestParams;
    PUVPERF_TRANSFER_PARAM InTest = NULL;
    PUVPERF_TRANSFER_PARAM OutTest = NULL;
    int key;
    long ec;
    unsigned int count;
    UCHAR bIsoAsap;

//showing descriptors
    libusb_device **devs;
    libusb_device_handle *handle = NULL;
    int r;
    ssize_t cnt;

    libusb_init(NULL);

    cnt = libusb_get_device_list(NULL, &devs);
    if (cnt < 0) {
        LOG_MSG("Error in getting device list\n");
        libusb_exit(NULL);
        return -1;
    }

    handle = libusb_open_device_with_vid_pid(NULL, 0x1004, 0x61A1);
    //handle = libusb_open_device_with_vid_pid(NULL, 0x054C, 0x0E4F);
    if (handle == 0) {
        LOGERR0("Device not found\n");
    }


    if (argc == 1) {
        LOG_VERBOSE("No arguments\n");
        ShowUsage();
        return -1;
    }

    LOG_VERBOSE("SetParamsDefaults\n");
    SetParamsDefaults(&TestParams);

    LOG_VERBOSE("ParseArgs\n");
    if (ParseArgs(&TestParams, argc, argv) < 0)
        return -1;

    FileIOOpen(&TestParams);


    LOG_VERBOSE("InitializeCriticalSection\n");
    InitializeCriticalSection(&DisplayCriticalSection);

    LOG_VERBOSE("LibusbK device List Initialize\n");
    if (!LstK_Init(&TestParams.DeviceList, 0)) {
        ec = GetLastError();
        LOG_ERROR("Failed to initialize device list ec=%08Xh, message %s: \n", ec, strerror(ec));
        goto Final;
    }

    count = 0;
    LstK_Count(TestParams.DeviceList, &count);
    if (count == 0) {
        LOGERR0("No devices found\n");
        goto Final;
    }

    printf("\n");
    int interface_index, altinterface_index, endpoint_index;
    int lock = 0;
    while (lock != '\r' && lock != 't') {

        ShowMenu();

        LOG_MSG("Enter your choice: ");
        key = _getch();
        lock = key;
        printf("\n");

        switch (key) {
            case 'e':
                LOG_MSG("Enter interface index: ");
                scanf("%d", &interface_index);
                LOG_MSG("Enter alternative interface index: ");
                scanf("%d", &altinterface_index);
                LOG_MSG("Enter endpoint index: ");
                scanf("%d", &endpoint_index);
                ShowEndpointDescriptor(libusb_get_device(handle), interface_index, altinterface_index, endpoint_index);
                LOG_MSG("\n");
                break;
            case 'i':
                // interface0 set as bulk transfer
                // interface1 set as isochronous transfer
                // ref find_usb.py 
                LOG_MSG("Enter interface index: ");
                scanf("%d", &interface_index);
                LOG_MSG("Enter alternative interface index: ");
                scanf("%d", &altinterface_index);
                ShowInterfaceDescriptor(libusb_get_device(handle), interface_index, altinterface_index);
                LOG_MSG("\n");
                break;
            case 'c':
                ShowConfigurationDescriptor(libusb_get_device(handle));
                LOG_MSG("\n");
                break;
            case 'd':
                ShowDeviceDescriptor(libusb_get_device(handle));
                LOG_MSG("\n");
                break;
            case 't':
                PerformTransfer();
                LOG_MSG("\n");
                break;
            case 'q':
                LOG_MSG("\n");
                LOG_MSG("User Aborted\n");
                return 0;
            default:
                LOGERR0("Invalid input\n");
                LOG_MSG("\n");
                break;
        }
    }


    ShowDeviceInterfaces(libusb_get_device(handle));

    LOG_VERBOSE("GetDeviceInfoFromList\n");
    if (TestParams.intf == -1 || TestParams.altf == -1 || TestParams.endpoint == 0x00) {
        if (GetDeviceInfoFromList(&TestParams) < 0) {
            goto Final;
        }
        KLST_DEVINFO_HANDLE deviceInfo;
        WINUSB_PIPE_INFORMATION_EX pipeInfo[32];
        int userChoice;
        UCHAR pipeIndex;

        int altresult;
        int altSetting_tmp;

        while (1) {
            LOG_MSG("Enter the alternate setting number for the device interface (0-n): ");
            altresult = scanf("%d", &altSetting_tmp);

            if (altresult == 1) {
                break;
            } else {
                LOG_ERROR("Invalid input for alternate setting. Please enter a number: ");
                while (getchar() != '\n');
            }
        }


        int validInput = 0; // Flag to check for valid input
        do {
            while (LstK_MoveNext(TestParams.DeviceList, &deviceInfo)) {
                if (!LibK_LoadDriverAPI(&K, deviceInfo->DriverID)) {
                    WinError(GetLastError());
                    LOG_ERROR("Cannot load driver API for %s\n",
                              GetDrvIdString(deviceInfo->DriverID));
                    continue;
                }

                if (!K.Init(&TestParams.InterfaceHandle, deviceInfo)) {
                    WinError(GetLastError());
                    LOG_ERROR("Cannot initialize device interface for %s\n",
                              deviceInfo->DevicePath);
                    continue;
                }

                UCHAR altSetting=altSetting_tmp;

                
                int attemptCount = 0;
                int maxAttempts = 5;

                LOG_MSG("Device %s initialized successfully.\n", deviceInfo->DevicePath);
                while (K.QueryInterfaceSettings(TestParams.InterfaceHandle, altSetting,
                                                &TestParams.InterfaceDescriptor)) {
                    LOG_MSG("Interface %d: Checking pipes...\n",
                            TestParams.InterfaceDescriptor.bInterfaceNumber);
                    pipeIndex = 0;
                    if (!K.QueryPipeEx(TestParams.InterfaceHandle, altSetting, pipeIndex, &pipeInfo[pipeIndex])) {
                        LOG_ERROR("No pipes available. ErrorCode=0x%08X, message : %s\n",
                            GetLastError(), strerror(GetLastError()));
                        if (++attemptCount >= maxAttempts) {
                            LOG_ERROR("Maximum retry attempts reached. Exiting loop.\n");
                            goto Final;
                        }
                        continue;
                    }

                    while (K.QueryPipeEx(TestParams.InterfaceHandle, altSetting, pipeIndex, &pipeInfo[pipeIndex])) {
                        LOG_MSG("Pipe %d: Type : %11s, %3s, MaximumBytesPerInterval : %4d, "
                                "MaxPacketSize : %4d, MC = %2d\n",
                                pipeIndex + 1,
                                EndpointTypeDisplayString[pipeInfo[pipeIndex].PipeType],
                                (pipeInfo[pipeIndex].PipeId & USB_ENDPOINT_DIRECTION_MASK) ? "in"
                                                                                           : "out",
                                pipeInfo[pipeIndex].MaximumBytesPerInterval,
                                pipeInfo[pipeIndex].MaximumPacketSize,
                                (pipeInfo[pipeIndex].MaximumPacketSize & 0x1800) >> 11);
                        pipeIndex++;
                    }

                    LOG_MSG(
                        "Enter the number of the pipe to use for transfer (1-%d), 'Q' to quit: ",
                        pipeIndex);
                    int ch = _getche();
                    printf("\n");

                    if (ch == 'Q' || ch == 'q') {
                        LOG_MSG("Exiting program.\n");
                        return 0;
                    }

                    userChoice = ch - '0';
                    if (userChoice < 1 || userChoice > pipeIndex) {
                        LOGERR0("Invalid pipe selection.\n");
                        continue;
                    }

                    TestParams.endpoint = (int)(pipeInfo[userChoice - 1].PipeId);
                    TestParams.TestType =
                        (pipeInfo[userChoice - 1].PipeId & USB_ENDPOINT_DIRECTION_MASK)
                            ? TestTypeIn
                            : TestTypeOut;

                    LOG_MSG("Selected pipe 0x%02X\n", pipeInfo[userChoice - 1].PipeId);

                    validInput = 1;
                    break;
                }
            }
        } while (!validInput);
    } else {
        LOG_VERBOSE("GetDeviceParam\n");
        if (GetDeviceParam(&TestParams) < 0) {
            goto Final;
        }
    }

    LOG_VERBOSE("Open Bench\n");
    if (!Bench_Open(&TestParams)) {
        goto Final;
    }

    if (TestParams.TestType & TestTypeIn) {
        LOG_VERBOSE("CreateTransferParam for InTest\n");
        InTest = CreateTransferParam(&TestParams, TestParams.endpoint | USB_ENDPOINT_DIRECTION_MASK);
        if (!InTest){
            LOGERR0("Failed to create transfer param for InTest\n");
            goto Final;
        }
        if (TestParams.UseRawIO != 0xFF) {
            if (!K.SetPipePolicy(TestParams.InterfaceHandle, InTest->Ep.PipeId, RAW_IO, 1,
                                 &TestParams.UseRawIO)) {
                ec = GetLastError();
                LOG_ERROR("SetPipePolicy:RAW_IO failed. ErrorCode=%08Xh message : %s\n", ec,
                          strerror(ec));
                goto Final;
            }
        }
    }

    if (TestParams.TestType & TestTypeOut) {
        LOG_VERBOSE("CreateTransferParam for OutTest\n");
        OutTest = CreateTransferParam(&TestParams, TestParams.endpoint & 0x0F);
        if (!OutTest)
            goto Final;
        if (TestParams.fixedIsoPackets) {
            if (!K.SetPipePolicy(TestParams.InterfaceHandle, OutTest->Ep.PipeId,
                                 ISO_NUM_FIXED_PACKETS, 2, &TestParams.fixedIsoPackets)) {
                ec = GetLastError();
                LOG_ERROR("SetPipePolicy:ISO_NUM_FIXED_PACKETS failed. ErrorCode=0x%08X, "
                          "message : %s\n",
                          ec, strerror(ec));
                goto Final;
            }
        }
        if (TestParams.UseRawIO != 0xFF) {
            if (!K.SetPipePolicy(TestParams.InterfaceHandle, OutTest->Ep.PipeId, RAW_IO, 1,
                                 &TestParams.UseRawIO)) {
                ec = GetLastError();
                LOG_ERROR("SetPipePolicy:RAW_IO failed. ErrorCode=0x%08X\n, message : %s", ec,
                          strerror(ec));
                goto Final;
            }
        }
    }

    if (TestParams.verify) {
        if (InTest && OutTest) {
            LOG_VERBOSE("CreateVerifyBuffer for OutTest\n");
            if (CreateVerifyBuffer(&TestParams, OutTest->Ep.MaximumPacketSize) < 0)
                goto Final;
        } else if (InTest) {
            LOG_VERBOSE("CreateVerifyBuffer for InTest\n");
            if (CreateVerifyBuffer(&TestParams, InTest->Ep.MaximumPacketSize) < 0)
                goto Final;
        }
    }

    if ((OutTest && OutTest->Ep.PipeType == UsbdPipeTypeIsochronous) ||
        (InTest && InTest->Ep.PipeType == UsbdPipeTypeIsochronous)) {
        UINT frameNumber;
        LOG_VERBOSE("GetCurrentFrameNumber\n");
        if (!K.GetCurrentFrameNumber(TestParams.InterfaceHandle, &frameNumber)) {
            ec = GetLastError();
            LOG_ERROR("GetCurrentFrameNumber Failed. ErrorCode=%u, message : %s", ec, strerror(ec));
            goto Final;
        }
        frameNumber += TestParams.bufferCount * 2;
        if (OutTest) {
            OutTest->frameNumber = frameNumber;
            frameNumber++;
        }
        if (InTest) {
            InTest->frameNumber = frameNumber;
            frameNumber++;
        }
    }

    LOG_VERBOSE("ShowParams\n");
    ShowParams(&TestParams);
    if (InTest)
        ShowTransfer(InTest);
    if (OutTest)
        ShowTransfer(OutTest);

    bIsoAsap = (UCHAR)TestParams.UseIsoAsap;
    if (InTest)
        K.SetPipePolicy(TestParams.InterfaceHandle, InTest->Ep.PipeId, ISO_ALWAYS_START_ASAP, 1,
                        &bIsoAsap);
    if (OutTest)
        K.SetPipePolicy(TestParams.InterfaceHandle, OutTest->Ep.PipeId, ISO_ALWAYS_START_ASAP, 1,
                        &bIsoAsap);

    if (InTest) {
        LOG_VERBOSE("ResumeThread for InTest\n");
        SetThreadPriority(InTest->ThreadHandle, TestParams.priority);
        ResumeThread(InTest->ThreadHandle);
    }

    if (OutTest) {
        LOG_VERBOSE("ResumeThread for OutTest\n");
        SetThreadPriority(OutTest->ThreadHandle, TestParams.priority);
        ResumeThread(OutTest->ThreadHandle);
    }

    LOGMSG0("Press 'Q' to abort\n");

    FileIOLog(&TestParams);

    while (!TestParams.isCancelled) {

        Sleep(TestParams.refresh);
        if (_kbhit()) {
            key = _getch();
            switch (key) {
            case 'Q':
            case 'q':
                LOG_VERBOSE("User Aborted\n");
                TestParams.isUserAborted = TRUE;
                TestParams.isCancelled = TRUE;
            }

            if ((InTest) && !InTest->isRunning) {
                LOG_VERBOSE("InTest is not running\n");
                TestParams.isCancelled = TRUE;
                break;
            }

            if ((OutTest) && !OutTest->isRunning) {
                LOG_VERBOSE("OutTest is not running\n");
                TestParams.isCancelled = TRUE;
                break;
            }
        }

        if (TestParams.Timer && InTest &&
            InTest->LastTick.tv_sec - InTest->StartTick.tv_sec >= TestParams.Timer) {
            LOG_VERBOSE("Over 60 seconds\n");
            DWORD elapsedSeconds =
                ((InTest->LastTick.tv_sec - InTest->StartTick.tv_sec) +
                 (InTest->LastTick.tv_nsec - InTest->StartTick.tv_nsec) / 100000000.0);
            LOG_MSG("Elapsed Time %.2f  seconds\n", elapsedSeconds);
            TestParams.isUserAborted = TRUE;
            TestParams.isCancelled = TRUE;
        }

        if (TestParams.Timer && OutTest &&
            OutTest->LastTick.tv_sec - OutTest->StartTick.tv_sec >= TestParams.Timer) {
            LOG_VERBOSE("Over 60 seconds\n");
            DWORD elapsedSeconds =
                ((InTest->LastTick.tv_sec - InTest->StartTick.tv_sec) +
                 (InTest->LastTick.tv_nsec - InTest->StartTick.tv_nsec) / 100000000.0);
            LOG_MSG("Elapsed Time %.2f  seconds\n", elapsedSeconds);
            TestParams.isUserAborted = TRUE;
            TestParams.isCancelled = TRUE;
        }

        // if (TestParams.fileIO) {
        //     if (InTest) {
        //         FileIOBuffer(&TestParams, InTest);
        //     }
        //     if (OutTest) {
        //         FileIOBuffer(&TestParams, OutTest);
        //     }
        // }

        LOG_VERBOSE("ShowRunningStatus\n");
        ShowRunningStatus(InTest, OutTest);
        while (_kbhit())
            _getch();
    }

    LOG_VERBOSE("WaitForTestTransfer\n");
    WaitForTestTransfer(InTest, 1000);
    if ((InTest) && InTest->isRunning) {
        LOG_WARNING("Aborting Read Pipe 0x%02X..\n", InTest->Ep.PipeId);
        K.AbortPipe(TestParams.InterfaceHandle, InTest->Ep.PipeId);
    }

    WaitForTestTransfer(OutTest, 1000);
    if ((OutTest) && OutTest->isRunning) {
        LOG_WARNING("Aborting Write Pipe 0x%02X..\n", OutTest->Ep.PipeId);
        K.AbortPipe(TestParams.InterfaceHandle, OutTest->Ep.PipeId);
    }

    if ((InTest) && InTest->isRunning)
        WaitForTestTransfer(InTest, INFINITE);
    if ((OutTest) && OutTest->isRunning)
        WaitForTestTransfer(OutTest, INFINITE);

    LOG_VERBOSE("Show Transfer\n");
    if (InTest)
        ShowTransfer(InTest);
    if (OutTest)
        ShowTransfer(OutTest);

    freopen("CON", "w", stdout);
    freopen("CON", "w", stderr);

Final:
    LOG_VERBOSE("Free TransferParam\n");
    if (TestParams.InterfaceHandle) {
        LOG_VERBOSE("ResetPipe\n");
        K.SetAltInterface(TestParams.InterfaceHandle, TestParams.InterfaceDescriptor.bInterfaceNumber,
                          FALSE, TestParams.defaultAltSetting);
        K.Free(TestParams.InterfaceHandle);

        TestParams.InterfaceHandle = NULL;
    }

    LOG_VERBOSE("Close Handle\n");
    if (!TestParams.use_UsbK_Init) {
        if (TestParams.DeviceHandle) {
            CloseHandle(TestParams.DeviceHandle);
            TestParams.DeviceHandle = NULL;
        }
    }

    LOG_VERBOSE("Close Bench\n");
    if (TestParams.VerifyBuffer) {
        PUVPERF_BUFFER verifyBuffer, verifyListTemp;

        free(TestParams.VerifyBuffer);
        TestParams.VerifyBuffer = NULL;
    }

    LOG_VERBOSE("Free TransferParam\n");
    LstK_Free(TestParams.DeviceList);
    FreeTransferParam(&InTest);
    FreeTransferParam(&OutTest);

    DeleteCriticalSection(&DisplayCriticalSection);

    if (!TestParams.listDevicesOnly) {
        LOGMSG0("Press any key to exit\n");
        _getch();
        LOGMSG0("\n");
    }

    libusb_free_device_list(devs, 1);
    libusb_exit(NULL);

    FileIOClose(&TestParams);

    return 0;
}
