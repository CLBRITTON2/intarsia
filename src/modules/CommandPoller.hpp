#pragma once

#include "config/Config.hpp"
#include "platform/ChangeSignal.hpp"
#include "platform/Handle.hpp"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

enum class CommandOutcome : std::uint8_t {
    Pending, // the first run has not finished yet
    Succeeded,
    Failed
};

struct CommandResult {
    CommandOutcome             outcome;
    std::string                text;    // Succeeded: the value, safe to draw. Failed: a reason short enough for the bar.
    std::optional<std::string> tooltip; // from json output, shown instead of the tooltip format, may hold line breaks
    bool                       alert;   // from json output, draws the value in the alert color
    std::string                detail;  // Failed: the full failure, for the log and the tooltip

    bool                       operator==(const CommandResult&) const = default;
};

// The first line of text, without its line ending.
std::string_view FirstLine(std::string_view text);

// What a successful run printed, read as format says. Output is untrusted: text must be UTF-8 with no control or
// bidirectional override characters, and a tooltip may also hold line breaks. Anything else is a Failed result.
CommandResult ParseOutput(std::string_view output, CustomOutput format);

// Runs a custom module's command on its own thread, first at once and then every interval, keeping the latest result
// and notifying changes whenever it changes.
class CommandPoller {
  public:
    CommandPoller(std::string name, const CustomConfig& config, ChangeSignal& changes);
    // Kills a run in progress rather than waiting for it.
    ~CommandPoller();
    CommandPoller(const CommandPoller&)            = delete;
    CommandPoller& operator=(const CommandPoller&) = delete;

    CommandResult  Result() const;

  private:
    void                 Run();
    CommandResult        RunOnce() const;
    void                 Publish(CommandResult result);

    std::string          _name;
    Command              _command;
    std::chrono::seconds _interval;
    std::chrono::seconds _timeout;
    CustomOutput         _output;
    ChangeSignal&        _changes;
    UniqueHandle         _stopEvent;
    mutable std::mutex   _mutex;
    CommandResult        _result;
    // Declared last so every member above exists before the thread starts.
    std::thread _thread;
};

// One poller per custom module name, shared by every bar that shows it, so each command runs once however many monitors
// there are.
class CommandPollers {
  public:
    CommandPollers(const Config& config, ChangeSignal& changes);

    // Throws std::logic_error for a name the config defines no custom module under.
    const CommandPoller& For(const std::string& name) const;

  private:
    std::map<std::string, std::unique_ptr<CommandPoller>> _pollers;
};
