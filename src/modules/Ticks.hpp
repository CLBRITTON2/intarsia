#pragma once

#include <chrono>

// Time left until the next whole multiple of interval since the epoch, so a 60 s clock ticks on the minute and 1 s
// gauges tick on the second together.
std::chrono::milliseconds TimeUntilNextTick(std::chrono::system_clock::time_point now, std::chrono::seconds interval);

// Whether a sample taken at sampledAt is due again at now, for a module that samples at most once per interval: the bar
// also lays it out for other modules' changes and every animation frame. The 50 ms slack is for the redraw timer firing
// a little early.
bool SampleDue(std::chrono::steady_clock::time_point sampledAt, std::chrono::steady_clock::time_point now, std::chrono::seconds interval);
