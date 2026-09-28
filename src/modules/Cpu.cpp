#include "modules/Cpu.hpp"

#include "platform/Errors.hpp"
#include "platform/Text.hpp"

#include <windows.h>

#include <cstdint>
#include <format>
#include <stdexcept>
#include <utility>

namespace {

    std::uint64_t Ticks(const FILETIME& time) {
        return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    }

    CpuTimes ReadCpuTimes() {
        FILETIME idle{};
        FILETIME kernel{};
        FILETIME user{};
        if (!GetSystemTimes(&idle, &kernel, &user)) {
            ThrowLastError("GetSystemTimes");
        }
        return CpuTimes{.idle = Ticks(idle), .kernel = Ticks(kernel), .user = Ticks(user)};
    }

} // namespace

std::int32_t CpuUsagePercent(const CpuTimes& before, const CpuTimes& after) {
    const std::uint64_t idle  = after.idle - before.idle;
    const std::uint64_t total = (after.kernel - before.kernel) + (after.user - before.user);
    if (idle > total) {
        throw std::runtime_error(std::format("GetSystemTimes reported more idle time ({}) than kernel plus user time ({}) between samples", idle, total));
    }
    if (total == 0) {
        return 0;
    }
    return static_cast<std::int32_t>(((total - idle) * 100 + (total / 2)) / total);
}

Cpu::Cpu(TextModuleConfig config) : TextModule(std::move(config), "cpu", CpuPlaceholders(0)), _previous(ReadCpuTimes()), _sampledAt(std::chrono::steady_clock::now()) {}

// Samples at most once per interval: the bar also redraws for other modules, and a sample over a few milliseconds
// would swing between 0 and 100. The 50 ms slack is for the redraw timer firing a little early.
ModuleText Cpu::Format(const std::string& spec) {
    const auto now = std::chrono::steady_clock::now();
    if (now - _sampledAt >= Settings().interval - std::chrono::milliseconds(50)) {
        const CpuTimes current = ReadCpuTimes();
        _usage                 = CpuUsagePercent(_previous, current);
        _previous              = current;
        _sampledAt             = now;
    }
    return ModuleText{.icon = Settings().icon, .text = ToWide(FormatPlaceholders(spec, CpuPlaceholders(_usage))), .state = TextState::Normal};
}

std::vector<Placeholder> CpuPlaceholders(std::int32_t usagePercent) {
    return {{.name = "usage", .value = std::int64_t{usagePercent}}};
}
