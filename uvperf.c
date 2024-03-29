// #include <window.h>
#include <stdio.h>
#include <stdlib.h>

#include "libusbk/libusbk.h"
#include "libusbk/lusbk_version.h"

typedef struct _PARAM{
    int vid;
    int pid;
    int interface;
    int altsetting;
    int endpoint;
    int transfer;       // 0 = isochronous, 1 = bulk
    int timeout;
    int length;
    int repeat;
    int delay;
    int verbose;
} PARAM, *PPARAM;

int ParseArgs(PARAM testParms, int argc, char** argv){
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
                    testParms.vid = value;
                    break;
                case 'p':
                    testParms.pid = value;
                    break;
                case 'i':
                    testParms.interface = value;
                    break;
                case 'a':
                    testParms.altsetting = value;
                    break;
                case 'e':
                    testParms.endpoint = value;
                    break;
                case 't':
                    testParms.transfer = value;
                    break;
                case 'o':
                    testParms.timeout = value;
                    break;
                case 'l':
                    testParms.length = value;
                    break;
                case 'r':
                    testParms.repeat = value;
                    break;
                case 'd':
                    testParms.delay = value;
                    break;
                case 'x':
                    testParms.verbose = value;
                    break;
                default:
                    status = -1;
                    break;
            }
        }
    }
    return status;
}

int main(int argc, char** argv){
    PARAM testParms;

    ParseArgs(testParms, argc, argv);


    return 0;
}