#include <libusb-1.0.27/libusb/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#define VENDOR_ID 0x1004
#define PRODUCT_ID 0xA000
#define TIMEOUT 1000
#define INTERFACE_NUMBER 1
#define ISO_ENDPOINT_ADDRESS 0x82

static void
transfer_cb(struct libusb_transfer *transfer)
{    
    for (int i = 0; i < transfer->num_iso_packets; ++i) {
        struct libusb_iso_packet_descriptor *ipd = transfer->iso_packet_desc + i;

        if (LIBUSB_TRANSFER_COMPLETED == ipd->status) {
            printf("Packet %d requested to transfer %u bytes, transfered %u bytes.\n",
                    i, ipd->length, ipd->actual_length);
        } else {
            fprintf(stderr, "Packet %d failed to transfer: status = %d.\n",
                    i, (int) ipd->status);
        }
    }

    *(int *) transfer->user_data = 1;
}

int main() {
    libusb_context *ctx = NULL;
    libusb_device_handle *dev_handle = NULL;
    int r;

    // Initialize libusb
    r = libusb_init(&ctx);
    if (r < 0) {
        fprintf(stderr, "Init Error %d\n", r);
        return 1;
    }
    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_DEBUG);

    // Open device
    dev_handle = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
    if (!dev_handle) {
        fprintf(stderr, "Cannot open device\n");
        goto cleanup;
    }
    printf("Device Opened\n\n\n");

    // Claim interface
    if (libusb_claim_interface(dev_handle, INTERFACE_NUMBER) < 0) {
        fprintf(stderr, "Cannot Claim Interface\n");
        goto cleanup_device;
    }
    printf("Claimed Interface\n\n\n");

    r = libusb_set_interface_alt_setting(dev_handle, INTERFACE_NUMBER, 1);
    if (r < 0) {
        fprintf(stderr, "Failed to set alternate setting\n");
        goto cleanup_interface;
    }

    // check the configuration descriptor
    struct libusb_config_descriptor *config;
    libusb_device *dev = libusb_get_device(dev_handle);
    // libusb_get_active_config_descriptor(dev, &config);

    // check the bInterval of the endpoint
    const struct libusb_endpoint_descriptor *ep_desc;
    libusb_get_active_config_descriptor(dev, &config);
    for (int i = 0; i < config->bNumInterfaces; i++) {
        const struct libusb_interface *interface = &config->interface[i];
        for (int j = 0; j < interface->num_altsetting; j++) {
            const struct libusb_interface_descriptor *interface_desc = &interface->altsetting[j];
            for (int k = 0; k < interface_desc->bNumEndpoints; k++) {
                ep_desc = &interface_desc->endpoint[k];
                if (ep_desc->bEndpointAddress == ISO_ENDPOINT_ADDRESS) {
                    fprintf(stderr, "i: %d, j: %d, k: %d\n", i, j, k);
                    printf("bInterval: %d\n", ep_desc->bInterval);
                }
            }
        }
    }

    unsigned int iso_interval_transfer = pow(2, (ep_desc->bInterval - 1));
    int msec_transfer_count = 1000 / (iso_interval_transfer * 125);
    printf("transfer count per 1 msec: %u, %d\n", iso_interval_transfer, msec_transfer_count);

    // Calculate additional Transaction Opportunities (MC) from ep_desc->wMaxPacketSize
    int mc = (ep_desc->wMaxPacketSize & 0x1800) >> 11;
    printf("MC: %d\n", mc);
    // MC = 0: 1 Transaction Opportunity
    // MC = 1: 2 Transaction Opportunities
    // MC = 2: 3 Transaction Opportunities

    int total_transfer_size = msec_transfer_count * (ep_desc->wMaxPacketSize & 0x7FF) * (mc + 1);
    printf("TOTAL_TRANSFER_SIZE: %d\n", total_transfer_size);

    // Continue with setting up the isochronous transfer
    const int num_iso_packets = total_transfer_size/(ep_desc->wMaxPacketSize & 0x7FF);
    int iso_packet_size = ep_desc->wMaxPacketSize & 0x7FF; // Extracting the maximum packet size

    // Allocate memory for the data to be transferred
    unsigned char *data = malloc(total_transfer_size);
    if (!data) {
        fprintf(stderr, "Failed to allocate memory for transfer data\n");
        return -1; // Or appropriate error handling
    }

    struct libusb_transfer *transfer = libusb_alloc_transfer(num_iso_packets);
    if (!transfer) {
        fprintf(stderr, "Failed to allocate transfer\n");
        free(data); // Clean up previously allocated data
        return -1; // Or appropriate error handling
    }

    int completed = 0;

    // Setup isochronous transfer
    libusb_fill_iso_transfer(transfer, dev_handle, ISO_ENDPOINT_ADDRESS, data, total_transfer_size, num_iso_packets, transfer_cb, &completed, TIMEOUT);
    for (int i = 0; i < num_iso_packets; i++) {
        transfer->iso_packet_desc[i].length = iso_packet_size;
    }

    // Submit transfer
    fprintf(stderr, "Starting isochronous transfer\n");
    r = libusb_submit_transfer(transfer);
    if (r != LIBUSB_SUCCESS) {
        fprintf(stderr, "Failed to submit isochronous transfer: %s\n", libusb_error_name(r));
        libusb_free_transfer(transfer); // Clean up transfer
        free(data); // Clean up data
        return -1; // Or appropriate error handling
    }
    else{
        fprintf(stderr,"Transfer submitted\n");
    }

    // Handle events (necessary to complete the transfer)
    while (!completed){
        libusb_handle_events_completed(NULL, NULL);
    }

    // Release the claimed interface
cleanup_interface:
    libusb_release_interface(dev_handle, INTERFACE_NUMBER);

cleanup_device:
    libusb_close(dev_handle);

cleanup:
    libusb_exit(ctx);
    free(data);
    return 0;
}
