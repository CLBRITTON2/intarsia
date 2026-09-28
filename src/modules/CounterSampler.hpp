#pragma once

#include "config/Config.hpp"
#include "platform/ChangeSignal.hpp"
#include "platform/Handle.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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

// Samples a counter module's counter on its own thread every interval, keeping the latest reading and notifying
// changes whenever it changes. A counter that fails is added again on the next interval, since one can come back
// (a GPU driver restarting).
class CounterSampler {
  public:
    CounterSampler(std::string name, const CounterConfig& config, ChangeSignal& changes);
    ~CounterSampler();
    CounterSampler(const CounterSampler&)            = delete;
    CounterSampler& operator=(const CounterSampler&) = delete;

    CounterReading  Reading() const;

  private:
    void                 Run();
    void                 Publish(CounterReading reading);

    std::string          _name;
    std::wstring         _path;
    CounterCombine       _combine;
    std::chrono::seconds _interval;
    ChangeSignal&        _changes;
    UniqueHandle         _stopEvent;
    mutable std::mutex   _mutex;
    CounterReading       _reading;
    // Declared last so every member above exists before the thread starts.
    std::thread _thread;
};

// One sampler per counter module name, shared by every bar that shows it.
class CounterSamplers {
  public:
    CounterSamplers(const Config& config, ChangeSignal& changes);

    // Throws std::logic_error for a name the config defines no counter module under.
    const CounterSampler& For(const std::string& name) const;

  private:
    std::map<std::string, std::unique_ptr<CounterSampler>> _samplers;
};
