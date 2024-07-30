#include "log.h"
#include "param.h"

void ShowParams(PUVPERF_PARAM TestParams) {
    if (!TestParams)
        return;

    LOG_MSG("\tDriver         :  %s\n", GetDrvIdString(TestParams->SelectedDeviceProfile->DriverID));
    LOG_MSG("\tVID:           :  0x%04X\n", TestParams->vid);
    LOG_MSG("\tPID:           :  0x%04X\n", TestParams->pid);
    LOG_MSG("\tInterface:     :  %d\n", TestParams->intf);
    LOG_MSG("\tAlt Interface: :  %d\n", TestParams->altf);
    LOG_MSG("\tEndpoint:      :  0x%02X\n", TestParams->endpoint);
    LOG_MSG("\tTransfer mode  :  %s\n", TestParams->TransferMode ? "Isochronous" : "Bulk");
    LOG_MSG("\tTimeout:       :  %d\n", TestParams->timeout);
    LOG_MSG("\tRead Length:   :  %d\n", TestParams->readlenth);
    LOG_MSG("\tWrite Length:  :  %d\n", TestParams->writelength);
    LOG_MSG("\tRepeat:        :  %d\n", TestParams->repeat);
    LOG_MSG("\n");
}
 

int GetDeviceParam(PUVPERF_PARAM TestParams) {
    char id[MAX_PATH];
    KLST_DEVINFO_HANDLE deviceInfo = NULL;

    LstK_MoveReset(TestParams->DeviceList);

    while (LstK_MoveNext(TestParams->DeviceList, &deviceInfo)) {
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

        if (TestParams->vid == vid && TestParams->pid == pid) {
            // enabled
            LibK_SetContext(deviceInfo, KLIB_HANDLE_TYPE_LSTINFOK, (KLIB_USER_CONTEXT)TRUE);
        }
    }

    return ERROR_SUCCESS;
}

void SetParamsDefaults(PUVPERF_PARAM TestParms) {
    memset(TestParms, 0, sizeof(*TestParms));

    TestParms->vid = 0x0000;
    TestParms->pid = 0x0000;
    TestParms->intf = -1;
    TestParms->altf = -1;
    TestParms->endpoint = 0x00;
    TestParms->TransferMode = TRANSFER_MODE_SYNC;
    TestParms->TestType = TestTypeIn;
    TestParms->Timer = 0;
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


int GetDeviceInfoFromList(PUVPERF_PARAM TestParams) {
    UCHAR selection;
    UCHAR count = 0;
    KLST_DEVINFO_HANDLE deviceInfo = NULL;

    LstK_MoveReset(TestParams->DeviceList);

    if (TestParams->listDevicesOnly) {
        while (LstK_MoveNext(TestParams->DeviceList, &deviceInfo)) {
            count++;
            LOG_MSG("%02u. %s (%s) [%s]\n", count, deviceInfo->DeviceDesc, deviceInfo->DeviceID,
                    GetDrvIdString(deviceInfo->DriverID));
        }

        return ERROR_SUCCESS;
    } else {
        while (LstK_MoveNext(TestParams->DeviceList, &deviceInfo) && count < 9) {
            LOG_MSG("%u. %s (%s) [%s]\n", count + 1, deviceInfo->DeviceDesc, deviceInfo->DeviceID,
                    GetDrvIdString(deviceInfo->DriverID));
            count++;

            // enabled
            LibK_SetContext(deviceInfo, KLIB_HANDLE_TYPE_LSTINFOK, (KLIB_USER_CONTEXT)TRUE);
        }

        if (!count) {
            LOG_ERROR("can not find vid : 0x%04X, pid : 0x%04X device\n", TestParams->vid,
                      TestParams->pid);
            return -1;
        }

        int validSelection = 0;

        do {
            LOG_MSG("Select device (1-%u): ", count);
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
                while (LstK_MoveNext(TestParams->DeviceList, &deviceInfo) && ++count != selection) {
                    // disabled
                    LibK_SetContext(deviceInfo, KLIB_HANDLE_TYPE_LSTINFOK,
                                    (KLIB_USER_CONTEXT)FALSE);
                }

                if (!deviceInfo) {
                    LOGERR0("Unknown selection\n");
                    continue;
                }

                TestParams->SelectedDeviceProfile = deviceInfo;
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
