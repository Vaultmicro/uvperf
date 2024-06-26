#ifndef UVPERF_H
#define UVPERF_H

#ifdef __linux__
#include <libusb-1.0/libusb.h>
#elif defined(__APPLE__)
#include <libusb.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace UVPerf {

enum class TransferMode { Async, Sync };
enum class TransferType { BulkIn, BulkOut, IsochronousIn };

class TransferHandle {
  public:
    TransferHandle();
    ~TransferHandle();

    // Move constructor
    TransferHandle(TransferHandle &&other) noexcept;
    // Move assignment operator
    TransferHandle &operator=(TransferHandle &&other) noexcept;
    // Deleted copy constructor and copy assignment operator
    TransferHandle(const TransferHandle &) = delete;
    TransferHandle &operator=(const TransferHandle &) = delete;

    std::vector<uint8_t> data;
    libusb_transfer *isochHandle;
    std::atomic<int> returnCode;
    std::atomic<bool> inUse;
};

class TransferParams {
  public:
    TransferParams();
    uint16_t vid;
    uint16_t pid;
    int intf;
    int altf;
    uint8_t endpoint;
    TransferMode transferMode;
    TransferType testType;
    uint32_t bufferLength;
    uint32_t bufferCount;
    uint32_t readLength;
    uint32_t writeLength;
    std::atomic<bool> isRunning;
    std::atomic<bool> isCancelled;
    std::atomic<bool> isUserAborted;
    libusb_device_handle *interfaceHandle;
    std::vector<TransferHandle> transferHandles;
    bool fileIO;
    bool verbose;
    bool list;
    bool showTransfer;
    std::string logFileName;
    std::ofstream logFile;

    libusb_endpoint_descriptor epDescriptor;
    std::unique_ptr<uint8_t[]> verifyBuffer;
    uint16_t verifyBufferSize;
    libusb_config_descriptor *configDescriptor;
    std::chrono::steady_clock::time_point startTick;
    std::chrono::steady_clock::time_point lastStartTick;
    std::chrono::steady_clock::time_point lastTick;
    size_t lastTransferred;
    size_t totalTransferred;
    size_t packets;
    size_t totalErrorCount;
    size_t runningErrorCount;
    size_t runningTimeoutCount;
    std::thread transferThread;
};

class UVPerf {
  public:
    UVPerf();
    ~UVPerf();
    bool initialize(int argc, char **argv);
    void run();
    int getDeviceInfoFromList();
    int getEndpointFromList();

  private:
    std::shared_ptr<TransferParams> inTest;
    std::shared_ptr<TransferParams> outTest;
    std::shared_ptr<TransferParams> transferParam;

    void setParamsDefaults();
    bool parseArgs(int argc, char **argv);
    void showUsage() const;
    bool openDevice();
    bool createTransferParams();
    void freeTransferParams();
    void showTransferParams() const;
    void transferLoop();
    void waitForTransferCompletion();
    void openLogFile();
    void logTransferParams();
    void closeLogFile();
};

} // namespace UVPerf

#endif // UVPERF_H
