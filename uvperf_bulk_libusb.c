#include <stdio.h>
#include <libusb-1.0.27/libusb/libusb.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

int main() {
    libusb_device_handle *dev_handle;
    libusb_context *ctx = NULL;
    int r;
    long long total_read = 0;
    long long total_written = 0;
    unsigned char data[30000];
    int actual_length;
    int actual_written;
    struct timeval start_time, end_time;
    long long elapsed;

    r = libusb_init(&ctx);
    if (r < 0) {
        fprintf(stderr, "Init Error %d\n", r);
        return 1;
    }

    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_INFO);

    dev_handle = libusb_open_device_with_vid_pid(ctx, 0x1004, 0x61a1);
    if (dev_handle == NULL) {
        fprintf(stderr, "Cannot open device\n");
        goto exit;
    }

    if (libusb_claim_interface(dev_handle, 0) < 0) {
        fprintf(stderr, "Cannot claim interface\n");
        goto close_device;
    }

    if(libusb_set_interface_alt_setting(dev_handle, 0, 0) < 0) {
        fprintf(stderr, "Cannot set interface alternate setting\n");
        goto close_device;
    }

    // check configuration descriptor
    struct libusb_config_descriptor *config;
    r = libusb_get_active_config_descriptor(libusb_get_device(dev_handle), &config);
    if (r < 0) {
        fprintf(stderr, "Cannot get configuration descriptor\n");
    }

    // check interface descriptor
    // const struct libusb_interface *inter;
    // const struct libusb_interface_descriptor *interdesc;
    // const struct libusb_endpoint_descriptor *epdesc;

    // for (int i = 0; i < config->bNumInterfaces; i++) {
    //     inter = &config->interface[i];
    //     printf("Number of alternate settings: %d\n", inter->num_altsetting);

    //     for (int j = 0; j < inter->num_altsetting; j++) {
    //         interdesc = &inter->altsetting[j];
    //         printf("Interface number: %d\n", interdesc->bInterfaceNumber);
    //         printf("Number of endpoints: %d\n", interdesc->bNumEndpoints);

    //         for (int k = 0; k < interdesc->bNumEndpoints; k++) {
    //             epdesc = &interdesc->endpoint[k];
    //             printf("Descriptor type: %d\n", epdesc->bDescriptorType);
    //             printf("Endpoint address: 0x%x\n", epdesc->bEndpointAddress);
    //             printf("Attributes: %d\n", epdesc->bmAttributes);
    //             printf("Max packet size: %d\n", epdesc->wMaxPacketSize);
    //             printf("Interval: %d\n", epdesc->bInterval);
    //         }
    //     }

    //     printf("\n\n\n");
    // }

    // printf("Reading\n");
    // gettimeofday(&start_time, NULL);
    // r = libusb_bulk_transfer(dev_handle, 0x81, data, sizeof(data), &actual_length, 3000);
    // gettimeofday(&end_time, NULL);
    // if (r == 0 && actual_length > 0) {
    //     total_read += actual_length;
    //     printf("Read %d bytes\n", actual_length);
    //     double elapsed = (end_time.tv_sec - start_time.tv_sec) * 1000.0;
    //     elapsed += (end_time.tv_usec - start_time.tv_usec) / 1000.0;
    //     printf("Reading speed: %.2f bytes/second\n", total_read / elapsed * 1000);
    //     printf("Reading speed: %.2f Mbps\n", (total_read / elapsed * 1000) * 8 / (1000*1000));
    // } else {
    //     fprintf(stderr, "Error reading: %d\n", r);
    // }

    printf("Bulk Write Maximum Packet : %d\n", libusb_get_max_packet_size(libusb_get_device(dev_handle), 0x0e));
    gettimeofday(&start_time, NULL);

    while(1){
        r = libusb_bulk_transfer(dev_handle, 0x81, data, sizeof(data), &actual_written, 3000);
        if(r < 0) break;

        total_written += (long)actual_written;
        gettimeofday(&end_time, NULL);
        
        elapsed = (end_time.tv_sec - start_time.tv_sec) * 1000.0;
        elapsed += (end_time.tv_usec - start_time.tv_usec) / 1000.0 ;
        if(elapsed >= 60000*5) break;
    }
    if (r == 0) {
        printf("TRNASFER_SIZE : %d\n", sizeof(data));
        printf("\tTotal %.2f Bytes\n", total_written);
        printf("\tAverage: %.2f bytes/second\n", total_written / elapsed * 1000.0);
        printf("\tAverage: %.2f Mbps\n", (total_written / elapsed) * 8.0 / 1000.0);
        printf("\tElapsed Time %.2f seconds\n", elapsed/1000.0);
    } else {
        fprintf(stderr, "Error writing: %d\n", r);
    }

    libusb_release_interface(dev_handle, 0);

close_device:
    libusb_close(dev_handle);

exit:
    libusb_exit(ctx);
    return 0;
}
