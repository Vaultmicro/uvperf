# Goal of uvperf
USBdevice Vault perf 테스트 툴(이하 uvperf)을 사용하여, host<->device의 Bulk In, Bulk Out, Isochronous In transfer들의 speed, accuracy를 파악할 수 있다.

# How to run uvperf
## Install Winusb driver for deivce
1. Using Zadig-2.9.exe (https://zadig.akeo.ie/#google_vignette)
3. If you can't see the deivce, check Option->List All Devices
4. Install Winusb(v6.1...)
## uvperf.c
1. gcc로 컴파일 할 경우 링크 경로 지정 -> -L"{libusbk.dll 경로}" -l"usb"
2. CMake
3. Visual Studio로 컴파일 할 경우 ->  #pragma comment(lib, "libusbk.lib") 추가
## Usage of uvperf

### Usage
```
uvperf
```
argument 없이 uvperf만 실행 할 경우
![uvperf_usage](https://github.com/Vaultmicro/uvperf/assets/162442453/f68bec64-9205-4ab2-8c8f-50e3f553912d)
위 사진과 같이 usage를 볼 수 있다

### List
```
uvperf -b | -l | -w Buffersize
```
then, you can select device and endpoint
![image](https://github.com/Vaultmicro/uvperf/assets/162442453/4129d869-888e-473a-90ef-979128b798c8)

( recommend type with buffer size and timeout with commands )

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



### Transfer Speed

In the middle of excution, press "q" or "Q" then, show the log average Bandwidth ( Mpbs ), and total transfer

### Known Issue
* Multi transfer 미지원

