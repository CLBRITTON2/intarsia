#include "modules/CounterSampler.hpp"

#include "platform/Log.hpp"
#include "platform/PerformanceCounter.hpp"
#include "platform/Text.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <numeric>
#include <stdexcept>
#include <utility>

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
    _stopEvent(OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(counter sampler stop)")),
    _reading{.outcome = CounterOutcome::Pending, .value = 0.0, .detail = {}}, _thread([this] { Run(); }) {}

CounterSampler::~CounterSampler() {
    SetEvent(_stopEvent.get());
    _thread.join();
}

CounterReading CounterSampler::Reading() const {
    const std::scoped_lock lock(_mutex);
    return _reading;
}

// A freshly added counter's first sample only starts its rates, so it publishes nothing unless it fails.
void CounterSampler::Run() {
    std::unique_ptr<PerformanceCounter> counter;
    while (true) {
        const auto started = std::chrono::steady_clock::now();
        try {
            if (counter == nullptr) {
                counter = std::make_unique<PerformanceCounter>(_path);
                counter->Collect();
            } else {
                counter->Collect();
                Publish(CounterReading{.outcome = CounterOutcome::Sampled, .value = CombineCounterValues(counter->Values(), _combine), .detail = {}});
            }
        } catch (const PdhError& error) {
            counter.reset();
            Publish(CounterReading{.outcome = CounterOutcome::Failed, .value = 0.0, .detail = error.what()});
        }
        const auto wait = std::max(std::chrono::duration_cast<std::chrono::milliseconds>(started + _interval - std::chrono::steady_clock::now()), std::chrono::milliseconds(0));
        if (WaitForSingleObject(_stopEvent.get(), static_cast<DWORD>(wait.count())) == WAIT_OBJECT_0) {
            return;
        }
    }
}

// Posts and logs only on a change, so a counter that keeps failing the same way logs once.
void CounterSampler::Publish(CounterReading reading) {
    {
        const std::scoped_lock lock(_mutex);
        if (reading == _reading) {
            return;
        }
        _reading = reading;
    }
    if (reading.outcome == CounterOutcome::Failed) {
        Log(LogLevel::Warning, "counter", "sample_failed",
            {{.key = "module", .value = _name}, {.key = "counter", .value = ToUtf8(_path)}, {.key = "reason", .value = reading.detail}});
    }
    _changes.Notify("counter");
}

CounterSamplers::CounterSamplers(const Config& config, ChangeSignal& changes) {
    ForEachModuleInConfig(config, [&](const ModuleInstance& instance) {
        if (instance.kind == ModuleKind::Counter && !_samplers.contains(instance.name)) {
            _samplers.emplace(instance.name, std::make_unique<CounterSampler>(instance.name, std::get<CounterConfig>(instance.settings), changes));
        }
    });
}

const CounterSampler& CounterSamplers::For(const std::string& name) const {
    const auto sampler = _samplers.find(name);
    if (sampler == _samplers.end()) {
        throw std::logic_error(std::format("no counter sampler for module {}, CounterSamplers should have made one from the config", name));
    }
    return *sampler->second;
}
