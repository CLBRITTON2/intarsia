#include "modules/Cpu.hpp"

#include "modules/Ticks.hpp"
#include "platform/Errors.hpp"
#include "platform/Text.hpp"

#include <windows.h>

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

int CpuUsagePercent(const CpuTimes& before, const CpuTimes& after) {
    const std::uint64_t idle  = after.idle - before.idle;
    const std::uint64_t total = (after.kernel - before.kernel) + (after.user - before.user);
    if (idle > total) {
        throw std::runtime_error(std::format("GetSystemTimes reported more idle time ({}) than kernel plus user time ({}) between samples", idle, total));
    }
    if (total == 0) {
        return 0;
    }
    return static_cast<int>(((total - idle) * 100 + (total / 2)) / total);
}

Cpu::Cpu(TextModuleConfig config) :
    config_(std::move(config)), previous_(ReadCpuTimes()), sampledAt_(std::chrono::steady_clock::now()), usage_(0),
    text_{.layout = nullptr, .size = {.width = 0.0f, .height = 0.0f}} {
    RequireFormat("cpu.format", config_.format, 0);
}

// Samples at most once per interval: the bar also redraws for other modules, and a sample over a few milliseconds
// would swing between 0 and 100. The 50 ms slack is for the redraw timer firing a little early.
void Cpu::Layout(const RenderContext& render) {
    const auto now = std::chrono::steady_clock::now();
    if (now - sampledAt_ >= config_.interval - std::chrono::milliseconds(50)) {
        const CpuTimes current = ReadCpuTimes();
        usage_                 = CpuUsagePercent(previous_, current);
        previous_              = current;
        sampledAt_             = now;
    }
    text_ = LayOutIconText(render, config_.icon, ToWide(FormatValue(config_.format, usage_)));
}

D2D1_SIZE_F Cpu::Size() const {
    return text_.size;
}

void Cpu::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    render.context->DrawTextLayout(origin, text_.layout.Get(), render.foreground);
}

std::optional<UINT> Cpu::MillisecondsUntilChange() const {
    return static_cast<UINT>(TimeUntilNextTick(std::chrono::system_clock::now(), config_.interval).count());
}

void                 Cpu::Click(D2D1_POINT_2F) {}

const ModuleActions& Cpu::Actions() const {
    return config_.actions;
}
