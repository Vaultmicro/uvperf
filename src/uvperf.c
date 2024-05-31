/*!*********************************************************************
 *   uvperf.c
 *   Version : V1.1.0
 *   Author : usiop-vault
 *   This is a simple utility to test the performance of USB transfers.
 *   It is designed to be used with the libusbK driver.
 *   The utility will perform a series of transfers to the specified endpoint
 *   and report the results.
 *
 *   Usage:
 *   uvperf -V VERBOSE-v VID -p PID -i INTERFACE -a AltInterface -e ENDPOINT -m TRANSFERMODE
 * -t TIMEOUT -b BUFFERLENGTH-c BUFFERCOUNT -l READLENGTH -w WRITELENGTH -r REPEAT -S
 *
 *   -VVERBOSE       Enable verbose output
 *   -vVID           USB Vendor ID
 *   -pPID           USB Product ID
 *   -iINTERFACE     USB Interface
 *   -aAltInterface  USB Alternate Interface
 *   -eENDPOINT      USB Endpoint
 *   -mTRANSFERMODE  0 = isochronous, 1 = bulk
 *   -tTIMEOUT       USB Transfer Timeout
 *   -bBUFFERLENGTH  Length of buffers
 *   -cBUFFERCOUNT   Number of buffers to use
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

#include "uvperf.h"
#include "bench.h"
#include "log.h"
#include "transfer.h"
#include "usb_descriptor.h"
#include "utils.h"

KUSB_DRIVER_API K;
CRITICAL_SECTION DisplayCriticalSection;
BOOL verbose = FALSE;

const char *EndpointTypeDisplayString[] = {"Control", "Isochronous", "Bulk", "Interrupt", NULL};

int main(int argc, char **argv) {
    UVPERF_PARAM TestParms;
    PUVPERF_TRANSFER_PARAM InTest = NULL;
    PUVPERF_TRANSFER_PARAM OutTest = NULL;
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

    FileIOOpen(&TestParms);

    // if (fetch_usb_descriptors(&TestParms) < 0) {
    //     LOG_ERROR("Failed to bring endpoint descriptor using libusb library\n");
    // }

    LOG_VERBOSE("InitializeCriticalSection\n");
    InitializeCriticalSection(&DisplayCriticalSection);

    LOG_VERBOSE("LibusbK device List Initialize\n");
    if (!LstK_Init(&TestParms.DeviceList, 0)) {
        ec = GetLastError();
        LOG_ERROR("Failed to initialize device list ec=%08Xh, message %s: \n", ec, strerror(ec));
        goto Final;
    }

    count = 0;
    LstK_Count(TestParms.DeviceList, &count);
    if (count == 0) {
        LOGERR0("No devices found\n");
        goto Final;
    }

    if (TestParms.list) {
        LOG_VERBOSE("GetDeviceFromList\n");
        if (GetDeviceInfoFromList(&TestParms) < 0) {
            goto Final;
        }

        if (GetEndpointFromList(&TestParms) < 0) {
            goto Final;
        }

    } else {
        LOG_VERBOSE("GetDeviceParam\n");
        if (GetDeviceParam(&TestParms) < 0) {
            goto Final;
        }

        LOG_VERBOSE("Open Bench\n");
        if (!Bench_Open(&TestParms)) {
            goto Final;
        }
    }

    if (TestParms.TestType & TestTypeIn) {
        LOG_VERBOSE("CreateTransferParam for InTest\n");
        InTest = CreateTransferParam(&TestParms, TestParms.endpoint | USB_ENDPOINT_DIRECTION_MASK);
        if (!InTest)
            goto Final;
        if (TestParms.UseRawIO != 0xFF) {
            if (!K.SetPipePolicy(TestParms.InterfaceHandle, InTest->Ep.PipeId, RAW_IO, 1,
                                 &TestParms.UseRawIO)) {
                ec = GetLastError();
                LOG_ERROR("SetPipePolicy:RAW_IO failed. ErrorCode=%08Xh message : %s\n", ec,
                          strerror(ec));
                goto Final;
            }
        }
    }

    if (TestParms.TestType & TestTypeOut) {
        LOG_VERBOSE("CreateTransferParam for OutTest\n");
        OutTest = CreateTransferParam(&TestParms, TestParms.endpoint & 0x0F);
        if (!OutTest)
            goto Final;
        if (TestParms.fixedIsoPackets) {
            if (!K.SetPipePolicy(TestParms.InterfaceHandle, OutTest->Ep.PipeId,
                                 ISO_NUM_FIXED_PACKETS, 2, &TestParms.fixedIsoPackets)) {
                ec = GetLastError();
                LOG_ERROR("SetPipePolicy:ISO_NUM_FIXED_PACKETS failed. ErrorCode=0x%08X, "
                          "message : %s\n",
                          ec, strerror(ec));
                goto Final;
            }
        }
        if (TestParms.UseRawIO != 0xFF) {
            if (!K.SetPipePolicy(TestParms.InterfaceHandle, OutTest->Ep.PipeId, RAW_IO, 1,
                                 &TestParms.UseRawIO)) {
                ec = GetLastError();
                LOG_ERROR("SetPipePolicy:RAW_IO failed. ErrorCode=0x%08X\n, gessage : %s", ec,
                          strerror(ec));
                goto Final;
            }
        }
    }

    if (TestParms.verify) {
        if (InTest && OutTest) {
            LOG_VERBOSE("CreateVerifyBuffer for OutTest\n");
            if (CreateVerifyBuffer(&TestParms, OutTest->Ep.MaximumPacketSize) < 0)
                goto Final;
        } else if (InTest) {
            LOG_VERBOSE("CreateVerifyBuffer for InTest\n");
            if (CreateVerifyBuffer(&TestParms, InTest->Ep.MaximumPacketSize) < 0)
                goto Final;
        }
    }

    if ((OutTest && OutTest->Ep.PipeType == UsbdPipeTypeIsochronous) ||
        (InTest && InTest->Ep.PipeType == UsbdPipeTypeIsochronous)) {
        UINT frameNumber;
        LOG_VERBOSE("GetCurrentFrameNumber\n");
        if (!K.GetCurrentFrameNumber(TestParms.InterfaceHandle, &frameNumber)) {
            ec = GetLastError();
            LOG_ERROR("GetCurrentFrameNumber Failed. ErrorCode=%u, message : %s", ec, strerror(ec));
            goto Final;
        }
        frameNumber += TestParms.bufferCount * 2;
        if (OutTest) {
            OutTest->frameNumber = frameNumber;
            frameNumber++;
        }
        if (InTest) {
            InTest->frameNumber = frameNumber;
            frameNumber++;
        }
    }

    // if (find_endpoint_descriptor(&TestParms) < 0) {
    //     LOGERR0("Failed to find endpoint descriptor\n");
    //     goto Final;
    // }

    LOG_VERBOSE("ShowParms\n");
    ShowParms(&TestParms);
    if (InTest)
        ShowTransfer(InTest);
    if (OutTest)
        ShowTransfer(OutTest);

    bIsoAsap = (UCHAR)TestParms.UseIsoAsap;
    if (InTest)
        K.SetPipePolicy(TestParms.InterfaceHandle, InTest->Ep.PipeId, ISO_ALWAYS_START_ASAP, 1,
                        &bIsoAsap);
    if (OutTest)
        K.SetPipePolicy(TestParms.InterfaceHandle, OutTest->Ep.PipeId, ISO_ALWAYS_START_ASAP, 1,
                        &bIsoAsap);

    if (InTest->Ep.PipeType == UsbdPipeTypeIsochronous ||
        OutTest->Ep.PipeType == UsbdPipeTypeIsochronous) {
        ChangeAlternateSetting(TestParms.InterfaceHandle,
                               TestParms.InterfaceDescriptor.bInterfaceNumber, 1);
    }

    if (InTest) {
        LOG_VERBOSE("ResumeThread for InTest\n");
        SetThreadPriority(InTest->ThreadHandle, TestParms.priority);
        ResumeThread(InTest->ThreadHandle);
    }

    if (OutTest) {
        LOG_VERBOSE("ResumeThread for OutTest\n");
        SetThreadPriority(OutTest->ThreadHandle, TestParms.priority);
        ResumeThread(OutTest->ThreadHandle);
    }

    LOGMSG0("Press 'Q' to abort\n");

    FileIOLog(&TestParms);

    while (!TestParms.isCancelled) {
        Sleep(TestParms.refresh);
        if (_kbhit()) {
            key = _getch();
            switch (key) {
            case 'Q':
            case 'q':
                LOG_VERBOSE("User Aborted\n");
                TestParms.isUserAborted = TRUE;
                TestParms.isCancelled = TRUE;
            }

            if ((InTest) && !InTest->isRunning) {
                LOG_VERBOSE("InTest is not running\n");
                TestParms.isCancelled = TRUE;
                break;
            }

            if ((OutTest) && !OutTest->isRunning) {
                LOG_VERBOSE("OutTest is not running\n");
                TestParms.isCancelled = TRUE;
                break;
            }
        }

        if (TestParms.Timer && InTest &&
            InTest->LastTick.tv_sec - InTest->StartTick.tv_sec >= TestParms.Timer) {
            LOG_VERBOSE("Over 60 seconds\n");
            DWORD elapsedSeconds =
                ((InTest->LastTick.tv_sec - InTest->StartTick.tv_sec) +
                 (InTest->LastTick.tv_nsec - InTest->StartTick.tv_nsec) / 100000000.0);
            LOG_MSG("Elapsed Time %.2f  seconds\n", elapsedSeconds);
            TestParms.isUserAborted = TRUE;
            TestParms.isCancelled = TRUE;
        }

        if (TestParms.Timer && OutTest &&
            OutTest->LastTick.tv_sec - OutTest->StartTick.tv_sec >= TestParms.Timer) {
            LOG_VERBOSE("Over 60 seconds\n");
            DWORD elapsedSeconds =
                ((InTest->LastTick.tv_sec - InTest->StartTick.tv_sec) +
                 (InTest->LastTick.tv_nsec - InTest->StartTick.tv_nsec) / 100000000.0);
            LOG_MSG("Elapsed Time %.2f  seconds\n", elapsedSeconds);
            TestParms.isUserAborted = TRUE;
            TestParms.isCancelled = TRUE;
        }

        LOG_VERBOSE("ShowRunningStatus\n");
        ShowRunningStatus(InTest, OutTest);
        while (_kbhit())
            _getch();
    }

    LOG_VERBOSE("WaitForTestTransfer\n");
    WaitForTestTransfer(InTest, 1000);
    if ((InTest) && InTest->isRunning) {
        LOG_WARNING("Aborting Read Pipe 0x%02X..", InTest->Ep.PipeId);
        K.AbortPipe(TestParms.InterfaceHandle, InTest->Ep.PipeId);
    }

    WaitForTestTransfer(OutTest, 1000);
    if ((OutTest) && OutTest->isRunning) {
        LOG_WARNING("Aborting Write Pipe 0x%02X..", OutTest->Ep.PipeId);
        K.AbortPipe(TestParms.InterfaceHandle, OutTest->Ep.PipeId);
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

        free(TestParms.VerifyBuffer);
        TestParms.VerifyBuffer = NULL;
    }

    LOG_VERBOSE("Free TransferParam\n");
    LstK_Free(TestParms.DeviceList);
    FreeTransferParam(&InTest);
    FreeTransferParam(&OutTest);

    DeleteCriticalSection(&DisplayCriticalSection);

    if (!TestParms.listDevicesOnly) {
        LOGMSG0("Press any key to exit\n");
        _getch();
        LOGMSG0("\n");
    }

    FileIOClose(&TestParms);

    return 0;
}
