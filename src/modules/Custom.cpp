#include "modules/Custom.hpp"

#include "modules/Placeholders.hpp"
#include "platform/Log.hpp"
#include "platform/Process.hpp"
#include "platform/Text.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

    CommandResult Failure(std::string text, std::string detail) {
        return CommandResult{
            .outcome = CommandOutcome::Failed,
            .text = std::move(text),
            .tooltip = std::nullopt,
            .alert = false,
            .classIndex = std::nullopt,
            .detail = std::move(detail),
        };
    }

    // The first line of text, without its line ending.
    std::string_view FirstLine(std::string_view text) {
        std::string_view line = text.substr(0, text.find('\n'));
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        return line;
    }

    CommandResult ParseText(std::string_view output) {
        const std::string_view line = FirstLine(output);
        RequireDrawable(line, "output", L"");
        return CommandResult{
            .outcome = CommandOutcome::Succeeded,
            .text = std::string(line),
            .tooltip = std::nullopt,
            .alert = false,
            .classIndex = std::nullopt,
            .detail = {},
        };
    }

    std::optional<std::string> ReadTooltip(const nlohmann::json& value) {
        const auto found = value.find("tooltip");
        if (found == value.end()) {
            return std::nullopt;
        }
        if (!found->is_string()) {
            throw OutputError("printed a JSON tooltip that is not a string");
        }
        return found->get<std::string>();
    }

    // The failure never quotes the class, which is output.
    std::optional<size_t> ReadClass(const nlohmann::json& value, bool alert, const std::vector<std::string>& classes) {
        const auto found = value.find("class");
        if (found == value.end()) {
            return std::nullopt;
        }
        if (!found->is_string()) {
            throw OutputError("printed a JSON class that is not a string");
        }
        if (alert) {
            throw OutputError("printed both a JSON class and alert, and a module is in one state at a time");
        }
        const auto declared = std::ranges::find(classes, found->get<std::string>());
        if (declared == classes.end()) {
            throw OutputError("printed a JSON class the module's classes do not list");
        }
        return static_cast<size_t>(declared - classes.begin());
    }

    CommandResult ParseJson(std::string_view output, const std::vector<std::string>& classes) {
        const nlohmann::json value = nlohmann::json::parse(output.begin(), output.end(), nullptr, false);
        if (value.is_discarded()) {
            throw OutputError("printed invalid JSON");
        }
        if (!value.is_object()) {
            throw OutputError("printed JSON that is not an object");
        }
        for (const auto& [key, field] : value.items()) {
            if (key != "text" && key != "tooltip" && key != "alert" && key != "class") {
                throw OutputError("printed a JSON field other than text, tooltip, alert and class");
            }
        }
        const auto text = value.find("text");
        if (text == value.end() || !text->is_string()) {
            throw OutputError("printed JSON without a text string");
        }
        std::optional<std::string> tooltip = ReadTooltip(value);
        const auto alert = value.find("alert");
        if (alert != value.end() && !alert->is_boolean()) {
            throw OutputError("printed a JSON alert that is not true or false");
        }

        const bool alerted = alert != value.end() && alert->get<bool>();
        CommandResult result{
            .outcome = CommandOutcome::Succeeded,
            .text = text->get<std::string>(),
            .tooltip = std::move(tooltip),
            .alert = alerted,
            .classIndex = ReadClass(value, alerted, classes),
            .detail = {},
        };
        RequireDrawable(result.text, "text", L"");
        if (result.tooltip.has_value()) {
            RequireDrawable(*result.tooltip, "tooltip", L"\n");
        }
        return result;
    }

    // The values a custom format can name: {text}, what the command printed.
    std::vector<Placeholder> CustomPlaceholders(const std::string& text) {
        return {{.name = "text", .value = text}};
    }

    // A json alert draws in the alert color, a json class in that class's colors.
    TextState SucceededState(const CommandResult& result) {
        if (result.alert) {
            return TextState::Alert;
        }
        if (result.classIndex.has_value()) {
            return TextState::Classed;
        }
        return TextState::Normal;
    }

} // namespace

CommandResult ParseOutput(std::string_view output, CustomOutput format, const std::vector<std::string>& classes) {
    try {
        switch (format) {
            case CustomOutput::Text: return ParseText(output);
            case CustomOutput::Json: return ParseJson(output, classes);
        }
    } catch (const OutputError& error) { return Failure(error.what(), error.what()); }
    throw std::invalid_argument(std::format("unknown custom output {}", static_cast<std::int32_t>(format)));
}

CommandPoller::CommandPoller(std::string name, const CustomConfig& config, ChangeSignal& changes) :
    _name(std::move(name)), _command(config.command), _interval(config.text.interval), _run(config.run), _output(config.output), _classes(config.classes), _changes(changes),
    _result{
        .outcome = CommandOutcome::Pending,
        .text = {},
        .tooltip = std::nullopt,
        .alert = false,
        .classIndex = std::nullopt,
        .detail = {},
    },
    _thread("command poller", [this](HANDLE stopEvent) { Run(stopEvent); }) {}

CommandResult CommandPoller::Result() const {
    const std::scoped_lock lock(_mutex);
    return _result;
}

void CommandPoller::Run(HANDLE stopEvent) {
    if (const auto* interval = std::get_if<IntervalRun>(&_run)) {
        RunEveryInterval(stopEvent, interval->timeout);
    } else {
        RunContinuously(stopEvent);
    }
}

void CommandPoller::RunEveryInterval(HANDLE stopEvent, std::chrono::seconds timeout) {
    while (true) {
        const auto started = std::chrono::steady_clock::now();
        const CommandResult result = RunOnce(stopEvent, timeout);
        // A run the destructor cut short says nothing about the command.
        if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) {
            return;
        }
        Publish(result);
        if (StoppedWaiting(stopEvent, started)) {
            return;
        }
    }
}

void CommandPoller::RunContinuously(HANDLE stopEvent) {
    while (true) {
        const CommandResult ended = StreamOnce(stopEvent);
        if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) {
            return;
        }
        Publish(ended);
        if (StoppedWaiting(stopEvent, std::chrono::steady_clock::now())) {
            return;
        }
    }
}

bool CommandPoller::StoppedWaiting(HANDLE stopEvent, std::chrono::steady_clock::time_point started) const {
    const auto wait = std::max(std::chrono::duration_cast<std::chrono::milliseconds>(started + _interval - std::chrono::steady_clock::now()), std::chrono::milliseconds(0));
    return WaitForSingleObject(stopEvent, static_cast<DWORD>(wait.count())) == WAIT_OBJECT_0;
}

// Runs on the poller thread, so every failure becomes a result instead of an exception.
CommandResult CommandPoller::RunOnce(HANDLE stopEvent, std::chrono::seconds timeout) const {
    const auto output = RunOrFailure([&] { return CaptureOutput(_command, timeout, kMaxCommandOutputBytes, stopEvent); });
    if (const auto* failure = std::get_if<RunFailure>(&output)) {
        return Failure(failure->reason, failure->detail);
    }
    // Parsed here so the UI thread only ever meets text it can draw.
    return ParseOutput(std::get<std::string>(output), _output, _classes);
}

// Publishes each line as it comes and returns how the command ended, which is always a failure: a continuous command
// is expected to keep running.
CommandResult CommandPoller::StreamOnce(HANDLE stopEvent) {
    const auto exitCode = RunOrFailure(
        [this, stopEvent] { return StreamLines(_command, kMaxCommandOutputBytes, stopEvent, [this](std::string_view line) { Publish(ParseOutput(line, _output, _classes)); }); });
    if (const auto* failure = std::get_if<RunFailure>(&exitCode)) {
        return Failure(failure->reason, failure->detail);
    }
    const std::string reason = std::format("exited with code {}", std::get<std::uint32_t>(exitCode));
    return Failure(reason, reason);
}

// Posts and logs only on a change, so a command that keeps failing the same way logs once.
void CommandPoller::Publish(const CommandResult& result) {
    {
        const std::scoped_lock lock(_mutex);
        if (result == _result) {
            return;
        }
        _result = result;
    }
    if (result.outcome == CommandOutcome::Failed) {
        const std::string program = ToUtf8(_command.program.wstring());
        Log(LogLevel::Warning, "custom", "command_failed", {{.key = "module", .value = _name}, {.key = "program", .value = program}, {.key = "reason", .value = result.detail}});
    }
    _changes.Notify("custom");
}

Custom::Custom(const CustomConfig& config, const CommandPoller& poller) : TextModule(config.text, "custom", CustomPlaceholders(std::string())), _poller(poller) {}

std::optional<std::uint32_t> Custom::MillisecondsUntilChange() const {
    return std::nullopt;
}

std::optional<std::wstring> Custom::TooltipText() {
    const CommandResult result = _poller.Result();
    if (result.outcome == CommandOutcome::Failed) {
        return ToWide(result.detail);
    }
    if (result.tooltip.has_value()) {
        return ToWide(*result.tooltip);
    }
    return TextModule::TooltipText();
}

ModuleText Custom::Format(const std::string& spec) {
    const CommandResult result = _poller.Result();
    switch (result.outcome) {
        case CommandOutcome::Pending: return NormalText(Settings().icon, L"");
        case CommandOutcome::Succeeded:
            return ModuleText{
                .icon = Settings().icon,
                .text = FormatPlaceholders(spec, CustomPlaceholders(result.text)),
                .state = SucceededState(result),
                .classIndex = result.classIndex.value_or(0),
            };
        case CommandOutcome::Failed:
            return ModuleText{
                .icon = Settings().icon,
                .text = ToWide(result.text),
                .state = TextState::Alert,
                .classIndex = 0,
            };
    }
    throw std::invalid_argument(std::format("unknown command outcome {}", static_cast<std::int32_t>(result.outcome)));
}
