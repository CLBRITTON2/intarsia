#pragma once

#include <chrono>

// Time left until the next whole multiple of interval since the epoch, so a 60 s clock ticks on the minute and 1 s
// gauges tick on the second together.
std::chrono::milliseconds TimeUntilNextTick(std::chrono::system_clock::time_point now, std::chrono::seconds interval);
