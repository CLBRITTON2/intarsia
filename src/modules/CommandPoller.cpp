#include "modules/CommandPoller.hpp"

#include "bar/Sections.hpp"
#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Process.hpp"
#include "platform/Text.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <utility>
#include <variant>

namespace {

    // Far more than one line on a bar needs. Output past it fails the run instead of growing without bound.
    constexpr size_t kMaxOutputBytes = size_t{64} * 1024;

} // namespace

std::string_view FirstLine(std::string_view text) {
    std::string_view line = text.substr(0, text.find('\n'));
    if (line.ends_with('\r')) {
        line.remove_suffix(1);
    }
    return line;
}

CommandPoller::CommandPoller(std::string name, const CustomConfig& config, HWND target, UINT changedMessage) :
    _name(std::move(name)), _command(config.command), _interval(config.text.interval), _timeout(config.timeout), _target(target), _changedMessage(changedMessage),
    _stopEvent(OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(command poller stop)")), _result{.outcome = CommandOutcome::Pending, .text = {}, .detail = {}},
    _thread([this] { Run(); }) {}

CommandPoller::~CommandPoller() {
    SetEvent(_stopEvent.get());
    _thread.join();
}

CommandResult CommandPoller::Result() const {
    const std::scoped_lock lock(_mutex);
    return _result;
}

void CommandPoller::Run() {
    while (true) {
        const auto          started = std::chrono::steady_clock::now();
        const CommandResult result  = RunOnce();
        // A run the destructor cut short says nothing about the command.
        if (WaitForSingleObject(_stopEvent.get(), 0) == WAIT_OBJECT_0) {
            return;
        }
        Publish(result);
        const auto wait = std::max(std::chrono::duration_cast<std::chrono::milliseconds>(started + _interval - std::chrono::steady_clock::now()), std::chrono::milliseconds(0));
        if (WaitForSingleObject(_stopEvent.get(), static_cast<DWORD>(wait.count())) == WAIT_OBJECT_0) {
            return;
        }
    }
}

// Runs on the poller thread, so every failure becomes a result instead of an exception.
CommandResult CommandPoller::RunOnce() const {
    std::string output;
    try {
        output = CaptureOutput(_command, _timeout, kMaxOutputBytes, _stopEvent.get());
    } catch (const CommandError& error) { return CommandResult{.outcome = CommandOutcome::Failed, .text = error.what(), .detail = error.what()}; } catch (const Win32Error& error) {
        return CommandResult{.outcome = CommandOutcome::Failed, .text = "could not run", .detail = error.what()};
    }
    const std::string_view line = FirstLine(output);
    // Checked here so the UI thread never meets text it cannot convert.
    try {
        ToWide(line);
    } catch (const Win32Error& error) { return CommandResult{.outcome = CommandOutcome::Failed, .text = "printed text that is not UTF-8", .detail = error.what()}; }
    return CommandResult{.outcome = CommandOutcome::Succeeded, .text = std::string(line), .detail = {}};
}

// Posts and logs only on a change, so a command that keeps failing the same way logs once.
void CommandPoller::Publish(CommandResult result) {
    {
        const std::scoped_lock lock(_mutex);
        if (result == _result) {
            return;
        }
        _result = result;
    }
    const std::string program = ToUtf8(_command.program.wstring());
    if (result.outcome == CommandOutcome::Failed) {
        Log(LogLevel::Warning, "custom", "command_failed", {{.key = "module", .value = _name}, {.key = "program", .value = program}, {.key = "reason", .value = result.detail}});
    }
    if (!PostMessageW(_target, _changedMessage, 0, 0)) {
        Log(LogLevel::Warning, "custom", "post_failed", {{.key = "module", .value = _name}, {.key = "error", .value = std::to_string(GetLastError())}});
    }
}

CommandPollers::CommandPollers(const Config& config, HWND target, UINT changedMessage) {
    ForEachInSections(config.modules, [&](const ModuleInstance& instance) {
        if (instance.kind == ModuleKind::Custom && !_pollers.contains(instance.name)) {
            _pollers.emplace(instance.name, std::make_unique<CommandPoller>(instance.name, std::get<CustomConfig>(instance.settings), target, changedMessage));
        }
    });
}

const CommandPoller& CommandPollers::For(const std::string& name) const {
    const auto poller = _pollers.find(name);
    if (poller == _pollers.end()) {
        throw std::logic_error(std::format("no command poller for module {}, CommandPollers should have made one from the config", name));
    }
    return *poller->second;
}
