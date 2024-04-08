import usb.core
import usb.util
import time
import datetime

#device setting
dev = usb.core.find(idVendor=0x1004, idProduct=0x61a1)
assert dev is not None

#configuration setting
dev.set_configuration()
cfg = dev.get_active_configuration()
intf = cfg[(0,0)]

ep_out = usb.util.find_descriptor(
    intf,
    custom_match = \
    lambda e: \
        usb.util.endpoint_direction(e.bEndpointAddress) == \
        usb.util.ENDPOINT_OUT and \
        usb.util.endpoint_type(e.bmAttributes) == \
        usb.util.ENDPOINT_TYPE_BULK
)

if not ep_out:
    raise ValueError("Can't find EndPoint OUT")

ep_in = usb.util.find_descriptor(
    intf,
    custom_match = \
    lambda e: \
        usb.util.endpoint_direction(e.bEndpointAddress) == \
        usb.util.ENDPOINT_IN and \
        usb.util.endpoint_type(e.bmAttributes) == \
        usb.util.ENDPOINT_TYPE_BULK
)

# print(ep_out)
print(ep_in)

#reading data
# print(f"Reading")
# start = datetime.datetime.now()
# try:
#     data = ep_in.read(3000000, timeout=3000)
# except usb.core.USBError as e:
#     print(f"USBError on initial read: {e}")

#writing data
TOTAL_TRANSFER_SIZE = 30000

print("Maxpacketsize:", ep_in.wMaxPacketSize)
print("TOTAL_TRANSFER_SIZE:", TOTAL_TRANSFER_SIZE)

test_step = [TOTAL_TRANSFER_SIZE]
# try:
# for step in test_step:
# time.sleep(0.01)
# print("Test Step:", step)

total_bytes = 0
start_time = time.time()
for i in range(0, 5000000):
    try:
        # intf.set_altsetting()
        result = ep_in.read(TOTAL_TRANSFER_SIZE,timeout=3000)
        total_bytes += len(result)
        # print(f"result len {len(result)}")
        if(time.time() - start_time >= 60*5):
            break
    except usb.core.USBError as e:
        print(f"USBError: {e}")
        break
    
end_time = time.time()
elapsed_time = end_time - start_time
if elapsed_time >0:
    transfer_speed = total_bytes/elapsed_time
    print(f"\tTotal Bytes: {total_bytes} Bytes")
    print(f"\tAverage: {transfer_speed/1024:.2f} Byte/sec")
    print(f"\tAverage: {transfer_speed*8/1000/1000:.2f} Mbps")
    print(f"\tElapsed Time: {elapsed_time:.2f} seconds")
else:
    print("Elapsed Time is 0")