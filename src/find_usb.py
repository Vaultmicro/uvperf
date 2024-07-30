import usb.core
import usb.util


#Sony vendor id 0x054C product id 0x0E4F
# 특정 USB 장치를 찾기 (054C:0E4F)
dev = usb.core.find(idVendor=0x1004, idProduct=0x61A1)
#dev = usb.core.find(idVendor=0x054C, idProduct=0x0E4F)


if dev is None:
    print("장치를 찾을 수 없습니다.")
else:
    print(f"Device: {dev}")
    for cfg in dev:
        print(f"Configuration: {cfg.bConfigurationValue}")
        for intf in cfg:
            print(f"\tInterface: {intf.bInterfaceNumber}, Alternate Setting: {intf.bAlternateSetting}")
            for ep in intf:
                print(f"\t\tEndpoint Address: {ep.bEndpointAddress}")
