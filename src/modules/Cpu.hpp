#pragma once

#include "config/Config.hpp"
#include "modules/Placeholders.hpp"
#include "modules/TextModule.hpp"
#include "platform/PerformanceCounter.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Cumulative CPU times across all cores, in 100 ns units, as GetSystemTimes reports them.
struct CpuTimes {
    std::uint64_t idle;
    std::uint64_t kernel; // includes idle time
    std::uint64_t user;
};

// Busy share of the time between two samples, 0 to 100.
std::int32_t CpuUsagePercent(const CpuTimes& before, const CpuTimes& after);

// The clock speed Task Manager shows: the base frequency scaled by the processor's performance, which passes 100%
// while boosting.
double CpuGigahertz(double baseMegahertz, double performancePercent);

// Each logical processor's usage in its number's order, four to a line, as "0:  12%  1:   3%  ...". Instances that
// are not a processor's number (_Total) are left out.
std::string FormatCoreUsage(const std::vector<CounterInstance>& instances);

// What one cpu sample holds.
struct CpuSample {
    std::int32_t usagePercent;
    double       gigahertz;      // 0 until the performance counter's second sample
    std::int32_t maxCorePercent; // the busiest logical processor, 0 until its second sample
    std::string  cores;          // FormatCoreUsage, empty until its second sample
};

// The values a cpu format can name: {usage}, a whole percent, {ghz}, the current clock speed as a double, {max_core},
// the busiest logical processor as a whole percent, and {cores}, every logical processor's usage (FormatCoreUsage).
std::vector<Placeholder> CpuPlaceholders(const CpuSample& sample);

// CPU busy time across all cores since the last sample: format = "{usage}%". {ghz}, {max_core} and {cores} come from
// performance counters, read only when a format or the tooltip names them.
class Cpu final : public TextModule {
  public:
    // Throws ConfigError when a format names another value or has a spec that does not fit it, and PdhError when a
    // named counter cannot be added.
    explicit Cpu(TextModuleConfig config);

  private:
    ModuleText                            Format(const std::string& spec) override;
    void                                  Sample();

    CpuTimes                              _previous;
    std::chrono::steady_clock::time_point _sampledAt;
    // Null unless a format names their value.
    std::unique_ptr<PerformanceCounter> _baseFrequency;
    std::unique_ptr<PerformanceCounter> _performance;
    std::unique_ptr<PerformanceCounter> _cores;
    CpuSample                           _sample{.usagePercent = 0, .gigahertz = 0.0, .maxCorePercent = 0, .cores = {}};
};
