#include "modules/Memory.hpp"

#include "modules/TextModule.hpp"
#include "platform/Errors.hpp"
#include "platform/Text.hpp"

#include <windows.h>

#include <psapi.h>

#include <chrono>
#include <cstdint>
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
        return MemoryUsage{
            .totalBytes       = info.PhysicalTotal * page,
            .availableBytes   = info.PhysicalAvailable * page,
            .cachedBytes      = info.SystemCache * page,
            .committedBytes   = info.CommitTotal * page,
            .commitLimitBytes = info.CommitLimit * page,
        };
    }

    // Gibibytes (1024^3), the unit Task Manager calls GB.
    double BytesToGibibytes(std::uint64_t bytes) {
        return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    }

} // namespace

Memory::Memory(TextModuleConfig config) :
    TextModule(std::move(config), "memory",
               MemoryPlaceholders(MemoryUsage{
                   .totalBytes       = 1,
                   .availableBytes   = 0,
                   .cachedBytes      = 0,
                   .committedBytes   = 0,
                   .commitLimitBytes = 0,
               })),
    _usage(ReadMemoryUsage()), _texts(std::chrono::steady_clock::now()) {}

ModuleText Memory::Format(const std::string& spec) {
    _texts.SampleIfDue(std::chrono::steady_clock::now(), Settings().interval, [this] { _usage = ReadMemoryUsage(); });
    return NormalText(Settings().icon, _texts.Of(spec, [this, &spec] { return ToWide(FormatPlaceholders(spec, MemoryPlaceholders(_usage))); }));
}

std::vector<Placeholder> MemoryPlaceholders(const MemoryUsage& usage) {
    const std::uint64_t used = usage.totalBytes - usage.availableBytes;
    return {{.name = "used", .value = BytesToGibibytes(used)},
            {.name = "total", .value = BytesToGibibytes(usage.totalBytes)},
            {.name = "available", .value = BytesToGibibytes(usage.availableBytes)},
            {.name = "cached", .value = BytesToGibibytes(usage.cachedBytes)},
            {.name = "committed", .value = BytesToGibibytes(usage.committedBytes)},
            {.name = "commit_limit", .value = BytesToGibibytes(usage.commitLimitBytes)},
            {.name = "percent", .value = static_cast<std::int64_t>(((used * 100) + (usage.totalBytes / 2)) / usage.totalBytes)}};
}
