#include "modules/Ticks.hpp"

std::chrono::milliseconds TimeUntilNextTick(std::chrono::system_clock::time_point now, std::chrono::seconds interval) {
    const auto sinceEpoch = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    const auto period     = std::chrono::duration_cast<std::chrono::milliseconds>(interval);
    return period - (sinceEpoch % period);
}
