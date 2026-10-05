#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// Time left until the next whole multiple of interval since the epoch, so a 60 s clock ticks on the minute and 1 s
// gauges tick on the second together.
std::chrono::milliseconds TimeUntilNextTick(std::chrono::system_clock::time_point now, std::chrono::seconds interval);

// Whether a sample taken at sampledAt is due again at now, for a module that samples at most once per interval: the bar
// also lays it out for other modules' changes and every animation frame. The 50 ms slack is for the redraw timer firing
// a little early.
bool SampleDue(std::chrono::steady_clock::time_point sampledAt, std::chrono::steady_clock::time_point now, std::chrono::seconds interval);

// The text each spec (the format, the tooltip) gave for the current sample, so a layout between samples formats
// nothing.
class SampledTexts {
  public:
    explicit SampledTexts(std::chrono::steady_clock::time_point sampledAt);
    // Calls sample and drops every text when a sample is due at now (SampleDue).
    void SampleIfDue(std::chrono::steady_clock::time_point now, std::chrono::seconds interval, const std::function<void()>& sample);
    // spec's text, from format the first time since the last sample.
    const std::wstring& Of(const std::string& spec, const std::function<std::wstring()>& format);

  private:
    std::chrono::steady_clock::time_point             _sampledAt;
    std::vector<std::pair<std::string, std::wstring>> _texts;
};
