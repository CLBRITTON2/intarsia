#include "modules/CommandPoller.hpp"

#include "bar/Sections.hpp"
#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Process.hpp"
#include "platform/Text.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <algorithm>
#include <format>
#include <stdexcept>
#include <utility>
#include <variant>

namespace {

    // Far more than one line on a bar needs. Output past it fails the run instead of growing without bound.
    constexpr size_t kMaxOutputBytes = size_t{64} * 1024;

    // Output that fails its run. The message never quotes the output, so it is always safe to draw and log.
    class OutputError : public std::runtime_error {
      public:
        using std::runtime_error::runtime_error;
    };

    // Throws OutputError naming field when text is not UTF-8 or holds an unsafe character other than those in allowed.
    void RequireDrawable(std::string_view text, std::string_view field, std::wstring_view allowed) {
        std::wstring wide;
        try {
            wide = ToWide(text);
        } catch (const Win32Error&) { throw OutputError(std::format("{} is not UTF-8", field)); }
        const auto unsafe = std::ranges::find_if(wide, [allowed](wchar_t unit) { return IsUnsafeCharacter(unit) && allowed.find(unit) == std::wstring_view::npos; });
        if (unsafe != wide.end()) {
            throw OutputError(std::format("{} has control character U+{:04X}", field, static_cast<std::uint32_t>(*unsafe)));
        }
    }

    CommandResult ParseText(std::string_view output) {
        const std::string_view line = FirstLine(output);
        RequireDrawable(line, "output", L"");
        return CommandResult{.outcome = CommandOutcome::Succeeded, .text = std::string(line), .tooltip = std::nullopt, .alert = false, .classIndex = std::nullopt, .detail = {}};
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
        const auto tooltip = value.find("tooltip");
        if (tooltip != value.end() && !tooltip->is_string()) {
            throw OutputError("printed a JSON tooltip that is not a string");
        }
        const auto alert = value.find("alert");
        if (alert != value.end() && !alert->is_boolean()) {
            throw OutputError("printed a JSON alert that is not true or false");
        }

        const bool    alerted = alert != value.end() && alert->get<bool>();
        CommandResult result{.outcome    = CommandOutcome::Succeeded,
                             .text       = text->get<std::string>(),
                             .tooltip    = tooltip == value.end() ? std::nullopt : std::optional<std::string>(tooltip->get<std::string>()),
                             .alert      = alerted,
                             .classIndex = ReadClass(value, alerted, classes),
                             .detail     = {}};
        RequireDrawable(result.text, "text", L"");
        if (result.tooltip.has_value()) {
            RequireDrawable(*result.tooltip, "tooltip", L"\n");
        }
        return result;
    }

} // namespace

std::string_view FirstLine(std::string_view text) {
    std::string_view line = text.substr(0, text.find('\n'));
    if (line.ends_with('\r')) {
        line.remove_suffix(1);
    }
    return line;
}

CommandResult ParseOutput(std::string_view output, CustomOutput format, const std::vector<std::string>& classes) {
    try {
        switch (format) {
            case CustomOutput::Text: return ParseText(output);
            case CustomOutput::Json: return ParseJson(output, classes);
        }
    } catch (const OutputError& error) {
        return CommandResult{.outcome = CommandOutcome::Failed, .text = error.what(), .tooltip = std::nullopt, .alert = false, .classIndex = std::nullopt, .detail = error.what()};
    }
    throw std::invalid_argument(std::format("unknown custom output {}", static_cast<std::int32_t>(format)));
}

CommandPoller::CommandPoller(std::string name, const CustomConfig& config, ChangeSignal& changes) :
    _name(std::move(name)), _command(config.command), _interval(config.text.interval), _timeout(config.timeout), _output(config.output), _classes(config.classes),
    _changes(changes), _stopEvent(OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(command poller stop)")),
    _result{.outcome = CommandOutcome::Pending, .text = {}, .tooltip = std::nullopt, .alert = false, .classIndex = std::nullopt, .detail = {}}, _thread([this] { Run(); }) {}

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
    } catch (const CommandError& error) {
        return CommandResult{.outcome = CommandOutcome::Failed, .text = error.what(), .tooltip = std::nullopt, .alert = false, .classIndex = std::nullopt, .detail = error.what()};
    } catch (const Win32Error& error) {
        return CommandResult{
            .outcome = CommandOutcome::Failed, .text = "could not run", .tooltip = std::nullopt, .alert = false, .classIndex = std::nullopt, .detail = error.what()};
    }
    // Parsed here so the UI thread only ever meets text it can draw.
    return ParseOutput(output, _output, _classes);
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
    _changes.Notify("custom");
}

CommandPollers::CommandPollers(const Config& config, ChangeSignal& changes) {
    ForEachModuleInConfig(config, [&](const ModuleInstance& instance) {
        if (instance.kind == ModuleKind::Custom && !_pollers.contains(instance.name)) {
            _pollers.emplace(instance.name, std::make_unique<CommandPoller>(instance.name, std::get<CustomConfig>(instance.settings), changes));
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
