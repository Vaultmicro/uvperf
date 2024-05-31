#include "usb_descriptor.h"
#include <stdio.h>
#include <stdlib.h>

void convert_to_libusbk_endpoint_descriptor(const struct libusb_endpoint_descriptor *endpoint_desc,
                                       USB_ENDPOINT_DESCRIPTOR *usbk_endpoint_desc) {
    usbk_endpoint_desc->bEndpointAddress = endpoint_desc->bEndpointAddress;
    usbk_endpoint_desc->bmAttributes = endpoint_desc->bmAttributes;
    usbk_endpoint_desc->bInterval = endpoint_desc->bInterval;
    usbk_endpoint_desc->bLength = endpoint_desc->bLength;
    usbk_endpoint_desc->bDescriptorType = endpoint_desc->bDescriptorType;
    usbk_endpoint_desc->wMaxPacketSize = endpoint_desc->wMaxPacketSize;
}

int find_endpoint_descriptor(PUVPERF_PARAM TestParams) {
    UCHAR endpoint_addr = (UCHAR)TestParams->endpoint;
    const struct libusb_config_descriptor *config = TestParams->config;
    const struct libusb_interface *intf;
    const struct libusb_interface_descriptor *interface_desc;
    const struct libusb_endpoint_descriptor *endpoint_desc;
    uint8_t i, j, k;
    size_t num_endpoint;

    for (i = 0; i < config->bNumInterfaces; i++) {
        intf = &config->intf[i];
        for (j = 0; j < intf->num_altsetting; j++) {
            interface_desc = &intf->altsetting[j];

            if (interface_desc->bInterfaceNumber !=
                    TestParams->InterfaceDescriptor.bInterfaceNumber ||
                interface_desc->bAlternateSetting !=
                    TestParams->InterfaceDescriptor.bAlternateSetting) {
                continue;
            }

            num_endpoint = (size_t)interface_desc->bNumEndpoints;

            for (k = 0; k < num_endpoint; k++) {
                if (interface_desc->endpoint[k].bEndpointAddress != endpoint_addr) {
                    continue;
                }
                endpoint_desc = &interface_desc->endpoint[k];
                convert_to_libusbk_endpoint_descriptor(endpoint_desc, &TestParams->EndpointDescriptor);
                return 1;
            }
        }
    }
    return -1;
}

int fetch_usb_descriptors(PUVPERF_PARAM TestParams) {
    libusb_device_handle *dev_handle;
    struct libusb_config_descriptor *config;
    libusb_context *ctx = NULL;
    int result;

    result = libusb_init(&ctx);
    if (result != LIBUSB_SUCCESS) {
        return -1;
    }

    dev_handle =
        libusb_open_device_with_vid_pid(ctx, (uint16_t)TestParams->vid, (uint16_t)TestParams->pid);
    if (dev_handle == NULL) {
        libusb_exit(ctx);
        return -1;
    }

    result = libusb_get_config_descriptor(libusb_get_device(dev_handle), 0, &config);
    if (result != LIBUSB_SUCCESS) {
        libusb_close(dev_handle);
        libusb_exit(ctx);
        return -1;
    }

    TestParams->config = config;

    libusb_free_config_descriptor(config);
    libusb_close(dev_handle);
    libusb_exit(ctx);

    return 1;
}
