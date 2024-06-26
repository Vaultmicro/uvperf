#include <libusb.h>
#include <cstring>
#include <iostream>
#include "logger.h"

int bulk_transfer(libusb_device_handle *dev_handle, unsigned char endpoint, unsigned char *data,
                  int length, int *transferred, unsigned int timeout) {
    int result = libusb_bulk_transfer(dev_handle, endpoint, data, length, transferred, timeout);
    if (result < 0) {
        LOG_ERROR("Error in libusb_bulk_transfer: %s", libusb_error_name(result));
        LOG_ERROR("System error: %s", strerror(errno));
    }
    
    return result;
}

int main() {
    libusb_context *context = nullptr;
    libusb_device_handle *dev_handle = nullptr;
    libusb_device **devs;
    ssize_t cnt;
    int r;

    r = libusb_init(&context);
    if (r < 0) {
        LOGERR0("Failed to initialize libusb");
        return -1;
    }

    cnt = libusb_get_device_list(context, &devs);
    if (cnt < 0) {
        LOG_ERROR("Failed to get device list: %s", libusb_error_name(static_cast<int>(cnt)));
        libusb_exit(context);
        return -1;
    }

    for (ssize_t i = 0; i < cnt; i++) {
        libusb_device_descriptor desc;
        libusb_get_device_descriptor(devs[i], &desc);
        if (desc.idVendor == 0x1004 && desc.idProduct == 0x61a1) {
            r = libusb_open(devs[i], &dev_handle);
            if (r < 0) {
                LOGERR0("Failed to open device");
                libusb_free_device_list(devs, 1);
                libusb_exit(context);
                return -1;
            }
            break;
        }
    }

    if (dev_handle == nullptr) {
        LOGERR0("Device not found");
        libusb_free_device_list(devs, 1);
        libusb_exit(context);
        return -1;
    }

    // Set configuration
    r = libusb_set_configuration(dev_handle, 0);
    if (r < 0) {
        LOG_ERROR("Failed to set configuration: %s", libusb_error_name(r));
        libusb_close(dev_handle);
        libusb_free_device_list(devs, 1);
        libusb_exit(context);
        return -1;
    }

    // Claim interface
    r = libusb_claim_interface(dev_handle, 0);
    if (r < 0) {
        LOG_ERROR("Failed to claim interface: %s", libusb_error_name(r));
        libusb_close(dev_handle);
        libusb_free_device_list(devs, 1);
        libusb_exit(context);
        return -1;
    }

    unsigned char data[16384*500];
    int transferred;
    for (int i = 0; i < 10000; i++) {

        int result = bulk_transfer(dev_handle, 0x81, data, sizeof(data), &transferred, 3000);

        if (result == 0) {
            LOG_MSG("Transferred %d bytes", transferred);
        }
    }

    libusb_release_interface(dev_handle, 0);
    libusb_close(dev_handle);
    libusb_free_device_list(devs, 1);
    libusb_exit(context);
    return 0;
}
