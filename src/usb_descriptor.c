#include "usb_descriptor.h"
#include <stdio.h>
#include <stdlib.h>

void convert_to_libusbk_endpoint_descriptor(const struct libusb_endpoint_descriptor *endpoint_desc,
                                            USB_ENDPOINT_DESCRIPTOR *usbk_endpoint_desc) {
    usbk_endpoint_desc->bEndpointAddress = (UCHAR)endpoint_desc->bEndpointAddress;
    usbk_endpoint_desc->bmAttributes = (UCHAR)endpoint_desc->bmAttributes;
    usbk_endpoint_desc->bInterval = (UCHAR)endpoint_desc->bInterval;
    usbk_endpoint_desc->bLength = (UCHAR)endpoint_desc->bLength;
    usbk_endpoint_desc->bDescriptorType = (UCHAR)endpoint_desc->bDescriptorType;
    usbk_endpoint_desc->wMaxPacketSize = (UCHAR)endpoint_desc->wMaxPacketSize;
}

int find_endpoint_descriptor(PUVPERF_PARAM TestParams) {
    for (int i = 0; i < TestParams->num_ep; i++) {
        if (TestParams->endpoint_descs[i].bEndpointAddress == TestParams->endpoint) {
            convert_to_libusbk_endpoint_descriptor(&TestParams->endpoint_descs[i],
                                                   &TestParams->EndpointDescriptor);
            return 1;
        }
    }

    return -1;
}

int open_device_with_libusb(PUVPERF_PARAM TestParams) {
    libusb_device **devs;
    libusb_context *ctx = NULL;

    int r;
    ssize_t cnt;

    r = libusb_init(&ctx);
    if (r < 0) {
        fprintf(stderr, "Init Error\n");
        return 1;
    }
    cnt = libusb_get_device_list(ctx, &devs);
    if (cnt < 0) {
        fprintf(stderr, "Get Device Error\n");
        libusb_exit(ctx);
        return 1;
    }

    ssize_t i;
    for (i = 0; i < cnt; i++) {
        libusb_device *device = devs[i];
        struct libusb_device_descriptor desc;
        r = libusb_get_device_descriptor(device, &desc);
        if (r < 0) {
            fprintf(stderr, "Failed to get device descriptor\n");
            continue;
        }

        if (desc.idVendor != TestParams->vid || desc.idProduct != TestParams->pid) {
            continue;
        }

        libusb_device_handle *handle;
        r = libusb_open(device, &handle);
        if (r < 0) {
            fprintf(stderr, "Unable to open device\n");
        } else {
            struct libusb_config_descriptor *config;
            r = libusb_get_config_descriptor(libusb_get_device(handle), 0, &config);
            if (r != LIBUSB_SUCCESS) {
                libusb_close(handle);
                libusb_exit(ctx);
                return -1;
            }

            TestParams->num_intf = config->bNumInterfaces;
            int k = 0;
            int num_ep = 0;
            for (int i = 0; i < TestParams->num_intf; i++) {
                const struct libusb_interface *intf = &config->intf[i];
                for (int j = 0; j < intf->num_altsetting; j++) {

                    TestParams->intferface_descs[k] = config->intf[i].altsetting[j];
                    if (intf->altsetting[j].endpoint == 0) {
                        continue;
                    }
                    TestParams->endpoint_descs[num_ep].bDescriptorType =
                        config->intf[i].altsetting[j].endpoint->bDescriptorType;
                    TestParams->endpoint_descs[num_ep].bEndpointAddress =
                        config->intf[i].altsetting[j].endpoint->bEndpointAddress;
                    TestParams->endpoint_descs[num_ep].bmAttributes =
                        config->intf[i].altsetting[j].endpoint->bmAttributes;
                    TestParams->endpoint_descs[num_ep].bInterval =
                        config->intf[i].altsetting[j].endpoint->bInterval;
                    TestParams->endpoint_descs[num_ep].bLength =
                        config->intf[i].altsetting[j].endpoint->bLength;
                    TestParams->endpoint_descs[num_ep].wMaxPacketSize =
                        config->intf[i].altsetting[j].endpoint->wMaxPacketSize;
                    num_ep++;
                    k++;
                }
            }
            TestParams->num_ep = num_ep;

            libusb_free_config_descriptor(config);
            libusb_close(handle);
            libusb_exit(ctx);

            return 1;
        }
    }

    libusb_free_device_list(devs, 1);
    return -1;
}