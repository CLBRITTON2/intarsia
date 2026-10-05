#pragma once

#include "config/Config.hpp"
#include "modules/Placeholders.hpp"
#include "modules/TextModule.hpp"
#include "modules/Ticks.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// Bytes as gibibytes (1024^3), the unit Task Manager calls GB.
double BytesToGibibytes(std::uint64_t bytes);

// The system's memory at one moment, in bytes.
struct MemoryUsage {
    std::uint64_t totalBytes;       // physical memory
    std::uint64_t availableBytes;   // physical memory free or on the standby list
    std::uint64_t cachedBytes;      // the system file cache
    std::uint64_t committedBytes;   // memory promised to processes, backed by RAM or the page file
    std::uint64_t commitLimitBytes; // what can be committed before the page file grows or allocations fail
};

// The values a memory format can name: {used}, {total}, {available}, {cached}, {committed} and {commit_limit}, each in
// gibibytes as a double, and {percent}, the share of physical memory in use as a whole percent.
std::vector<Placeholder> MemoryPlaceholders(const MemoryUsage& usage);

// Physical memory in use: format = "{used:.1f} GB".
class Memory final : public TextModule {
  public:
    // Throws ConfigError when a format names another value or has a spec that does not fit it.
    explicit Memory(TextModuleConfig config);

  private:
    ModuleText   Format(const std::string& spec) override;

    MemoryUsage  _usage;
    SampledTexts _texts;
};
