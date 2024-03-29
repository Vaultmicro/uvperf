import usb.core
import usb.util
import time
import datetime

#device setting
dev = usb.core.find(idVendor=0x0526, idProduct=0xa4a3)
assert dev is not None

#configuration setting
dev.set_configuration()
cfg = dev.get_active_configuration()
intf = cfg[(0,0)]

ep_in = usb.util.find_descriptor(
intf,
custom_match = \
lambda e: \
    usb.util.endpoint_direction(e.bEndpointAddress) == \
    usb.util.ENDPOINT_IN)

print(ep_in)
# send get descritor on control request
result = dev.ctrl_transfer(0x80, 0x06, 0x0100, 0x0000, 0x40)

# reading data
print(f"Reading")
total_read = 0
# start = datetime.datetime.now()
TOALSIZE = 1000*2000
# try:
data = ep_in.read(TOALSIZE, timeout=2000)
total_read += len(data)
# except usb.core.USBError as e:
#     print(f"USBError on initial read: {e}")

# calculating the communication speed
# end = datetime.datetime.now()
# elapsed_time = end - start
# communication_speed = total_read / elapsed_time.total_seconds()
# print(f"Communication speed: {communication_speed} bytes/second")