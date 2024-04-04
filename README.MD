# 실행방법
1. include "libusb.h"
2. gcc로 컴파일 할 경우 링크 경로 지정 -> -L"{libusbk.dll 경로}" -l"usb"
3. CMake로 컴파일 할 경우
    1. mkdir build
    2. build dir에서 cmake ..
4. Visual Studio로 컴파일 할 경우 ->  #pragma comment(lib, "libusbk.lib") 추가

## UVTest 사용법

uvperf -vVID -pPID -iINTERFACE -aAltInterface -eENDPOINT -mTRANSFERMODE -tTIMEOUT -lREADLENGTH -wWRITELENGTH -rREPEAT -S1 -R|-W|-L
*   -vVID<br/>           USB Vendor ID
*   -pPID<br/>           USB Product ID
*   -iINTERFACE<br>      USB Interface
*   -aAltInterface<br>   USB Alternate Interface
*   -eENDPOINT<br>       USB Endpoint
*   -mTRANSFERMODE<br>   0 = isochronous, 1 = bulk
*   -tTIMEOUT<br>        USB Transfer Timeout
*   -lREADLENGTH<br>     Length of read transfers
*   -wWRITELENGTH<br>    Length of write transfers
*   -rREPEAT<br>         Number of transfers to perform
*   -S 0|1<br>           1 = Show transfer data, defulat = 0\n
*   -R <br>              :ead Test
*   -W <br>              Write Test
*   -L <br>              Loop Test

### Example:

        ./uvperf.exe -v0x1004 -p0x61a1 -i0 -a0 -e0x81 -m1 -t1000 -l1024 -r1000 -R

This will perform 1000 bulk transfers of 1024 bytes to endpoint 0x81
on interface 0, alternate setting 0 of a device with VID 0x1004 and PID 0xA000.
The transfers will have a timeout of 1000ms.