import usb.core
import usb.util
import time
import datetime

#device setting
dev = usb.core.find(idVendor=0x0525, idProduct=0xa4a3)
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
    usb.util.ENDPOINT_OUT)

if not ep_out:
    raise ValueError("Can't find EndPoint OUT")

ep_in = usb.util.find_descriptor(
intf,
custom_match = \
lambda e: \
    usb.util.endpoint_direction(e.bEndpointAddress) == \
    usb.util.ENDPOINT_IN)

print(ep_out)
print(ep_in)

#reading data
# print(f"Reading")
# start = datetime.datetime.now()
# try:
#     data = ep_in.read(3000000, timeout=3000)
# except usb.core.USBError as e:
#     print(f"USBError on initial read: {e}")

#writing data
data_out = b'0000'
print(f"Writing '{data_out}'\n")

try:
    ep_out.write(data_out, timeout=3000)
    print("Done")

    # Data written to the device, now print each byte in hex format
    print("Data in hex:", " ".join([f"{byte:02x}" for byte in data_out]))
except usb.core.USBError as e:
    print(f"USBError on write: {e}")
    
#TODO : calculating the communication speed