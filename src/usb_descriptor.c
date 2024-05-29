#include "usb_descriptor.h"
#include <stdio.h>
#include <stdlib.h>

void convert_to_libusbk_endpoint_descriptor(const struct libusb_endpoint_descriptor *src,
                                            USB_ENDPOINT_DESCRIPTOR *dest) {
    dest->bLength = src->bLength;
    dest->bDescriptorType = src->bDescriptorType;
    dest->bEndpointAddress = src->bEndpointAddress;
    dest->bmAttributes = src->bmAttributes;
    dest->wMaxPacketSize = src->wMaxPacketSize;
    dest->bInterval = src->bInterval;
}

int fetch_usb_descriptors(PUVPERF_PARAM TestParams) {
    USB_INTERFACE_DESCRIPTOR *k_interface_desc = &TestParams->InterfaceDescriptor;
    USB_ENDPOINT_DESCRIPTOR *k_endpoint_desc = &TestParams->EndpointDescriptor;
    UCHAR endpoint_addr = (UCHAR)TestParams->PipeInformation[0].PipeId;
    libusb_device_handle *dev_handle;
    struct libusb_config_descriptor *config;
    const struct libusb_interface *intf;
    const struct libusb_interface_descriptor *interface_desc;
    const struct libusb_endpoint_descriptor *endpoint_desc;
    libusb_context *ctx = NULL;
    int result;
    size_t i, j;
    size_t num_endpoint;

    result = libusb_init(&ctx);
    if (result != LIBUSB_SUCCESS) {
        return -1;
    }

    dev_handle = libusb_open_device_with_vid_pid(ctx, (uint16_t)TestParams->vid, (uint16_t)TestParams->pid);
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

    for (i = 0; i < config->bNumInterfaces; i++) {
        intf = &config->intf[i];
        for (j = 0; j < intf->num_altsetting; j++) {
            interface_desc = &intf->altsetting[j];

            if (interface_desc->bInterfaceNumber != k_interface_desc->bInterfaceNumber ||
                interface_desc->bAlternateSetting != k_interface_desc->bAlternateSetting) {
                continue;
            }

            num_endpoint = (size_t)interface_desc->bNumEndpoints;

            for (int k = 0; k < num_endpoint; k++) {
                if (interface_desc->endpoint[k].bEndpointAddress == endpoint_addr) {
                    endpoint_desc = &interface_desc->endpoint[k];
                    convert_to_libusbk_endpoint_descriptor(endpoint_desc, k_endpoint_desc);
                    break;
                }
            }
        }
    }

    libusb_free_config_descriptor(config);
    libusb_close(dev_handle);
    libusb_exit(ctx);

    return 1;
}
