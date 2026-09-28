#include "modules/Cpu.hpp"

#include "platform/Errors.hpp"
#include "platform/Text.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

    bool Names(const TextModuleConfig& config, std::string_view name) {
        return FormatNames(config.format, name) || (config.tooltip.has_value() && FormatNames(*config.tooltip, name));
    }

    // Started with a first sample, so a rate has its value from the next one.
    std::unique_ptr<PerformanceCounter> CounterIfNamed(const TextModuleConfig& config, std::initializer_list<std::string_view> names, const wchar_t* path) {
        if (std::ranges::none_of(names, [&config](std::string_view name) { return Names(config, name); })) {
            return nullptr;
        }
        auto counter = std::make_unique<PerformanceCounter>(path);
        counter->Collect();
        return counter;
    }

    // The counter's one value, or 0 before it has one.
    double SingleValue(const PerformanceCounter& counter) {
        const std::vector<double> values = counter.Values();
        return values.empty() ? 0.0 : values.front();
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

double CpuGigahertz(double baseMegahertz, double performancePercent) {
    return baseMegahertz * performancePercent / 100.0 / 1000.0;
}

Cpu::Cpu(TextModuleConfig config) :
    TextModule(std::move(config), "cpu", CpuPlaceholders(CpuSample{.usagePercent = 0, .gigahertz = 0.0, .maxCorePercent = 0, .cores = {}})), _previous(ReadCpuTimes()),
    _sampledAt(std::chrono::steady_clock::now()), _baseFrequency(CounterIfNamed(Settings(), {"ghz"}, L"\\Processor Information(_Total)\\Processor Frequency")),
    _performance(CounterIfNamed(Settings(), {"ghz"}, L"\\Processor Information(_Total)\\% Processor Performance")),
    _cores(CounterIfNamed(Settings(), {"max_core", "cores"}, L"\\Processor(*)\\% Processor Time")) {}

// Samples at most once per interval: the bar also redraws for other modules, and a sample over a few milliseconds
// would swing between 0 and 100. The 50 ms slack is for the redraw timer firing a little early.
ModuleText Cpu::Format(const std::string& spec) {
    const auto now = std::chrono::steady_clock::now();
    if (now - _sampledAt >= Settings().interval - std::chrono::milliseconds(50)) {
        Sample();
        _sampledAt = now;
    }
    return ModuleText{.icon = Settings().icon, .text = ToWide(FormatPlaceholders(spec, CpuPlaceholders(_sample))), .state = TextState::Normal, .classIndex = 0};
}

void Cpu::Sample() {
    const CpuTimes current = ReadCpuTimes();
    _sample.usagePercent   = CpuUsagePercent(_previous, current);
    _previous              = current;
    // A failed sample blanks the counters' values until one succeeds, rather than ending the bar over a tooltip.
    try {
        if (_baseFrequency != nullptr && _performance != nullptr) {
            _baseFrequency->Collect();
            _performance->Collect();
            _sample.gigahertz = CpuGigahertz(SingleValue(*_baseFrequency), SingleValue(*_performance));
        }
        if (_cores != nullptr) {
            _cores->Collect();
            // _Total is among the instances and never passes the busiest core.
            const std::vector<CounterInstance> cores = _cores->Instances();
            const auto                         max   = std::ranges::max_element(cores, {}, &CounterInstance::value);
            _sample.maxCorePercent                   = max == cores.end() ? 0 : static_cast<std::int32_t>(std::lround(max->value));
            _sample.cores                            = FormatCoreUsage(cores);
        }
        _counterLog.Succeeded();
    } catch (const PdhError& error) {
        _counterLog.Failed("counter_failed", error);
        _sample.gigahertz      = 0.0;
        _sample.maxCorePercent = 0;
        _sample.cores.clear();
    }
}

std::string FormatCoreUsage(const std::vector<CounterInstance>& instances) {
    constexpr size_t                                    kPerLine = 4;
    std::vector<std::pair<std::uint32_t, std::int64_t>> cores;
    for (const CounterInstance& instance : instances) {
        if (instance.name.empty() || !std::ranges::all_of(instance.name, [](wchar_t unit) { return unit >= L'0' && unit <= L'9'; })) {
            continue;
        }
        cores.emplace_back(static_cast<std::uint32_t>(std::stoul(instance.name)), std::lround(instance.value));
    }
    std::ranges::sort(cores);
    std::string text;
    for (size_t index = 0; index < cores.size(); ++index) {
        if (index > 0) {
            text += index % kPerLine == 0 ? "\n" : "  ";
        }
        text += std::format("{:>2}: {:>3}%", cores[index].first, cores[index].second);
    }
    return text;
}

std::vector<Placeholder> CpuPlaceholders(const CpuSample& sample) {
    return {{.name = "usage", .value = std::int64_t{sample.usagePercent}},
            {.name = "ghz", .value = sample.gigahertz},
            {.name = "max_core", .value = std::int64_t{sample.maxCorePercent}},
            {.name = "cores", .value = sample.cores}};
}
