#include "bar/MenuLister.hpp"

#include "modules/CommandPoller.hpp"
#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Process.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <variant>

namespace {

    MenuItem ReadItem(const nlohmann::json& item) {
        if (!item.is_object()) {
            throw OutputError("printed a JSON item that is not an object");
        }
        if (item.contains("separator")) {
            if (item.size() != 1 || item.at("separator") != true) {
                throw OutputError("printed a JSON separator other than {\"separator\":true}");
            }
            return MenuSeparator{};
        }
        for (const auto& [key, field] : item.items()) {
            if (key != "id" && key != "text" && key != "active" && key != "tooltip") {
                throw OutputError("printed a JSON item field other than id, text, active and tooltip");
            }
        }
        const auto id   = item.find("id");
        const auto text = item.find("text");
        if (id == item.end() || !id->is_string() || text == item.end() || !text->is_string()) {
            throw OutputError("printed a JSON item without an id string and a text string");
        }
        const auto active = item.find("active");
        if (active != item.end() && !active->is_boolean()) {
            throw OutputError("printed a JSON item active that is not true or false");
        }
        const auto tooltip = item.find("tooltip");
        if (tooltip != item.end() && !tooltip->is_string()) {
            throw OutputError("printed a JSON item tooltip that is not a string");
        }
        MenuEntry entry{.id      = id->get<std::string>(),
                        .text    = text->get<std::string>(),
                        .active  = active != item.end() && active->get<bool>(),
                        .tooltip = tooltip == item.end() ? std::nullopt : std::optional<std::string>(tooltip->get<std::string>())};
        RequireDrawable(entry.id, "id", L"");
        RequireDrawable(entry.text, "text", L"");
        if (entry.tooltip.has_value()) {
            RequireDrawable(*entry.tooltip, "tooltip", L"\n");
        }
        if (entry.id.empty() || entry.id.front() == '-' || entry.id.front() == '/') {
            throw OutputError("printed a JSON item id that is empty or starts with - or /");
        }
        return entry;
    }

    // Runs on the lister's thread, so every failure becomes a listing instead of an exception.
    MenuListing ListEntries(const Command& command, std::chrono::seconds timeout, HANDLE stopEvent) {
        const auto output = RunOrFailure([&] { return CaptureOutput(command, timeout, kMaxCommandOutputBytes, stopEvent); });
        if (const auto* failure = std::get_if<RunFailure>(&output)) {
            return MenuFailure{.reason = failure->reason, .detail = failure->detail};
        }
        try {
            return ParseMenuOutput(std::get<std::string>(output));
        } catch (const OutputError& error) { return MenuFailure{.reason = error.what(), .detail = error.what()}; }
    }

} // namespace

std::vector<MenuItem> ParseMenuOutput(std::string_view output) {
    const nlohmann::json value = nlohmann::json::parse(output.begin(), output.end(), nullptr, false);
    if (value.is_discarded()) {
        throw OutputError("printed invalid JSON");
    }
    const auto items = value.find("items");
    if (!value.is_object() || value.size() != 1 || items == value.end()) {
        throw OutputError("printed JSON that is not an object with only an items list");
    }
    if (!items->is_array()) {
        throw OutputError("printed JSON items that is not a list");
    }
    if (items->size() > kMaxMenuEntries) {
        throw OutputError(std::format("printed more than {} items", kMaxMenuEntries));
    }
    std::vector<MenuItem> read;
    read.reserve(items->size());
    for (const nlohmann::json& item : *items) {
        read.push_back(ReadItem(item));
    }
    return read;
}

bool HasEntries(const std::vector<MenuItem>& items) {
    return std::ranges::any_of(items, [](const MenuItem& item) { return std::holds_alternative<MenuEntry>(item); });
}

MenuLister::MenuLister(HWND target, std::uint32_t message) : _target(target), _message(message), _command{}, _timeout{} {}

void MenuLister::List(const Command& command, std::chrono::seconds timeout) {
    // The old thread is joined before the new one starts, so only the new one can fill _ready, or read _command and
    // _timeout, from here on.
    _thread.reset();
    {
        const std::scoped_lock lock(_mutex);
        _ready.reset();
    }
    _command = command;
    _timeout = timeout;
    _thread  = std::make_unique<StoppableThread>("menu lister", [this](HANDLE stopEvent) { Run(stopEvent); });
}

std::optional<MenuListing> MenuLister::Take() {
    const std::scoped_lock     lock(_mutex);
    std::optional<MenuListing> ready = std::move(_ready);
    _ready.reset();
    return ready;
}

void MenuLister::Run(HANDLE stopEvent) {
    MenuListing listing = ListEntries(_command, _timeout, stopEvent);
    // A listing List or the destructor cut short says nothing about the command.
    if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) {
        return;
    }
    {
        const std::scoped_lock lock(_mutex);
        _ready = std::move(listing);
    }
    if (!PostMessageW(_target, _message, 0, 0)) {
        Log(LogLevel::Warning, "menu", "post_failed", {{.key = "error", .value = std::to_string(GetLastError())}});
    }
}
