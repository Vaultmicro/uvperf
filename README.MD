# 실행방법
1. include "libusb.h"
2. gcc로 컴파일 할 경우 링크 경로 지정 -> -L"{libusb.dll 경로}" -l"usb-1.0"

# 문제점
1. uvperf_iso_in.c 의 경우 I/O error 87 오류 해결 못함
2. usb_iso_py 의 경우
    * read 전에 set_altsetting() 매 번 초기화를 해야하는 문제가 있음
    * buffer size가 커지면 timeout 오류 출력

# 원인과 해결방법
1. 원인을 찾지 못함  

2.
    * 정확한 원인을 찾지 못함
    * ISO transfer의 buffersize는 wMaxPacketSize * frame 당 packet 수 즉, wMaxPacketSize * 8 의 배수가 되어야 함  
    하지만 너무 큰 buffer size를 할당한다면 timeout error + USBD_STATUS_BAD_START_FRAME packet을 받음  
    buffer 사이즈의 문제인 것으로 생각되나 정확한 원인을 찾는 중