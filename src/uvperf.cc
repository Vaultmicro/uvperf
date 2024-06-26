/*!*********************************************************************
 *   uvperf.c
 *   Version : V2.0.0
 *   Author : usiop-vault
 *   This is a simple utility to test the performance of USB transfers.
 *   It is designed to be used with the libusbK driver.
 *   The utility will perform a series of transfers to the specified endpoint
 *   and report the results.
 *
 *   Usage:
 *   uvperf -V VERBOSE-v VID -p PID -i INTERFACE -a AltInterface -e ENDPOINT -m TRANSFERMODE
 * -t TIMEOUT -c BUFFERCOUNT -b BUFFERLENGTH -l READLENGTH -w WRITELENGTH -r REPEAT -S SHOWTRANSFER
 *-L LOGTOFILE
 *
 *   -VVERBOSE       Enable verbose output
 *   -vVID           USB Vendor ID
 *   -pPID           USB Product ID
 *   -iINTERFACE     USB Interface
 *   -aAltInterface  USB Alternate Interface
 *   -eENDPOINT      USB Endpoint
 *   -mTRANSFERMODE  0 = Async, 1 = Sync
 *   -tTIMEOUT       USB Transfer Timeout
 *   -cBUFFERCOUNT   Number of buffers to use
 *   -bBUFFERLENGTH  Length of buffers
 *   -lREADLENGTH    Length of read transfers
 *   -wWRITELENGTH   Length of write transfers
 *   -rREPEAT        Number of transfers to perform
 *   -SSHOWTRANSFER  1 = Show transfer data, defulat = 0\n
 *   -LLOGTOFILE     Enable logging to file
 *
 *   Example:
 *   uvperf -v0x1004 -p0xa000 -i0 -a0 -e0x81 -m1 -l1024 -r1000
 *
 *   This will perform 1000 bulk transfers of 1024 bytes to endpoint 0x81
 *   on interface 0, alternate setting 0 of a device with VID 0x1004 and PID 0xA000.
 *
 ********************************************************************!*/

#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <ctime>
#include <iostream>

#include "logger.h"
#include "transfer.h"
#include "uvperf.h"

namespace UVPerf {

UVPerf::UVPerf() {
    if (libusb_init(nullptr) < 0) {
        LOGERR0("Failed to initialize libusb");
        std::exit(EXIT_FAILURE);
    }
    setParamsDefaults();
}

UVPerf::~UVPerf() {
    freeTransferParams();
    libusb_exit(nullptr);
}

void UVPerf::setParamsDefaults() {
    inTest = std::make_shared<TransferParams>();
    outTest = std::make_shared<TransferParams>();
}

bool UVPerf::parseArgs(int argc, char **argv) {
    int opt;
    while ((opt = getopt(argc, argv, "Vv:p:i:a:e:m:t:b:c:l:w:SL")) != -1) {
        switch (opt) {
        case 'V':
            inTest->verbose = outTest->verbose = true;
            break;
        case 'v':
            inTest->vid = outTest->vid = static_cast<uint16_t>(std::strtol(optarg, nullptr, 0));
            inTest->list = outTest->list = false;
            break;
        case 'p':
            inTest->pid = outTest->pid = static_cast<uint16_t>(std::strtol(optarg, nullptr, 0));
            inTest->list = outTest->list = false;
            break;
        case 'i':
            inTest->intf = outTest->intf = std::strtol(optarg, nullptr, 0);
            break;
        case 'a':
            inTest->altf = outTest->altf = std::strtol(optarg, nullptr, 0);
            break;
        case 'e':
            inTest->endpoint = outTest->endpoint =
                static_cast<uint8_t>(std::strtol(optarg, nullptr, 0));
            break;
        case 'm':
            inTest->transferMode = outTest->transferMode =
                (std::strtol(optarg, nullptr, 0) ? TransferMode::Async : TransferMode::Sync);
            break;
        case 'b':
            inTest->bufferLength = outTest->bufferLength =
                static_cast<uint32_t>(std::strtol(optarg, nullptr, 0));
            break;
        case 'c':
            inTest->bufferCount = outTest->bufferCount =
                static_cast<uint32_t>(std::strtol(optarg, nullptr, 0));
            break;
        case 'l':
            inTest->readLength = static_cast<uint32_t>(std::strtol(optarg, nullptr, 0));
            inTest->readLength = std::max(inTest->readLength, inTest->bufferLength);
            break;
        case 'w':
            outTest->writeLength = static_cast<uint32_t>(std::strtol(optarg, nullptr, 0));
            outTest->writeLength = std::max(outTest->writeLength, outTest->bufferLength);
            break;
        case 'S':
            inTest->isRunning.store(true);
            outTest->isRunning.store(true);
            break;
        case 'L':
            inTest->fileIO = true;
            outTest->fileIO = true;
            break;
        default:
            return false;
        }
    }

    return true;
}

void UVPerf::showUsage() const {
    LOGMSG0(
        "Usage: uvperf -v VID -p PID -i INTERFACE -a AltInterface -e ENDPOINT -m TRANSFERMODE -t "
        "TIMEOUT -b BUFFERLENGTH -c BUFFERCOUNT -l READLENGTH -w WRITELENGTH -r REPEAT -S -L");
    LOGMSG0("\t-v VID           USB Vendor ID");
    LOGMSG0("\t-p PID           USB Product ID");
    LOGMSG0("\t-i INTERFACE     USB Interface");
    LOGMSG0("\t-a AltInterface  USB Alternate Interface");
    LOGMSG0("\t-e ENDPOINT      USB Endpoint");
    LOGMSG0("\t-m TRANSFER      0 = isochronous, 1 = bulk");
    LOGMSG0("\t-t TIMEOUT       USB Transfer Timeout");
    LOGMSG0("\t-b BUFFERLENGTH  Length of buffers");
    LOGMSG0("\t-c BUFFERCOUNT   Number of buffers to use");
    LOGMSG0("\t-l READLENGTH    Length of read transfers");
    LOGMSG0("\t-w WRITELENGTH   Length of write transfers");
    LOGMSG0("\t-r REPEAT        Number of transfers to perform");
    LOGMSG0("\t-S               Show transfer data");
    LOGMSG0("\t-L               Enable logging to file");
    LOGMSG0("\nExample:");
    LOGMSG0("uvperf -v 0x1004 -p 0xa000 -i 0 -a 0 -e 0x81 -m 1 -t 1000 -l 1024 -w 1024 -r 1000");
}

bool UVPerf::openDevice() {
    libusb_device **list;
    ssize_t cnt = libusb_get_device_list(nullptr, &list);
    if (cnt < 0) {
        LOGERR0("Failed to get device list");
        return false;
    }

    bool deviceFound = false;

    for (ssize_t i = 0; i < cnt; ++i) {
        libusb_device *device = list[i];
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(device, &desc) == 0) {
            if (desc.idVendor == transferParam->vid && desc.idProduct == transferParam->pid) {
                if (libusb_open(device, &transferParam->interfaceHandle) == 0) {
                    deviceFound = true;
                    break;
                } else {
                    LOGERR0("Cannot open device");
                }
            }
        }
    }

    libusb_free_device_list(list, 1);

    if (!deviceFound) {
        LOGERR0("Device not found");
        return false;
    }

    return true;
}

bool UVPerf::createTransferParams() {
    transferParam->transferHandles.resize(transferParam->bufferCount);
    for (auto &handle : transferParam->transferHandles) {
        handle.data.resize(transferParam->bufferLength);
    }

    if (libusb_claim_interface(transferParam->interfaceHandle, transferParam->intf) != 0) {
        LOGERR0("Cannot claim interface");
        return false;
    }
    // if (libusb_set_interface_alt_setting(transferParam->interfaceHandle, transferParam->intf,
    //                                      transferParam->altf) != 0) {
    //     LOGERR0("Cannot set alternate setting");
    //     return false;
    // }

    return true;
}

void UVPerf::freeTransferParams() {
    if (inTest && inTest->interfaceHandle) {
        libusb_release_interface(inTest->interfaceHandle, inTest->intf);
        libusb_close(inTest->interfaceHandle);
        inTest->interfaceHandle = nullptr;
    }

    if (outTest && outTest->interfaceHandle) {
        libusb_release_interface(outTest->interfaceHandle, outTest->intf);
        libusb_close(outTest->interfaceHandle);
        outTest->interfaceHandle = nullptr;
    }

    if (transferParam) {
        transferParam->transferHandles.clear();

        if (transferParam->interfaceHandle) {
            libusb_release_interface(transferParam->interfaceHandle, transferParam->intf);
            libusb_close(transferParam->interfaceHandle);
            transferParam->interfaceHandle = nullptr;
        }
    }
}

void UVPerf::showTransferParams() const {
    LOG_MSG("Transfer Parameters:");
    LOG_MSG("VID               : 0x%x", transferParam->vid);
    LOG_MSG("PID               : 0x%x", transferParam->pid);
    LOG_MSG("Interface         : %d", transferParam->intf);
    LOG_MSG("Alternate Setting : %d", transferParam->altf);
    LOG_MSG("Endpoint          : 0x%x", transferParam->endpoint);
    LOG_MSG("Buffer Length     : %d", transferParam->bufferLength);
    LOG_MSG("Read Length       : %d", transferParam->readLength);
    LOG_MSG("Write Length      : %d", transferParam->writeLength);
    LOG_MSG("Buffer Count      : %d", transferParam->bufferCount);
}

void UVPerf::openLogFile() {
    if (transferParam->fileIO) {
        time_t now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);

        char buffer[256];
        strftime(buffer, sizeof(buffer), "uvperf_log_%Y%m%d_%H%M%S.txt", &t);
        transferParam->logFileName = buffer;
        transferParam->logFile.open(transferParam->logFileName, std::ios::out | std::ios::app);

        if (!transferParam->logFile.is_open()) {
            LOG_ERROR("Failed to open log file: %s", transferParam->logFileName.c_str());
            transferParam->fileIO = false;
        }
    }
}

void UVPerf::logTransferParams() {
    if (transferParam->fileIO && transferParam->logFile.is_open()) {
        transferParam->logFile << "VID: 0x" << std::hex << transferParam->vid << "\n";
        transferParam->logFile << "PID: 0x" << std::hex << transferParam->pid << "\n";
        transferParam->logFile << "Interface: " << std::dec << transferParam->intf << "\n";
        transferParam->logFile << "Alternate Setting: " << std::dec << transferParam->altf << "\n";
        transferParam->logFile << "Endpoint: 0x" << std::hex << transferParam->endpoint << "\n";
        transferParam->logFile << "Buffer Length: " << std::dec << transferParam->bufferLength
                               << "\n";
        transferParam->logFile << "Read Length: " << std::dec << transferParam->readLength << "\n";
        transferParam->logFile << "Write Length: " << std::dec << transferParam->writeLength
                               << "\n";
        transferParam->logFile << "Buffer Count: " << std::dec << transferParam->bufferCount
                               << "\n";
        transferParam->logFile << "------------------------------------------------\n";
    }
}

void UVPerf::closeLogFile() {
    if (transferParam->fileIO && transferParam->logFile.is_open()) {
        transferParam->logFile.close();
    }
}

int UVPerf::getDeviceInfoFromList() {
    char selection;
    char count = 0;
    libusb_device **list;
    ssize_t cnt = libusb_get_device_list(nullptr, &list);

    if (cnt <= 0) {
        LOGERR0("No devices found");
        libusb_free_device_list(list, 1);
        return -1;
    }

    if (cnt == 1) {
        for (ssize_t i = 0; i < cnt; ++i) {
            libusb_device *device = list[i];
            libusb_device_descriptor desc;
            if (libusb_get_device_descriptor(device, &desc) == 0) {
                ++count;

                LOG_MSG("Manufacturer: %d. Product: %d. %d. VID: 0x%x PID: 0x%x",
                        desc.iManufacturer, desc.iProduct, static_cast<int>(count), desc.idVendor,
                        desc.idProduct);
            }
        }
        libusb_free_device_list(list, 1);
        return 0;
    } else {
        for (ssize_t i = 0; i < cnt; ++i) {
            libusb_device *device = list[i];
            libusb_device_descriptor desc;
            if (libusb_get_device_descriptor(device, &desc) == 0) {
                ++count;

                LOG_MSG("Manufacturer: %d. Product: %d. %d. VID: 0x%x PID: 0x%x",
                        desc.iManufacturer, desc.iProduct, static_cast<int>(count), desc.idVendor,
                        desc.idProduct);
            }
        }
        int validSelection = 0;

        do {
            LOG_MSG("Select Device (1-%d, or 'q' to quit): ", static_cast<int>(count));
            std::cin >> selection;
            if (selection == 'q') {
                LOGMSG0("Quitting...");
                std::exit(0);
            }
            if (selection < '1' || selection > '0' + count) {
                LOG_ERROR("Invalid selection. Please select a number between 1 and %d",
                        static_cast<int>(count));
                continue;
            }
            validSelection = 1;
        } while (!validSelection);

        count = 0;
        for (ssize_t i = 0; i < cnt; ++i) {
            libusb_device *device = list[i];
            libusb_device_descriptor desc;
            if (libusb_get_device_descriptor(device, &desc) == 0) {
                ++count;
                if (count == (selection - '0')) {
                    inTest->vid = outTest->vid = desc.idVendor;
                    inTest->pid = outTest->pid = desc.idProduct;
                    if (libusb_open(device, &inTest->interfaceHandle) != 0) {
                        LOGERR0("Cannot open device");
                        libusb_free_device_list(list, 1);
                        return -1;
                    }
                    break;
                }
            }
        }

        libusb_free_device_list(list, 1);
        return 0;
    }
}

int UVPerf::getEndpointFromList() {
    if (inTest->interfaceHandle == nullptr) {
        LOGERR0("Device not opened");
        return -1;
    }

    libusb_config_descriptor *config;
    if (libusb_get_active_config_descriptor(libusb_get_device(inTest->interfaceHandle), &config) !=
        0) {
        LOGERR0("Failed to get config descriptor");
        return -1;
    }

    struct EndpointDetail {
        libusb_endpoint_descriptor epDescriptor;
        int interfaceNumber;
        int altSettingNumber;
    };

    std::vector<EndpointDetail> endpoints;
    for (uint8_t i = 0; i < config->bNumInterfaces; ++i) {
        const libusb_interface &interface = config->interface[i];
        for (int j = 0; j < interface.num_altsetting; ++j) {
            const libusb_interface_descriptor &altsetting = interface.altsetting[j];
            for (uint8_t k = 0; k < altsetting.bNumEndpoints; ++k) {
                EndpointDetail detail = {altsetting.endpoint[k], i, j};
                endpoints.push_back(detail);
                LOG_MSG("Endpoint %d: Endpoint Descriptor Type: %s, %s, Endpoint Address: 0x%x, "
                        "Max Packet Size: %d",
                        static_cast<int>(endpoints.size()),
                        ((altsetting.endpoint[k].bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) ==
                                 LIBUSB_TRANSFER_TYPE_ISOCHRONOUS
                             ? "iso"
                             : "bulk"),
                        ((altsetting.endpoint[k].bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
                                 LIBUSB_ENDPOINT_IN
                             ? "in"
                             : "out"),
                        static_cast<int>(altsetting.endpoint[k].bEndpointAddress),
                        altsetting.endpoint[k].wMaxPacketSize);
            }
        }
    }

    if (endpoints.empty()) {
        LOGERR0("No endpoints found");
        libusb_free_config_descriptor(config);
        return -1;
    }

    int validSelection = 0;
    int selection = 0;

    do {
        LOG_MSG("Select Endpoint (1-%d, or 'q' to quit): ", static_cast<int>(endpoints.size()));
        std::cin >> selection;
        if (selection == 'q') {
            LOGMSG0("Quitting...");
            std::exit(0);
        }
        if (selection < 1 || selection > static_cast<int>(endpoints.size())) {
            LOG_ERROR("Invalid selection. Please select a number between 1 and %d",
                    static_cast<int>(endpoints.size()));
            continue;
        }
        validSelection = 1;
    } while (!validSelection);

    EndpointDetail selectedEndpoint = endpoints[selection - 1];

    if ((selectedEndpoint.epDescriptor.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
        LIBUSB_ENDPOINT_IN) {
        inTest->epDescriptor = selectedEndpoint.epDescriptor;
        inTest->intf = selectedEndpoint.interfaceNumber;
        inTest->altf = selectedEndpoint.altSettingNumber;

        outTest.reset();
    } else {
        outTest->epDescriptor = selectedEndpoint.epDescriptor;
        outTest->intf = selectedEndpoint.interfaceNumber;
        outTest->altf = selectedEndpoint.altSettingNumber;

        inTest.reset();
    }

    libusb_free_config_descriptor(config);
    return 0;
}

void UVPerf::transferLoop() {
    Transfer transferManager;
    transferManager.createTransferParam(transferParam, transferParam->endpoint);

    if (!transferParam) {
        LOGERR0("Failed to create transfer parameters.");
        return;
    }

    while (!transferParam->isCancelled.load()) {
        for (auto &handle : transferParam->transferHandles) {
            if (transferParam->transferMode == TransferMode::Sync) {
                int actual_length;
                int res = libusb_bulk_transfer(transferParam->interfaceHandle,
                                               transferParam->endpoint, handle.data.data(),
                                               transferParam->bufferLength, &actual_length, 0);
                if (res < 0) {
                    LOG_ERROR("Transfer error: %s", libusb_error_name(res));
                    transferParam->isCancelled.store(true);
                    break;
                }
                handle.returnCode = actual_length;
            } else {
                libusb_fill_bulk_transfer(
                    handle.isochHandle, transferParam->interfaceHandle, transferParam->endpoint,
                    handle.data.data(), transferParam->bufferLength,
                    [](libusb_transfer *transfer) {
                        auto *handle = static_cast<TransferHandle *>(transfer->user_data);
                        handle->returnCode = transfer->actual_length;
                        handle->inUse.store(false);
                    },
                    &handle, 0);
                int res = libusb_submit_transfer(handle.isochHandle);
                if (res < 0) {
                    LOG_ERROR("Submit transfer error: %s", libusb_error_name(res));
                    transferParam->isCancelled.store(true);
                    break;
                }
                handle.inUse.store(true);
            }
        }

        if (transferParam->transferMode == TransferMode::Async) {
            for (auto &handle : transferParam->transferHandles) {
                if (handle.inUse.load()) {
                    libusb_handle_events_completed(nullptr, nullptr);
                }
            }
        }

        if (std::cin.peek() == 'q') {
            transferParam->isUserAborted.store(true);
            transferParam->isCancelled.store(true);
        }
    }

    transferManager.waitForTestTransfer(transferParam, 10000);
}

void UVPerf::waitForTransferCompletion() {
    for (auto &handle : transferParam->transferHandles) {
        if (handle.inUse.load()) {
            libusb_cancel_transfer(handle.isochHandle);
        }
    }

    if (transferParam->transferMode == TransferMode::Async) {
        libusb_handle_events_completed(nullptr, nullptr);
    }
}

bool UVPerf::initialize(int argc, char **argv) {
    if (argc == 1) {
        showUsage();
        return false;
    } else {
        if (!parseArgs(argc, argv)) {
            return false;
        }
    }

    if (inTest->list || outTest->list) {
        if (getDeviceInfoFromList() < 0) {
            return false;
        }
        if (getEndpointFromList() < 0) {
            return false;
        }
    } else {
        if (!openDevice()) {
            return false;
        }
    }

    if (inTest) {
        transferParam = inTest;
        transferParam->endpoint = (uint8_t)transferParam->epDescriptor.bEndpointAddress;
        transferParam->readLength =
            std::max(transferParam->bufferLength, transferParam->readLength);
    }
    if (outTest) {
        transferParam = outTest;
        transferParam->endpoint = (uint8_t)transferParam->epDescriptor.bEndpointAddress;
        transferParam->writeLength =
            std::max(transferParam->bufferLength, transferParam->writeLength);
    }

    if (!createTransferParams()) {
        return false;
    }

    openLogFile();

    return true;
}

void UVPerf::run() {
    Transfer transferManager;

    showTransferParams();
    logTransferParams();

    transferManager.createTransferParam(transferParam, transferParam->endpoint);
    waitForTransferCompletion();
    closeLogFile();
}

} // namespace UVPerf

int main(int argc, char **argv) {
    UVPerf::UVPerf uvperf;
    if (!uvperf.initialize(argc, argv)) {
        return EXIT_FAILURE;
    }
    uvperf.run();
    return EXIT_SUCCESS;
}