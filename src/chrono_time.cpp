#include "time.h"
#include <chrono>

extern "C" {
void get_time(struct timespec *ts) {
    if (ts == nullptr) {
        return;
    }

    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    
    std::chrono::seconds sec =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());

    std::chrono::nanoseconds nsec =
        std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()) -
        std::chrono::duration_cast<std::chrono::nanoseconds>(sec);

    ts->tv_sec = static_cast<time_t>(sec.count());
    ts->tv_nsec = static_cast<long>(nsec.count());
}
}
