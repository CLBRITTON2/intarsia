#include "modules/Memory.hpp"

#include "platform/Errors.hpp"
#include "platform/Text.hpp"

#include <windows.h>

#include <psapi.h>

#include <cstdint>
#include <stdexcept>
#include <utility>

namespace {

    MemoryUsage ReadMemoryUsage() {
        PERFORMANCE_INFORMATION info{};
        info.cb = sizeof(info);
        if (!GetPerformanceInfo(&info, sizeof(info))) {
            ThrowLastError("GetPerformanceInfo");
        }
        // Every count is in pages.
        const std::uint64_t page = info.PageSize;
        return MemoryUsage{.totalBytes       = info.PhysicalTotal * page,
                           .availableBytes   = info.PhysicalAvailable * page,
                           .cachedBytes      = info.SystemCache * page,
                           .committedBytes   = info.CommitTotal * page,
                           .commitLimitBytes = info.CommitLimit * page};
    }

} // namespace

double BytesToGibibytes(std::uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
}

Memory::Memory(TextModuleConfig config) :
    TextModule(std::move(config), "memory", MemoryPlaceholders(MemoryUsage{.totalBytes = 1, .availableBytes = 0, .cachedBytes = 0, .committedBytes = 0, .commitLimitBytes = 0})) {}

ModuleText Memory::Format(const std::string& spec) {
    return ModuleText{.icon = Settings().icon, .text = ToWide(FormatPlaceholders(spec, MemoryPlaceholders(ReadMemoryUsage()))), .state = TextState::Normal, .classIndex = 0};
}

std::vector<Placeholder> MemoryPlaceholders(const MemoryUsage& usage) {
    if (usage.totalBytes == 0) {
        throw std::invalid_argument("memory usage has no physical memory, so no percent of it is in use");
    }
    const std::uint64_t used = usage.totalBytes - usage.availableBytes;
    return {{.name = "used", .value = BytesToGibibytes(used)},
            {.name = "total", .value = BytesToGibibytes(usage.totalBytes)},
            {.name = "available", .value = BytesToGibibytes(usage.availableBytes)},
            {.name = "cached", .value = BytesToGibibytes(usage.cachedBytes)},
            {.name = "committed", .value = BytesToGibibytes(usage.committedBytes)},
            {.name = "commit_limit", .value = BytesToGibibytes(usage.commitLimitBytes)},
            {.name = "percent", .value = static_cast<std::int64_t>(((used * 100) + (usage.totalBytes / 2)) / usage.totalBytes)}};
}
