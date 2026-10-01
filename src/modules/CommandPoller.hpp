#pragma once

#include "config/Config.hpp"
#include "modules/SharedWorkers.hpp"
#include "platform/ChangeSignal.hpp"
#include "platform/StoppableThread.hpp"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

enum class CommandOutcome : std::uint8_t {
    Pending, // the first run has not finished yet
    Succeeded,
    Failed
};

struct CommandResult {
    CommandOutcome             outcome;
    std::string                text;       // Succeeded: the value, safe to draw. Failed: a reason short enough for the bar.
    std::optional<std::string> tooltip;    // from json output, shown instead of the tooltip format, may hold line breaks
    bool                       alert;      // from json output, draws the value in the alert color
    std::optional<size_t>      classIndex; // from json output, which declared class draws the value
    std::string                detail;     // Failed: the full failure, for the log and the tooltip

    bool                       operator==(const CommandResult&) const = default;
};

// What a successful run printed, read as format says. Output is untrusted: text must be UTF-8 with no control or
// bidirectional override characters, a tooltip may also hold line breaks, and a json class must be one of classes and
// cannot come with alert. Anything else is a Failed result.
CommandResult ParseOutput(std::string_view output, CustomOutput format, const std::vector<std::string>& classes);

// Runs a custom module's command on its own thread, first at once and then every interval, keeping the latest result
// and notifying changes whenever it changes.
class CommandPoller {
  public:
    CommandPoller(std::string name, const CustomConfig& config, ChangeSignal& changes);
    CommandPoller(const CommandPoller&)            = delete;
    CommandPoller& operator=(const CommandPoller&) = delete;

    CommandResult  Result() const;

  private:
    void                     Run(HANDLE stopEvent);
    CommandResult            RunOnce(HANDLE stopEvent) const;
    void                     Publish(const CommandResult& result);

    std::string              _name;
    Command                  _command;
    std::chrono::seconds     _interval;
    std::chrono::seconds     _timeout;
    CustomOutput             _output;
    std::vector<std::string> _classes;
    ChangeSignal&            _changes;
    mutable std::mutex       _mutex;
    CommandResult            _result;
    // Declared last so every member above exists before the thread starts. Destruction kills a run in progress rather
    // than waiting for it.
    StoppableThread _thread;
};

using CommandPollers = SharedWorkers<CommandPoller, CustomConfig, ModuleKind::Custom>;
