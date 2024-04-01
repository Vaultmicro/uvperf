/*!********************************************************************
*   This file is part of the libusbK project.
*   libusbK is free software: you can redistribute it and/or modify
*   it under the terms of the GNU Lesser General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   libusbK is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU Lesser General Public License for more details.
*
*   You should have received a copy of the GNU Lesser General Public License
*   along with libusbK.  If not, see <http://www.gnu.org/licenses/>.
*
*   uvperf.c
*   This is a simple utility to test the performance of USB transfers.
*   It is designed to be used with the libusbK driver.
*   The utility will perform a series of transfers to the specified endpoint
*   and report the results.
*
*   Usage:
*   uvperf -vVID -pPID -iINTERFACE -aAltInterface -eENDPOINT -tTRANSFER -oTIMEOUT -rlLENGTH -wlLENGTH -rREPEAT -dDELAY -xVERBOSE
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
*   -dDELAY         Delay between transfers
*   -xVERBOSE       Verbose output
*
*   Example:
*   uvperf -v0x0451 -p0x2046 -i0 -a0 -e0x81 -t1 -o1000 -l1024 -r1000 -d0 -x1
*
*   This will perform 1000 bulk transfers of 1024 bytes to endpoint 0x81
*   on interface 0, alternate setting 0 of a device with VID 0x0451 and PID 0x2046.
*   The transfers will have a timeout of 1000ms and there will be no delay between transfers.
*   Verbose output will be displayed.
*
********************************************************************!*/
#include <window.h>
#include <stdio.h>
#include <stdlib.h>

#include "libusbk/libusbk.h"
#include "libusbk/lusbk_version.h"
#include "libusbk/libusbk_shared.h"

#define LOG(LogTypeString, format, ...) printf("%s[" __FUNCTION__"] "format, LogTypeString, ##__VA_ARGS__)
#define LOG_NO_FN(LogTypeString, format, ...) printf("%s"format "%s", LogTypeString, ##__VA_ARGS__"")

#define LOG_ERROR(format, ...) LOG("ERROR: ", format, ##__VA_ARGS__)
#define LOG_WARNING(format, ...) LOG("WARNING: ", format, ##__VA_ARGS__)
#define LOG_MSG(format, ...) LOG_NO_FN("", format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) LOG("DEBUG: ", format, ##__VA_ARGS__)

#define LOGERR0(message) LOG_ERROR("%s\n", message)
#define LOGWAR0(message) LOG_WARNING("%s\n", message)
#define LOGMSG0(message) LOG_MSG("%s\n", message)
#define LOGDBG0(message) LOG_DEBUG("%s\n", message)

KUSB_DRIVER_API K;
CRITICAL_SECTION DisplayCriticalSection;

typedef struct _PARAM{
    int vid;
    int pid;
    int interface;
    int altface;
    int endpoint;
    int transfer;       // 0 = isochronous, 1 = bulk
    int timeout;
    int readlenth;
    int writelength;
    int repeat;
    int delay;
    int verbose;
} PARAM, *PPARAM;

typedef struct _BENMARK_BUFFER{
    int Length;
    unsigned char* Buffer;
    struct _BENMARK_BUFFER* Next;
} BENMARK_BUFFER, *PBENMARK_BUFFER;

typedef struct _BENMARK_TRANSFER_PARAM{
    int TransferType;
    int Endpoint;
    int Timeout;
    int Length;
    int Repeat;
    int Delay;
    int Verbose;
    PBENMARK_BUFFER Buffer;
    PBENMARK_BUFFER VerifyBuffer;
    PBENMARK_BUFFER VerifyList;
    FILE* LogFile;
} BENMARK_TRANSFER_PARAM, *PBENMARK_TRANSFER_PARAM;

int ParseArgs(PARAM TestParms, int argc, char** argv);
void ShowTestParms(PARAM TestParms);

int ParseArgs(PARAM TestParms, int argc, char** argv){
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
                    TestParms.vid = value;
                    break;
                case 'p':
                    TestParms.pid = value;
                    break;
                case 'i':
                    TestParms.interface = value;
                    break;
                case 'a':
                    TestParms.altface = value;
                    break;
                case 'e':
                    TestParms.endpoint = value;
                    break;
                case 't':
                    TestParms.transfer = value;
                    break;
                case 'o':
                    TestParms.timeout = value;
                    break;
                case 'r':
                    TestParms.repeat = value;
                    break;
                case 'd':
                    TestParms.delay = value;
                    break;
                case 'l':
                    TestParms.readlenth = value;
                    break;
                case 'w':
                    TestParms.writelength = value;
                    break;
                case 'x':
                    TestParms.verbose = value;
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

void ShowTestParms(PARAM TestParms){
    if(!TestParms) return;
    LOGMSG0("Test Parameters:\n");
    LOGMSG0("\tVID:             %04X\n", TestParms.vid);
    LOGMSG0("\tPID:             %04X\n", TestParms.pid);
    LOGMSG0("\tInterface:       %d\n", TestParms.interface);
    LOGMSG0("\tAlt Interface:   %d\n", TestParms.altface);
    LOGMSG0("\tEndpoint:        %02X\n", TestParms.endpoint);
    LOGMSG0("\tTransfer:        %d\n", TestParms.transfer);
    LOGMSG0("\tTimeout:         %d\n", TestParms.timeout);
    LOGMSG0("\tRead Length:     %d\n", TestParms.readlenth);
    LOGMSG0("\tWrite Length:    %d\n", TestParms.writelength);
    LOGMSG0("\tRepeat:          %d\n", TestParms.repeat);
    LOGMSG0("\tDelay:           %d\n", TestParms.delay);
    LOGMSG0("\tVerbose:         %d\n", TestParms.verbose);
    LOGMSG0("\n");
}

int main(int argc, char** argv){
    PARAM TestParms;
    PBENMARK_TRANSFER_PARAM ReadTest = NULL;
    PBENMARK_TRANSFER_PARAM WriteTest = NULL;

    // todo : when argc == 1, print help message
    if(argc == 1){
        // print help message
        LOGERR0("Invalid argument\n");
        return -1;
    }

    if(ParseArgs(TestParms, argc, argv) < 0)
        return -1;
    
Done:
    if(TestParms.InterfaceHandle){
        K.SetAltInterface(
                TestParms.InterfaceHandle,
                TestParms.InterfaceNumber,
                TestParms.AltInterfaceNumber
        );
        K.Free(TestParms.InterfaceHandle);
        TestParms.InterfaceHandle = NULL;
    }

    if(!TestParms.Use_UsbK_Init){
        if(TestParms.DeviceHandle){
            CloseHandle(TestParms.DeviceHandle)
            TestParms.DeviceHandle = NULL;
        }
    }

    if(TestParms.VerifyBuffer){
        PBENMARK_BUFFER verifyBuffer, verifyListTemp;

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
        TestParms.REadLogFile = NULL;
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

    if(!TestParns.ListDevicesOnly){
        LOGMSG0("Press any key to exit\n");
        _getch();
        LOGMSG0("\n");
    }

    return 0;
}