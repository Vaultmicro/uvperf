#ifndef TRANSFER_H
#define TRANSFER_H


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

class TransferParams;
class TransferHandle;

class TransferStatus {
  public:
    static void showRunningStatus(const std::shared_ptr<TransferParams> &readParam,
                                  const std::shared_ptr<TransferParams> &writeParam);

  private:
    static void getAverageBitSec(const std::shared_ptr<TransferParams> &transferParam,
                                   double &Mbps);
    static void getCurrentBitSec(const std::shared_ptr<TransferParams> &transferParam,
                                   double &Mbps);
    static std::mutex displayMutex;
};

class Transfer {
  public:
    Transfer();
    ~Transfer();

    int verifyData(const std::shared_ptr<TransferParams> &transferParam,
                   const std::vector<uint8_t> &data);
    int transferSync(const std::shared_ptr<TransferParams> &transferParam);
    int transferAsync(const std::shared_ptr<TransferParams> &transferParam,
                      TransferHandle *&handleRef);
    int createVerifyBuffer(const std::shared_ptr<TransferParams> &testParam,
                           uint16_t endpointMaxPacketSize);
    void freeTransferParam(std::shared_ptr<TransferParams> &transferParamRef);
    void createTransferParam(const std::shared_ptr<TransferParams> &testParam, int endpointID);
    void getAverageBytesSec(const std::shared_ptr<TransferParams> &transferParam, double &byteps);
    void getCurrentBytesSec(const std::shared_ptr<TransferParams> &transferParam, double &byteps);
    void showTransfer(const std::shared_ptr<TransferParams> &transferParam);
    bool waitForTestTransfer(const std::shared_ptr<TransferParams> &transferParam,
                             unsigned int msToWait);

  private:
    void transferThread(std::shared_ptr<TransferParams> transferParam);
};

} // namespace UVPerf

#endif // TRANSFER_H