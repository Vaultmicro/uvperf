# 실행방법
## uvperf.c
1. gcc로 컴파일 할 경우 링크 경로 지정 -> -L"{libusbk.dll 경로}" -l"usb"
2. CMake
3. Visual Studio로 컴파일 할 경우 ->  #pragma comment(lib, "libusbk.lib") 추가
## UVTest 사용법

### CLI

uvperf -v VID -p PID -i INTERFACE -a AltInterface -e ENDPOINT -m TRANSFERMODE 
            -T TIMER -t TIMEOUT -f FileIO -b BUFFERCOUNT -l READLENGTH -w WRITELENGTH -r REPEAT -S
            Example

*   -v VID<br/>            USB Vendor ID
*   -p PID<br/>            USB Product ID
*   -i INTERFACE<br/>      USB Interface
*   -a AltInterface<br/>   USB Alternate Interface
*   -e ENDPOINT<br/>       USB Endpoint
*   -m TRANSFERMODE<br/>   0 = Async, 1 = Sync
*   -T TIMER<br/>          Timer in seconds
*   -t TIMEOUT<br/>        USB Transfer Timeout
*   -f FILEIO<br/>         Use file I/O, default : FALSE
*   -b BUFFERCOUNT<br/>    Number of buffers to use
*   -l READLENGTH<br/>     Length of read transfers
*   -w WRITELENGTH<br/>    Length of write transfers
*   -r REPEAT<br/>         Number of transfers to perform
*   -S <br/>               Show transfer data, default : FALSE
```
uvperf -v 0x1004 -p 0xa000 -i 0 -a 0 -e 0x81 -m 0 -t 1000 -l 1024 -r 1000 -R
```
This will perform 1000 bulk transfers of 1024 bytes to endpoint 0x81 on interface 0, alternate setting 0 of a device with VID 0x1004 and PID 0xA000 The transfers will have a timeout of 1000ms.

### List
```
uvperf -l | -w Buffersize
```
then, you can select device and endpoint
( recommend type with buffer size and timeout with commands )

### Bandwitdh

In the middle of excution, press "q" or "Q" then, show the log average Bandwidth ( Mpbs ), total transfer
