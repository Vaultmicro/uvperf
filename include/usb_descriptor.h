#ifndef USB_DESCRIPTOR_H
#define USB_DESCRIPTOR_H

#include <stddef.h>
#include <stdint.h>
#include "libusb.h"
#include "libusbk.h"
#include "uvperf.h"

void convert_to_libusbk_endpoint_descriptor(const struct libusb_endpoint_descriptor *endpoint_desc,
                                            USB_ENDPOINT_DESCRIPTOR *usbk_endpoint_desc);

int find_endpoint_descriptor(PUVPERF_PARAM TestParams);

int fetch_usb_descriptors(PUVPERF_PARAM TestParams);

#endif /* USB_DESCRIPTOR_H */
