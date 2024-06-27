#include "transfer.h"
#include "logger.h"
#include "uvperf.h"

#include <iostream>
#include <memory>

namespace UVPerf {

std::mutex TransferStatus::displayMutex;

Transfer::Transfer() = default;
Transfer::~Transfer() = default;

int Transfer::verifyData(const std::shared_ptr<TransferParams> &transferParam,
                         const std::vector<uint8_t> &data) {
    // Verify data implementation
    return 0;
}

int Transfer::transferSync(const std::shared_ptr<TransferParams> &transferParam) {
    int transferred = 0;
    int result = 0;
    auto endpointAddress = transferParam->epDescriptor.bEndpointAddress;
    auto buffer = transferParam->verifyBuffer.get();
    unsigned char data[transferParam->bufferLength];
    if (endpointAddress & LIBUSB_ENDPOINT_IN) {
        result = libusb_bulk_transfer(transferParam->interfaceHandle, endpointAddress, data,
                                      transferParam->bufferLength, &transferred, 0);
    } else {
        result = libusb_bulk_transfer(transferParam->interfaceHandle, endpointAddress, data,
                                      transferParam->bufferLength, &transferred, 0);
    }

    return (result == 0) ? transferred : -1;
}

int Transfer::transferAsync(const std::shared_ptr<TransferParams> &transferParam,
                            TransferHandle *&handleRef) {
    int ret = 0;
    handleRef = nullptr;

    while (transferParam->isRunning.load() &&
           transferParam->transferHandles.size() < transferParam->bufferCount) {
        handleRef = &transferParam->transferHandles[transferParam->transferHandles.size()];

        if (!handleRef->isochHandle) {
            handleRef->isochHandle = libusb_alloc_transfer(0);
            handleRef->data.resize(transferParam->bufferLength);
        }

        if (transferParam->epDescriptor.bmAttributes & LIBUSB_TRANSFER_TYPE_ISOCHRONOUS) {
            libusb_fill_iso_transfer(
                handleRef->isochHandle, transferParam->interfaceHandle,
                transferParam->epDescriptor.bEndpointAddress, handleRef->data.data(),
                transferParam->bufferLength, 1,
                [](libusb_transfer *transfer) {
                    auto *handle = static_cast<TransferHandle *>(transfer->user_data);
                    handle->returnCode = transfer->actual_length;
                    handle->inUse.store(false);
                },
                handleRef, 0);
            libusb_set_iso_packet_lengths(handleRef->isochHandle,
                                          transferParam->epDescriptor.wMaxPacketSize);
        } else {
            libusb_fill_bulk_transfer(
                handleRef->isochHandle, transferParam->interfaceHandle,
                transferParam->epDescriptor.bEndpointAddress, handleRef->data.data(),
                transferParam->bufferLength,
                [](libusb_transfer *transfer) {
                    auto *handle = static_cast<TransferHandle *>(transfer->user_data);
                    handle->returnCode = transfer->actual_length;
                    handle->inUse.store(false);
                },
                handleRef, 0);
        }

        ret = libusb_submit_transfer(handleRef->isochHandle);
        if (ret < 0) {
            handleRef->inUse.store(false);
            LOG_ERROR("Error submitting transfer: %s", libusb_error_name(ret));
            return ret;
        }

        handleRef->inUse.store(true);
        transferParam->transferHandles.emplace_back(std::move(*handleRef));
    }

    for (auto &handle : transferParam->transferHandles) {
        if (handle.inUse.load()) {
            ret = libusb_handle_events_completed(nullptr, nullptr);
            if (handle.returnCode < 0) {
                handle.inUse.store(false);
                LOG_ERROR("Transfer failed: %s", libusb_error_name(handle.returnCode));
                return handle.returnCode;
            }
        }
    }

    return ret;
}

int Transfer::createVerifyBuffer(const std::shared_ptr<TransferParams> &testParam,
                                 uint16_t endpointMaxPacketSize) {
    auto verifyBuffer = std::make_unique<uint8_t[]>(endpointMaxPacketSize);
    if (!verifyBuffer) {
        LOGERR0("Memory allocation failure.");
        return -1;
    }

    testParam->verifyBuffer = std::move(verifyBuffer);
    testParam->verifyBufferSize = endpointMaxPacketSize;

    for (int i = 0; i < endpointMaxPacketSize; ++i) {
        testParam->verifyBuffer[i] = static_cast<uint8_t>(i % 256);
    }

    return 0;
}

void Transfer::freeTransferParam(std::shared_ptr<TransferParams> &transferParamRef) {
    transferParamRef.reset();
}

void Transfer::createTransferParam(const std::shared_ptr<TransferParams> &testParam,
                                   int endpointID) {
    if (testParam->epDescriptor.wMaxPacketSize == 0) {
        LOG_ERROR("Max packet size is 0 for endpoint %d. Check alternate settings.", endpointID);
        return;
    }

    testParam->bufferLength = testParam->bufferLength * testParam->bufferCount;

    if ((testParam->epDescriptor.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) ==
        LIBUSB_TRANSFER_TYPE_ISOCHRONOUS) {
        testParam->transferMode = TransferMode::Async;
        if (testParam->epDescriptor.wMaxPacketSize == 0) {
            LOGERR0("Unable to determine 'MaxBytesPerInterval' for isochronous pipe.");
            return;
        }

        for (int i = 0; i < testParam->bufferCount; ++i) {
            testParam->transferHandles.emplace_back();
            auto &handle = testParam->transferHandles.back();
            handle.isochHandle = libusb_alloc_transfer(0);
            if (handle.isochHandle == nullptr) {
                LOGERR0("Failed to allocate isochronous transfer.");
                return;
            }
            handle.data.resize(testParam->bufferLength);
        }
    }

    testParam->transferThread = std::thread(&Transfer::transferThread, this, testParam);
}

void Transfer::getAverageBytesSec(const std::shared_ptr<TransferParams> &transferParam,
                                  double &byteps) {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::steady_clock::now() - transferParam->startTick)
                       .count();

    byteps = (elapsed > 0) ? static_cast<double>(transferParam->totalTransferred) / elapsed : 0.0;
}

void Transfer::getCurrentBytesSec(const std::shared_ptr<TransferParams> &transferParam,
                                  double &byteps) {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::steady_clock::now() - transferParam->lastStartTick)
                       .count();

    byteps = (elapsed > 0) ? static_cast<double>(transferParam->lastTransferred) / elapsed : 0.0;
}

void Transfer::showTransfer(const std::shared_ptr<TransferParams> &transferParam) {
    double bytepsAverage;
    double bytepsCurrent;

    getAverageBytesSec(transferParam, bytepsAverage);
    getCurrentBytesSec(transferParam, bytepsCurrent);

    LOG_MSG("Total %zu Bytes", transferParam->totalTransferred);
    LOG_MSG("Total %zu Transfers", transferParam->packets);
    LOG_MSG("Average %.2f Mbps/sec", (bytepsAverage * 8) / 1000 / 1000);
}

bool Transfer::waitForTestTransfer(const std::shared_ptr<TransferParams> &transferParam,
                                   unsigned int msToWait) {
    unsigned int waited = 0;
    while (transferParam && waited < msToWait) {
        if (!transferParam->isRunning) {
            if (transferParam->transferThread.joinable()) {
                transferParam->transferThread.join();
            }
            LOG_MSG("Stopped thread for endpoint %d", transferParam->epDescriptor.bEndpointAddress);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waited += 100;
    }
    return false;
}

void Transfer::transferThread(std::shared_ptr<TransferParams> transferParam) {
    transferParam->isRunning = true;

    while (!transferParam->isCancelled) {
        int ret = 0;
        uint8_t *buffer = nullptr;
        TransferHandle *handle = nullptr;

        if (transferParam->transferMode == TransferMode::Sync) {
            ret = transferSync(transferParam);
            if (ret >= 0)
                buffer = transferParam->verifyBuffer.get();
        } else if (transferParam->transferMode == TransferMode::Async) {
            handle = &transferParam->transferHandles[0];
            ret = transferAsync(transferParam, handle);
            if (handle && ret >= 0)
                buffer = handle->data.data();
        } else {
            LOGERR0("Invalid transfer mode.");
            break;
        }

        if (transferParam->verifyBuffer &&
            (transferParam->epDescriptor.bEndpointAddress & LIBUSB_ENDPOINT_IN) && ret > 0) {
            verifyData(transferParam, handle->data);
        }

        if (ret < 0) {
            if (transferParam->isUserAborted)
                break;
            transferParam->totalErrorCount++;
            transferParam->runningErrorCount++;
            LOG_ERROR("Transfer error: %d", ret);
            if (transferParam->runningErrorCount > 5) // Assuming 5 is the retry limit
                break;
            ret = 0;
        } else {
            transferParam->runningTimeoutCount = 0;
            transferParam->runningErrorCount = 0;
            if (transferParam->epDescriptor.bEndpointAddress & LIBUSB_ENDPOINT_IN) {
                if (transferParam->verifyBuffer) {
                    verifyData(transferParam, handle->data);
                }
            }
        }

        if (transferParam->packets == 0) {
            transferParam->startTick = std::chrono::steady_clock::now();
            transferParam->lastStartTick = transferParam->startTick;
            transferParam->lastTick = transferParam->startTick;
            transferParam->lastTransferred = 0;
            transferParam->totalTransferred = 0;
            transferParam->packets = 1;
        } else {
            if (transferParam->lastStartTick.time_since_epoch().count() == 0) {
                transferParam->lastStartTick = transferParam->lastTick;
                transferParam->lastTransferred = 0;
            }
            transferParam->lastTick = std::chrono::steady_clock::now();
            transferParam->lastTransferred += ret;
            transferParam->totalTransferred += ret;
            transferParam->packets++;
        }
    }

    transferParam->isRunning = false;
}

void TransferStatus::showRunningStatus(const std::shared_ptr<TransferParams> &readParam,
                                       const std::shared_ptr<TransferParams> &writeParam) {
    std::lock_guard<std::mutex> lock(displayMutex);
    double mbpsReadOverall = 0;
    double mbpsWriteOverall = 0;
    if (readParam) {
        getAverageBitSec(readParam, mbpsReadOverall);
    }
    if (writeParam) {
        getAverageBitSec(writeParam, mbpsWriteOverall);
    }
    LOG_MSG("Read: %.2f Mbps, Write: %.2f Mbps", mbpsReadOverall, mbpsWriteOverall);
}

void TransferStatus::getAverageBitSec(const std::shared_ptr<TransferParams> &transferParam,
                                      double &mbps) {
    auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::steady_clock::now() - transferParam->startTick)
                              .count();

    double byteps = (elapsedSeconds > 0)
                        ? static_cast<double>(transferParam->totalTransferred) / elapsedSeconds
                        : 0;
    mbps = (byteps * 8) / (1000 * 1000); // Convert to megabits per second
}

void TransferStatus::getCurrentBitSec(const std::shared_ptr<TransferParams> &transferParam,
                                      double &mbps) {
    auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::steady_clock::now() - transferParam->lastStartTick)
                              .count();

    double byteps = (elapsedSeconds > 0)
                        ? static_cast<double>(transferParam->lastTransferred) / elapsedSeconds
                        : 0;
    mbps = (byteps * 8) / (1000 * 1000); // Convert to megabits per second
}

TransferHandle::TransferHandle() : isochHandle(nullptr), returnCode(0), inUse(false) {}

TransferHandle::~TransferHandle() {
    if (isochHandle) {
        libusb_free_transfer(isochHandle);
    }
}

// Move constructor
TransferHandle::TransferHandle(TransferHandle &&other) noexcept
    : data(std::move(other.data)), isochHandle(other.isochHandle),
      returnCode(other.returnCode.load()), inUse(other.inUse.load()) {
    other.isochHandle = nullptr;
}

// Move assignment operator
TransferHandle &TransferHandle::operator=(TransferHandle &&other) noexcept {
    if (this != &other) {
        data = std::move(other.data);
        isochHandle = other.isochHandle;
        returnCode.store(other.returnCode.load());
        inUse.store(other.inUse.load());
        other.isochHandle = nullptr;
    }
    return *this;
}

TransferParams::TransferParams()
    : vid(0), pid(0), intf(-1), altf(-1), endpoint(0), transferMode(TransferMode::Sync),
      testType(TransferType::BulkIn), bufferLength(512), bufferCount(1), readLength(512),
      writeLength(512), isRunning(false), isCancelled(false), isUserAborted(false),
      interfaceHandle(nullptr), fileIO(false), verifyBufferSize(0), lastTransferred(0),
      totalTransferred(0), packets(0), totalErrorCount(0), runningErrorCount(0),
      runningTimeoutCount(0), list(true), verbose(false) {}

} // namespace UVPerf
