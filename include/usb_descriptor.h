#ifndef USB_DESCRIPTOR_H
#define USB_DESCRIPTOR_H

#include <libusb-1.0/libusb.h>

void ShowMenu();
void ShowEndpointDescriptor(libusb_device *dev, int interface_index, int altinterface_index, int endpoint_index);
void ShowInterfaceDescriptor(libusb_device *dev, int interface_index, int altinterface_index);
void ShowConfigurationDescriptor(libusb_device *dev);
void ShowDeviceDescriptor(libusb_device *dev);
void PerformTransfer();
void ShowDeviceInterfaces(libusb_device *dev);


#endif // USB_DESCRIPTOR_H
