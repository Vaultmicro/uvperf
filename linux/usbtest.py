import usb.core
import usb.util
import time
import asyncio
import sys
import usb.backend.libusb1

# libusb 백엔드 가져오기
backend = usb.backend.libusb1.get_backend()

#using winusb(6.1) driver

dev = usb.core.find(idVendor=0xAA, idProduct=0xBB )
assert dev is not None

# print(dev)
dev.set_configuration()
cfg = dev.get_active_configuration()
assert cfg is not None

# # find Iso EP included  interface from configuration
# intf = usb.util.find_descriptor(
#     cfg,
#     custom_match = \
#     lambda i: \
#         usb.util.find_descriptor(
#             i,
#             custom_match = \
#             lambda e: \
#                 usb.util.endpoint_type(e.bmAttributes) == \
#                 usb.util.ENDPOINT_TYPE_ISO))

# assert intf is not None
# print(intf)

intf = usb.util.find_descriptor(
    cfg,
    custom_match = \
    lambda i: \
        usb.util.find_descriptor(
            i,
            custom_match = \
            lambda e: \
                usb.util.endpoint_type(e.bmAttributes) == \
                usb.util.ENDPOINT_TYPE_BULK))


# epIsoIn = usb.util.find_descriptor(
#     intf,
#     custom_match = \
#     lambda e: \
#         usb.util.endpoint_direction(e.bEndpointAddress) == \
#         usb.util.ENDPOINT_IN and \
#         usb.util.endpoint_type(e.bmAttributes) == \
#         usb.util.ENDPOINT_TYPE_ISO)

# assert epIsoIn is not None
# print("epIsoIn:", epIsoIn)

epBulkIn = usb.util.find_descriptor(
    intf,
    custom_match = \
    lambda e: \
        usb.util.endpoint_direction(e.bEndpointAddress) == \
        usb.util.ENDPOINT_IN and \
        usb.util.endpoint_type(e.bmAttributes) == \
        usb.util.ENDPOINT_TYPE_BULK)

assert epBulkIn is not None
print("epBulkIn:", epBulkIn)

async def bulk_test():
    try:
        while True:
            intf.set_altsetting()
            result = epBulkIn.read(10000, timeout=2000)
            rlen = len(result)
            print(f"result len : {rlen}")
            await asyncio.sleep(0)
    except usb.core.USBError as e:
        print(f"USBError: {e}")
    except asyncio.CancelledError:
        print("task is cancel requested")
        raise

# TIMEOUT = 1100
# ISO_INTERVAL_TRANSFER = 2**(epIsoIn.bInterval-1)
# MSEC_TRANSFER_COUNT = int(1000/(ISO_INTERVAL_TRANSFER*125))
# print("transfer count per 1 msec:", ISO_INTERVAL_TRANSFER, MSEC_TRANSFER_COUNT)

# # get additional Transaction Oppotunities from epIsoIn.wMaxPacketSize
# MC = (epIsoIn.wMaxPacketSize&0x1800)>>11
# print("MC:", MC)
# # MC = 0: 1 Transaction Opportunity
# # MC = 1: 2 Transaction Opportunities
# # MC = 2: 3 Transaction Opportunities

# TOTAL_TRANSFER_SIZE = MSEC_TRANSFER_COUNT*(epIsoIn.wMaxPacketSize&0x7ff)*(MC+1)*1000
# print("maxpacketsize:", epIsoIn.wMaxPacketSize,"/",(epIsoIn.wMaxPacketSize&0x7ff))
# test_step = [TOTAL_TRANSFER_SIZE]

# async def iso_test():
#     for step in test_step:
#         print("Test Step:", step)
        
#         for i in range(0, 1000):
#             try:
#                 intf.set_altsetting()
#                 result = epIsoIn.read(8000, timeout=TIMEOUT)
#                 rlen = len(result)
#                 print(f"result len : {rlen} ,{rlen/step*100}%")
#                 await asyncio.sleep(0)
                
#                 # flush read buffer
#             except usb.core.USBError as e:
#                 print(f"USBError: {e} ntime {i}")
#                 break
#             except asyncio.CancelledError:
#                 print("task is cancel requested")
#                 raise
#     print("test done")


async def keyboard_input():
    await asyncio.get_event_loop().run_in_executor(None, input, "종료하려면 아무 키나 누르세요\n")


async def main():
    test_task = asyncio.create_task(bulk_test())
    input_task = asyncio.create_task(keyboard_input())

    done, pending = await asyncio.wait(
        {input_task , test_task },
        return_when=asyncio.FIRST_COMPLETED  # 먼저 완료되는 태스크가 있으면 반환
    )

    for task in pending:  # 완료되지 않은 나머지 태스크들을 취소합니다.
        task.cancel()

    if test_task in done:
        print("테스트 작업이 먼저 완료되었습니다.")
    else:
        print("키보드 입력으로 종료합니다.")

    exit()


asyncio.run(main())