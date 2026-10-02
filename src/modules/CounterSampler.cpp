#include "modules/CounterSampler.hpp"

#include "platform/Log.hpp"
#include "platform/PerformanceCounter.hpp"
#include "platform/Text.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

double CombineCounterValues(const std::vector<double>& values, CounterCombine combine) {
    if (values.empty()) {
        return 0.0;
    }
    switch (combine) {
        case CounterCombine::Sum: return std::accumulate(values.begin(), values.end(), 0.0);
        case CounterCombine::Max: return std::ranges::max(values);
        case CounterCombine::Average: return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    }
    throw std::invalid_argument(std::format("unknown counter combine {}", static_cast<std::int32_t>(combine)));
}

CounterSampler::CounterSampler(std::string name, const CounterConfig& config, ChangeSignal& changes) :
    _name(std::move(name)), _path(config.path), _combine(config.combine), _interval(config.text.interval), _changes(changes),
    _reading{.outcome = CounterOutcome::Pending, .value = 0.0, .detail = {}}, _thread("counter sampler", [this](HANDLE stopEvent) { Run(stopEvent); }) {}

CounterReading CounterSampler::Reading() const {
    const std::scoped_lock lock(_mutex);
    return _reading;
}

// A freshly added counter's first sample only starts its rates, and a sample without a value keeps the last reading,
// so neither publishes anything unless it fails.
void CounterSampler::Run(HANDLE stopEvent) {
    std::unique_ptr<PerformanceCounter> counter;
    while (true) {
        const auto started = std::chrono::steady_clock::now();
        try {
            if (counter == nullptr) {
                counter = std::make_unique<PerformanceCounter>(_path);
                counter->Collect();
            } else {
                counter->Collect();
                const std::optional<std::vector<double>> values = counter->Values();
                if (values.has_value()) {
                    Publish(CounterReading{.outcome = CounterOutcome::Sampled, .value = CombineCounterValues(*values, _combine), .detail = {}});
                }
            }
        } catch (const PdhError& error) {
            counter.reset();
            Publish(CounterReading{.outcome = CounterOutcome::Failed, .value = 0.0, .detail = error.what()});
        }
        const auto wait = std::max(std::chrono::duration_cast<std::chrono::milliseconds>(started + _interval - std::chrono::steady_clock::now()), std::chrono::milliseconds(0));
        if (WaitForSingleObject(stopEvent, static_cast<DWORD>(wait.count())) == WAIT_OBJECT_0) {
            return;
        }
    }
}

bool ChangesOutcome(const CounterReading& previous, const CounterReading& next) {
    return next.outcome != previous.outcome || next.detail != previous.detail;
}

// A counter that keeps failing the same way logs once.
void CounterSampler::Publish(const CounterReading& reading) {
    {
        const std::scoped_lock lock(_mutex);
        const bool             changesOutcome = ChangesOutcome(_reading, reading);
        _reading                              = reading;
        if (!changesOutcome) {
            return;
        }
    }
    if (reading.outcome == CounterOutcome::Failed) {
        Log(LogLevel::Warning, "counter", "sample_failed",
            {{.key = "module", .value = _name}, {.key = "counter", .value = ToUtf8(_path)}, {.key = "reason", .value = reading.detail}});
    }
    _changes.Notify("counter");
}
