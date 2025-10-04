#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <chrono>
#include <cstdint>

uint64_t now_ns_monotonic() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()
           ).count();
}


#endif
