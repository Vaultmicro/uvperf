import usb.core
import usb.util
import time
import logging

logging.basicConfig(level=logging.DEBUG)
logger = logging.getLogger('usbtest_iso')

#using winusb(6.1) driver

dev = usb.core.find(idVendor=0x1004, idProduct=0x61a1 )
assert dev is not None

# print(dev)
dev.set_configuration()
cfg = dev.get_active_configuration()
assert cfg is not None

# find Iso EP included  interface from configuration
intf = usb.util.find_descriptor(
    cfg,
    custom_match = \
    lambda i: \
        usb.util.find_descriptor(
            i,
            custom_match = \
            lambda e: \
                usb.util.endpoint_type(e.bmAttributes) == \
                usb.util.ENDPOINT_TYPE_ISO))

print(intf)
intf.set_altsetting()


epIsoIn = usb.util.find_descriptor(
    intf,
    custom_match = \
    lambda e: \
        usb.util.endpoint_direction(e.bEndpointAddress) == \
        usb.util.ENDPOINT_IN and \
        usb.util.endpoint_type(e.bmAttributes) == \
        usb.util.ENDPOINT_TYPE_ISO)

assert epIsoIn is not None
print("epIsoIn:", epIsoIn)

TIMEOUT = 1000
ISO_INTERVAL_TRANSFER = 2**(epIsoIn.bInterval-1)
MSEC_TRANSFER_COUNT = int(1000/(ISO_INTERVAL_TRANSFER*125))
print("transfer count per 1 msec:", ISO_INTERVAL_TRANSFER, MSEC_TRANSFER_COUNT)

# get additional Transaction Oppotunities from epIsoIn.wMaxPacketSize
MC = (epIsoIn.wMaxPacketSize&0x1800)>>11
print("MC:", MC)
# MC = 0: 1 Transaction Opportunity
# MC = 1: 2 Transaction Opportunities
# MC = 2: 3 Transaction Opportunities

TOTAL_TRANSFER_SIZE = MSEC_TRANSFER_COUNT*(epIsoIn.wMaxPacketSize&0x7ff)*(MC+1)*100

print("maxpacketsize:", epIsoIn.wMaxPacketSize,"/",(epIsoIn.wMaxPacketSize&0x7ff))
print("TOTAL_TRANSFER_SIZE:", TOTAL_TRANSFER_SIZE)

test_step = [TOTAL_TRANSFER_SIZE]
# try:
# for step in test_step:
# time.sleep(0.01)
# print("Test Step:", step)

total_bytes = 0
start_time = time.time()
for i in range(0, 500000):
    try:
        # intf.set_altsetting()
        result = epIsoIn.read(int(TOTAL_TRANSFER_SIZE*1),timeout=TIMEOUT)        
        end_time = time.time()
        if((end_time - start_time) > 600):
            break
        total_bytes += len(result)
        # if(end_time - start_time % 100):
            # print(f"result len {len(result)}") 
    except usb.core.USBError as e:
        print(f"USBError: {e}")
        break
    
end_time = time.time()
elapsed_time = end_time - start_time
if elapsed_time >0:
    transfer_speed = total_bytes/elapsed_time
    print(f"Elapsed Time: {elapsed_time:.2f} seconds")
    print(f"Transfer Speed: {(transfer_speed/1024/1024):.2f} MB/sec")
    print(f"\tAverage: {transfer_speed*8/1000/1000:.2f} Mbps")
else:
    print("Elapsed Time is 0")
            
        
# except KeyboardInterrupt:
#     end_time = time.time()
#     elapsed_time = end_time - start_time
#     if elapsed_time >0:
#         transfer_speed = total_bytes/elapsed_time
#         print(f"Elapsed Time: {elapsed_time:.2f}")
#         print(f"Transfer Speed: {transfer_speed}")
#     else:
#         print("Elapsed Time is 0")

print("Test Done")
