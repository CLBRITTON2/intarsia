#pragma once

#include "config/Config.hpp"
#include "modules/SharedWorkers.hpp"
#include "platform/ChangeSignal.hpp"
#include "platform/StoppableThread.hpp"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

enum class CounterOutcome : std::uint8_t {
    Pending, // no sample with a value yet
    Sampled,
    Failed
};

struct CounterReading {
    CounterOutcome outcome;
    double         value;  // Sampled: the instances combined
    std::string    detail; // Failed: why, for the log and the tooltip

    bool           operator==(const CounterReading&) const = default;
};

// The instances' values made one by combine, 0 when there are none: a wildcard matching nothing, such as GPU engines
// while no process draws, means nothing is counted.
double CombineCounterValues(const std::vector<double>& values, CounterCombine combine);

// Whether next must be drawn now rather than on the module's next tick: its outcome or failure differs from previous.
bool ChangesOutcome(const CounterReading& previous, const CounterReading& next);

// Samples a counter module's counter on its own thread every interval, keeping the latest reading. It notifies changes
// only when ChangesOutcome, since the module draws new values on its own interval tick and a notify per sample would
// wake every bar a second time each interval. A counter that fails is added again on the next interval, since one can
// come back (a GPU driver restarting).
class CounterSampler {
  public:
    CounterSampler(std::string name, const CounterConfig& config, ChangeSignal& changes);
    CounterSampler(const CounterSampler&)            = delete;
    CounterSampler& operator=(const CounterSampler&) = delete;

    CounterReading  Reading() const;

  private:
    void                 Run(HANDLE stopEvent);
    void                 Publish(const CounterReading& reading);

    std::string          _name;
    std::wstring         _path;
    CounterCombine       _combine;
    std::chrono::seconds _interval;
    ChangeSignal&        _changes;
    mutable std::mutex   _mutex;
    CounterReading       _reading;
    // Declared last so every member above exists before the thread starts.
    StoppableThread _thread;
};

using CounterSamplers = SharedWorkers<CounterSampler, CounterConfig, ModuleKind::Counter>;
