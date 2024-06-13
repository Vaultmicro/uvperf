#include "utils.h"
#include "bench.h"
#include "log.h"
#include "transfer.h"
#include "usb_descriptor.h"
#include "uvperf.h"

static LPCSTR DrvIdNames[8] = {"libusbK", "libusb0", "WinUSB", "libusb0 filter",
                               "Unknown", "Unknown", "Unknown"};

#define GetDrvIdString(DriverID)                                                                   \
    (DrvIdNames[((((LONG)(DriverID)) < 0) || ((LONG)(DriverID)) >= KUSB_DRVID_COUNT)               \
                    ? KUSB_DRVID_COUNT                                                             \
                    : (DriverID)])

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

LONG WinError(__in_opt DWORD errorCode) {
    LPSTR buffer = NULL;

    errorCode = errorCode ? errorCode : GetLastError();
    if (!errorCode)
        return errorCode;

    if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, errorCode,
                       0, (LPSTR)&buffer, 0, NULL) > 0) {
        SetLastError(0);
    } else {
        LOGERR0("FormatMessage error!\n");
    }

    if (buffer)
        LocalFree(buffer);

    return -((LONG)errorCode);
}

void ShowUsage() {
    LOG_MSG("Version : V1.1.0\n");
    LOG_MSG("\n");
    LOG_MSG(
        "Usage: uvperf -v VID -p PID -i INTERFACE -a AltInterface -e ENDPOINT -m TRANSFERMODE "
        "-T TIMER -t TIMEOUT -f FileIO -b BUFFERCOUNT-l READLENGTH -w WRITELENGTH -r REPEAT -S \n");
    LOG_MSG("\t-v VID           USB Vendor ID\n");
    LOG_MSG("\t-p PID           USB Product ID\n");
    LOG_MSG("\t-i INTERFACE     USB Interface\n");
    LOG_MSG("\t-a AltInterface  USB Alternate Interface\n");
    LOG_MSG("\t-e ENDPOINT      USB Endpoint\n");
    LOG_MSG("\t-m TRANSFER      0 = isochronous, 1 = bulk\n");
    LOG_MSG("\t-T TIMER         Timer in seconds\n");
    LOG_MSG("\t-t TIMEOUT       USB Transfer Timeout\n");
    LOG_MSG("\t-f FileIO        Use file I/O, default : FALSE\n");
    LOG_MSG("\t-c BUFFERCOUNT   Number of buffers to use\n");
    LOG_MSG("\t-b BUFFERLENGTH  Length of buffers\n");
    LOG_MSG("\t-l READLENGTH    Length of read transfers\n");
    LOG_MSG("\t-w WRITELENGTH   Length of write transfers\n");
    LOG_MSG("\t-r REPEAT        Number of transfers to perform\n");
    LOG_MSG("\t-S               Show transfer data, default : FALSE\n");
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
    TestParms->pid = 0x61a1;
    TestParms->intf = -1;
    TestParms->altf = -1;
    TestParms->endpoint = 0x00;
    TestParms->TransferMode = TRANSFER_MODE_SYNC;
    TestParms->TestType = TestTypeIn;
    TestParms->Timer = 0;
    TestParms->timeout = 3000;
    TestParms->fileIO = FALSE;
    TestParms->list = TRUE;
    TestParms->bufferlength = 512;
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

            LibK_SetContext(deviceInfo, KLIB_HANDLE_TYPE_LSTINFOK, (KLIB_USER_CONTEXT)FALSE);
        }

        if (!count) {
            LOG_ERROR("can not find vid : 0x%04X, pid : 0x%04X device\n", TestParms->vid,
                      TestParms->pid);
            return -1;
        }

        int validSelection = 0;

        do {
            LOG_MSG("Select Interface (1-%u): ", count);
            while (_kbhit()) {
                _getch();
            }

            selection = (CHAR)_getche() - (UCHAR)'0';
            fprintf(stderr, "\n");
            if (selection == 'q' - '0') {
                return -1;
            }
            if (selection > 0 && selection <= count) {
                count = 0;
                while (LstK_MoveNext(TestParms->DeviceList, &deviceInfo) && ++count != selection) {
                    LibK_SetContext(deviceInfo, KLIB_HANDLE_TYPE_LSTINFOK,
                                    (KLIB_USER_CONTEXT)TRUE);
                }

                if (!deviceInfo) {
                    LOGERR0("Unknown selection\n");
                    continue;
                }

                TestParms->SelectedDeviceProfile = deviceInfo;
                TestParms->vid = (int)strtol(deviceInfo->DeviceID + 8, NULL, 16);
                TestParms->pid = (int)strtol(deviceInfo->DeviceID + 17, NULL, 16);

                LOG_MSG("vid : 0x%04X, pid : 0x%04X\n", TestParms->vid, TestParms->pid);
                validSelection = 1;
            } else {
                LOG_MSG("Invalid selection. Please select a number between 1 and %u\n", count);
                LOGMSG0("Press 'q' to quit\n");
            }
        } while (!validSelection);

        return ERROR_SUCCESS;
    }

    return -1;
}

int GetEndpointFromList(PUVPERF_PARAM TestParms) {
    KLST_DEVINFO_HANDLE deviceInfo = TestParms->SelectedDeviceProfile;
    WINUSB_PIPE_INFORMATION_EX pipeInfo[32];
    int userChoice;
    UCHAR pipeIndex;
    TestParms->PipeInformation;
    int hasIsoEndpoints = 0;
    int hasZeroMaxPacketEndpoints = 0;

    int validInput = 0; // Flag to check for valid input
    do {
        while (LstK_MoveNext(TestParms->DeviceList, &deviceInfo)) {
            if (!LibK_LoadDriverAPI(&K, deviceInfo->DriverID)) {
                WinError(GetLastError());
                LOG_ERROR("Cannot load driver API for %s\n", GetDrvIdString(deviceInfo->DriverID));
                continue;
            }

            if (!K.Init(&TestParms->InterfaceHandle, deviceInfo)) {
                WinError(GetLastError());
                LOG_ERROR("Cannot initialize device interface for %s\n", deviceInfo->DevicePath);
                continue;
            }

            UCHAR altSetting = 0;

            LOG_VERBOSE("Device %s initialized successfully.\n", deviceInfo->DevicePath);
            while (K.QueryInterfaceSettings(TestParms->InterfaceHandle, altSetting,
                                            &TestParms->InterfaceDescriptor)) {
                LOG_VERBOSE("Interface %d: Checking pipes...\n",
                            TestParms->InterfaceDescriptor.bInterfaceNumber);
                pipeIndex = 0;

                while (K.QueryPipeEx(TestParms->InterfaceHandle, altSetting, pipeIndex,
                                     &pipeInfo[pipeIndex])) {
                    LOG_MSG("Pipe %d: Type : %11s, %3s\n", pipeIndex + 1,
                            EndpointTypeDisplayString[pipeInfo[pipeIndex].PipeType],
                            (pipeInfo[pipeIndex].PipeId & USB_ENDPOINT_DIRECTION_MASK) ? "in"
                                                                                       : "out");
                    pipeIndex++;
                }

                if (pipeIndex == 0) {
                    altSetting++;
                    continue;
                }

                LOG_MSG("Enter the number of the pipe to use for transfer (1-%d), 'Q' to quit: ",
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

                memcpy(TestParms->PipeInformation, pipeInfo, sizeof(pipeInfo));

                if (TestParms->PipeInformation[userChoice - 1].PipeType == UsbdPipeTypeIsochronous)
                    hasIsoEndpoints++;

                if (!TestParms->PipeInformation[userChoice - 1].MaximumPacketSize)
                    hasZeroMaxPacketEndpoints++;

                validInput = 1;
                break;
            }
            if (pipeIndex == 0) {
                LOGERR0("No pipes available.\n");
            }
            if (validInput == 1) {
                break;
            }
        }
    } while (!validInput);

    if (((TestParms->intf == -1) ||
         (TestParms->intf == TestParms->InterfaceDescriptor.bInterfaceNumber)) &&
        ((TestParms->altf == -1) ||
         (TestParms->altf == TestParms->InterfaceDescriptor.bAlternateSetting))) {
        if (TestParms->altf == -1 && hasIsoEndpoints && hasZeroMaxPacketEndpoints) {
            LOG_MSG("skipping interface %02X:%02X. zero-length iso endpoints exist.\n",
                    TestParms->InterfaceDescriptor.bInterfaceNumber,
                    TestParms->InterfaceDescriptor.bAlternateSetting);
        } else {
            TestParms->intf = TestParms->InterfaceDescriptor.bInterfaceNumber;
            TestParms->altf = TestParms->InterfaceDescriptor.bAlternateSetting;
            TestParms->SelectedDeviceProfile = deviceInfo;

            if (hasIsoEndpoints && TestParms->bufferCount == 1)
                TestParms->bufferCount++;

            TestParms->DefaultAltSetting = 0;
            K.GetCurrentAlternateSetting(TestParms->InterfaceHandle, &TestParms->DefaultAltSetting);
            if (!K.SetCurrentAlternateSetting(TestParms->InterfaceHandle,
                                              TestParms->InterfaceDescriptor.bAlternateSetting)) {
                LOG_ERROR("can not find alt interface %02X\n", TestParms->altf);
                return FALSE;
            }
        }
    }

    TestParms->endpoint = (int)(pipeInfo[userChoice - 1].PipeId);
    TestParms->TestType =
        (pipeInfo[userChoice - 1].PipeId & USB_ENDPOINT_DIRECTION_MASK) ? TestTypeIn : TestTypeOut;

    return 1;
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
            LibK_SetContext(deviceInfo, KLIB_HANDLE_TYPE_LSTINFOK, (KLIB_USER_CONTEXT)TRUE);
        }
    }

    return ERROR_SUCCESS;
}

int ParseArgs(PUVPERF_PARAM TestParms, int argc, char **argv) {
    int status = 0;

    int c;
    while ((c = getopt(argc, argv, "Vv:p:i:a:e:m:t:fc:b:l:w:r:SRWL")) != -1) {
        switch (c) {
        case 'V':
            verbose = TRUE;
            break;
        case 'v':
            TestParms->vid = strtol(optarg, NULL, 0);
            TestParms->list = FALSE;
            break;
        case 'p':
            TestParms->pid = strtol(optarg, NULL, 0);
            TestParms->list = FALSE;
            break;
        case 'i':
            TestParms->intf = strtol(optarg, NULL, 0);
            break;
        case 'a':
            TestParms->altf = strtol(optarg, NULL, 0);
            break;
        case 'e':
            TestParms->endpoint = strtol(optarg, NULL, 0);
            TestParms->list = FALSE;
            break;
        case 'm':
            TestParms->TransferMode =
                (strtol(optarg, NULL, 0) ? TRANSFER_MODE_ASYNC : TRANSFER_MODE_SYNC);
            break;
        case 'T':
            TestParms->Timer = strtol(optarg, NULL, 0);
            break;
        case 't':
            TestParms->timeout = strtol(optarg, NULL, 0);
            break;
        case 'f':
            TestParms->fileIO = TRUE;
            break;
        case 'c':
            TestParms->bufferCount = strtol(optarg, NULL, 0);
            if (TestParms->bufferCount > 1) {
                TestParms->TransferMode = TRANSFER_MODE_ASYNC;
            }
            break;
        case 'b':
            TestParms->bufferlength = strtol(optarg, NULL, 0);
            TestParms->readlenth = max(TestParms->readlenth, TestParms->bufferlength);
            TestParms->writelength = max(TestParms->writelength, TestParms->bufferlength);
            break;
        case 'l':
            TestParms->readlenth = strtol(optarg, NULL, 0);
            TestParms->readlenth = max(TestParms->readlenth, TestParms->bufferlength);
            break;
        case 'w':
            TestParms->writelength = strtol(optarg, NULL, 0);
            TestParms->writelength = max(TestParms->writelength, TestParms->bufferlength);
            break;
        case 'r':
            TestParms->repeat = strtol(optarg, NULL, 0);
            break;
        case 'S':
            TestParms->ShowTransfer = TRUE;
            break;
        case 'R':
            TestParms->TestType = TestTypeIn;
            break;
        case 'W':
            TestParms->TestType = TestTypeOut;
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

    if (TestParms->vid == -1 || TestParms->pid == -1 || TestParms->endpoint == -1 ||
        TestParms->list == FALSE || status == -1) {
        LOGERR0("Invalid argument\n");
        status = -1;
    }

    if (optind < argc) {
        printf("Non-option arguments: ");
        while (optind < argc)
            printf("%s ", argv[optind++]);
        printf("\n");
    }

    return status;
}

void ShowParms(PUVPERF_TRANSFER_PARAM transferParam) {
    PUVPERF_PARAM TestParms = transferParam->TestParms;

    if (!TestParms)
        return;

    uint8_t mc = (transferParam->Ep.MaximumPacketSize / 1024 == 0
                      ? 0
                      : (transferParam->Ep.MaximumPacketSize / 1024) - 1);
    uint32_t mps = (mc == 0) ? transferParam->Ep.MaximumPacketSize : (mc << 11) + 1024;

    LOG_MSG("\tDriver                   :  %s\n",
            GetDrvIdString(TestParms->SelectedDeviceProfile->DriverID));
    LOG_MSG("\tVID:                     :  0x%04X\n", TestParms->vid);
    LOG_MSG("\tPID:                     :  0x%04X\n", TestParms->pid);
    LOG_MSG("\tInterface:               :  %d\n", TestParms->intf);
    LOG_MSG("\tAlt Interface:           :  %d\n", TestParms->altf);
    LOG_MSG("\tTimeout:                 :  %d\n", TestParms->timeout);
    LOG_MSG("\tBuffer Length            :  %d\n", TestParms->bufferlength);
    LOG_MSG("\tRepeat:                  :  %d\n", TestParms->repeat);
    LOG_MSG("------------------------------------------------\n");
    LOG_MSG("Endpoint addr              :  0x%02X\n", transferParam->Ep.PipeId);
    LOG_MSG("Endpoint Type              :  %s, %s\n",
            EndpointTypeDisplayString[transferParam->Ep.PipeType],
            (transferParam->Ep.PipeId & USB_ENDPOINT_DIRECTION_MASK) ? "In" : "Out");
    LOG_MSG("Endpoint MC                :  %d\n", mc);
    LOG_MSG("Endpoint Max Packet Size   :  %d\n", mps);
    LOG_MSG("Endpoint Interval          :  %d\n", transferParam->Ep.Interval);

    LOG_MSG("\n");
}

void FileIOOpen(PUVPERF_PARAM TestParms) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    strftime(TestParms->LogFileName, MAX_PATH - 1, "uvperf_log_%Y%m%d_%H%M%S.txt", t);
    TestParms->LogFileName[MAX_PATH - 1] = '\0';

    if (TestParms->fileIO) {
        TestParms->LogFile =
            CreateFile(TestParms->LogFileName, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_ALWAYS, // Open the file if it exists; otherwise, create it
                       FILE_ATTRIBUTE_NORMAL, NULL);

        if (TestParms->LogFile == INVALID_HANDLE_VALUE) {
            LOG_ERROR("failed opening %s\n", TestParms->LogFileName);
            TestParms->fileIO = FALSE;
        }
    }
}

void FileIOLog(PUVPERF_TRANSFER_PARAM transferParam) {
    PUVPERF_PARAM TestParms = transferParam->TestParms;

    if (!TestParms->fileIO) {
        return;
    }

    freopen(TestParms->LogFileName, "a+", stdout);
    freopen(TestParms->LogFileName, "a+", stderr);

    ShowParms(transferParam);
}

void FileIOClose(PUVPERF_PARAM TestParms) {
    if (TestParms->fileIO) {
        if (TestParms->LogFile != INVALID_HANDLE_VALUE) {
            fclose(stdout);
            CloseHandle(TestParms->LogFile);
            TestParms->LogFile = INVALID_HANDLE_VALUE;
        }
    }
}
