#pragma once

#include "config/Config.hpp"
#include "platform/Handle.hpp"

#include <windows.h>

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

enum class CommandOutcome {
    Pending, // the first run has not finished yet
    Succeeded,
    Failed
};

struct CommandResult {
    CommandOutcome outcome;
    std::string    text;   // the first line of output when it succeeded, a reason short enough for the bar when it failed
    std::string    detail; // the full failure for the log, empty otherwise

    bool           operator==(const CommandResult&) const = default;
};

// The first line of text, without its line ending.
std::string_view FirstLine(std::string_view text);

// Runs a custom module's command on its own thread, first at once and then every interval, keeping the latest result
// and posting changedMessage to target whenever it changes.
class CommandPoller {
  public:
    CommandPoller(std::string name, const CustomConfig& config, HWND target, UINT changedMessage);
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
    HWND                 _target;
    UINT                 _changedMessage;
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
    CommandPollers(const Config& config, HWND target, UINT changedMessage);

    // Throws std::logic_error for a name the config defines no custom module under.
    const CommandPoller& For(const std::string& name) const;

  private:
    std::map<std::string, std::unique_ptr<CommandPoller>> _pollers;
};
