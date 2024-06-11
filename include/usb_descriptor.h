#ifndef USB_DESCRIPTOR_H
#define USB_DESCRIPTOR_H

#include <stddef.h>
#include <stdint.h>
#include "libusb.h"
#include "libusbk.h"
#include "uvperf.h"
#include "log.h"

int find_endpoint_descriptor(PUVPERF_PARAM TestParams);

int open_device_with_libusb(PUVPERF_PARAM TestParams);

#endif /* USB_DESCRIPTOR_H */
