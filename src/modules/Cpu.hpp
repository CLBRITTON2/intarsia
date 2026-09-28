#pragma once

#include "config/Config.hpp"
#include "modules/Placeholders.hpp"
#include "modules/TextModule.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

// Cumulative CPU times across all cores, in 100 ns units, as GetSystemTimes reports them.
struct CpuTimes {
    std::uint64_t idle;
    std::uint64_t kernel; // includes idle time
    std::uint64_t user;
};

// Busy share of the time between two samples, 0 to 100.
std::int32_t CpuUsagePercent(const CpuTimes& before, const CpuTimes& after);

// The values a cpu format can name: {usage}, a whole percent.
std::vector<Placeholder> CpuPlaceholders(std::int32_t usagePercent);

// CPU busy time across all cores since the last sample: format = "{usage}%".
class Cpu final : public TextModule {
  public:
    // Throws ConfigError when a format names another value or has a spec that cannot render an integer.
    explicit Cpu(TextModuleConfig config);

  private:
    ModuleText                            Format(const std::string& spec) override;

    CpuTimes                              _previous;
    std::chrono::steady_clock::time_point _sampledAt;
    std::int32_t                          _usage = 0;
};
