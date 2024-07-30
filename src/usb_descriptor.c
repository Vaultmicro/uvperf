#include "usb_descriptor.h"
#include "log.h"

void ShowMenu() {
    LOG_MSG("Press e: Endpoint descriptor\n");
    LOG_MSG("Press i: Interface descriptor\n");
    LOG_MSG("Press c: Configuration descriptor\n");
    LOG_MSG("Press d: Device descriptor\n");
    LOG_MSG("Press q: Quit\n");
    LOG_MSG("Press t or Enter: Transfer\n");

}

void ShowEndpointDescriptor(libusb_device *dev, int interface_index, int altinterface_index, int endpoint_index) {
    struct libusb_config_descriptor *config;
    const struct libusb_interface *interface;
    const struct libusb_interface_descriptor *altsetting;
    const struct libusb_endpoint_descriptor *endpoint;

    int r = libusb_get_active_config_descriptor(dev, &config);
    if (r < 0) {
        LOGERR0("Failed to get configuration descriptor\n");
        return;
    }

    interface = &config->interface[interface_index];
    altsetting = &interface->altsetting[altinterface_index];
    endpoint = &altsetting->endpoint[endpoint_index];

    LOG_MSG("Endpoint Descriptor:\n");
    LOG_MSG("  bLength: %d\n", endpoint->bLength);
    LOG_MSG("  bDescriptorType: %d\n", endpoint->bDescriptorType);
    LOG_MSG("  bEndpointAddress: %02X\n", endpoint->bEndpointAddress);
    LOG_MSG("  bmAttributes: %02X\n", endpoint->bmAttributes);
    LOG_MSG("  wMaxPacketSize: %d\n", endpoint->wMaxPacketSize);
    LOG_MSG("  bInterval: %d\n", endpoint->bInterval);

    libusb_free_config_descriptor(config);
}

void ShowInterfaceDescriptor(libusb_device *dev, int interface_index, int altinterface_index) {
    struct libusb_config_descriptor *config;
    const struct libusb_interface *interface;
    const struct libusb_interface_descriptor *altsetting;

    int r = libusb_get_active_config_descriptor(dev, &config);
    if (r < 0) {
        LOGERR0("Failed to get configuration descriptor\n");
        return;
    }

    interface = &config->interface[interface_index];
    altsetting = &interface->altsetting[altinterface_index];

    LOG_MSG("Interface Descriptor:\n");
    LOG_MSG("  bLength: %d\n", altsetting->bLength);
    LOG_MSG("  bDescriptorType: %d\n", altsetting->bDescriptorType);
    LOG_MSG("  bInterfaceNumber: %d\n", altsetting->bInterfaceNumber);
    LOG_MSG("  bAlternateSetting: %d\n", altsetting->bAlternateSetting);
    LOG_MSG("  bNumEndpoints: %d\n", altsetting->bNumEndpoints);
    LOG_MSG("  bInterfaceClass: %d\n", altsetting->bInterfaceClass);
    LOG_MSG("  bInterfaceSubClass: %d\n", altsetting->bInterfaceSubClass);
    LOG_MSG("  bInterfaceProtocol: %d\n", altsetting->bInterfaceProtocol);
    LOG_MSG("  iInterface: %d\n", altsetting->iInterface);

    libusb_free_config_descriptor(config);
}

void ShowConfigurationDescriptor(libusb_device *dev) {
    struct libusb_config_descriptor *config;

    int r = libusb_get_active_config_descriptor(dev, &config);
    if (r < 0) {
        LOGERR0("Failed to get configuration descriptor\n");
        return;
    }

    LOG_MSG("Configuration Descriptor:\n");
    LOG_MSG("  bLength: %d\n", config->bLength);
    LOG_MSG("  bDescriptorType: %d\n", config->bDescriptorType);
    LOG_MSG("  wTotalLength: %d\n", config->wTotalLength);
    LOG_MSG("  bNumInterfaces: %d\n", config->bNumInterfaces);
    LOG_MSG("  bConfigurationValue: %d\n", config->bConfigurationValue);
    LOG_MSG("  iConfiguration: %d\n", config->iConfiguration);
    LOG_MSG("  bmAttributes: %02X\n", config->bmAttributes);
    LOG_MSG("  MaxPower: %d\n", config->MaxPower);

    libusb_free_config_descriptor(config);
}

void ShowDeviceDescriptor(libusb_device *dev) {
    struct libusb_device_descriptor desc;
    int r = libusb_get_device_descriptor(dev, &desc);
    if (r < 0) {
        LOGERR0("Failed to get device descriptor\n");
        return;
    }

    LOG_MSG("Device Descriptor:\n");
    LOG_MSG("  bLength: %d\n", desc.bLength);
    LOG_MSG("  bDescriptorType: %d\n", desc.bDescriptorType);
    LOG_MSG("  bcdUSB: %04X\n", desc.bcdUSB);
    LOG_MSG("  bDeviceClass: %d\n", desc.bDeviceClass);
    LOG_MSG("  bDeviceSubClass: %d\n", desc.bDeviceSubClass);
    LOG_MSG("  bDeviceProtocol: %d\n", desc.bDeviceProtocol);
    LOG_MSG("  bMaxPacketSize0: %d\n", desc.bMaxPacketSize0);
    LOG_MSG("  idVendor: %04X\n", desc.idVendor);
    LOG_MSG("  idProduct: %04X\n", desc.idProduct);
    LOG_MSG("  bcdDevice: %04X\n", desc.bcdDevice);
    LOG_MSG("  iManufacturer: %d\n", desc.iManufacturer);
    LOG_MSG("  iProduct: %d\n", desc.iProduct);
    LOG_MSG("  iSerialNumber: %d\n", desc.iSerialNumber);
    LOG_MSG("  bNumConfigurations: %d\n", desc.bNumConfigurations);
}



void PerformTransfer() {
    LOG_MSG("Performing transfer...\n");
}


void ShowDeviceInterfaces(libusb_device *dev) {
    struct libusb_config_descriptor *config;
    int r = libusb_get_active_config_descriptor(dev, &config);
    if (r < 0) {
        LOGERR0("Failed to get configuration descriptor\n");
        return;
    }

    LOG_MSG("\n");
    LOG_MSG("Number of interfaces: %d\n", config->bNumInterfaces);
    for (int i = 0; i < config->bNumInterfaces; i++) {
        const struct libusb_interface *interface = &config->interface[i];
        LOG_MSG("Interface %d has %d alternate settings\n", i, interface->num_altsetting);
        for (int j = 0; j < interface->num_altsetting; j++) {
            const struct libusb_interface_descriptor *altsetting = &interface->altsetting[j];
            LOG_MSG("  Alternate setting %d: Interface number: %d, Number of endpoints: %d\n",
                    altsetting->bAlternateSetting, altsetting->bInterfaceNumber, altsetting->bNumEndpoints);
        }
    }

    libusb_free_config_descriptor(config);
}
