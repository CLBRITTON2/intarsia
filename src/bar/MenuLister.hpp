#pragma once

#include "platform/Process.hpp"
#include "platform/StoppableThread.hpp"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// One entry a menu command printed.
struct MenuEntry {
    std::string                id; // the argument on_select gets when the entry is picked
    std::string                text;
    bool                       active;  // drawn in the menu item active block colors, such as the profile already connected
    std::optional<std::string> tooltip; // shown beside the menu while the cursor rests on the entry

    bool                       operator==(const MenuEntry&) const = default;
};

// A blank line between entries, as tall as one, that cannot be picked.
struct MenuSeparator {
    bool operator==(const MenuSeparator&) const = default;
};

using MenuItem = std::variant<MenuEntry, MenuSeparator>;

// A menu command that gave no entries to show: it failed, or printed output ParseMenuOutput refuses.
struct MenuFailure {
    std::string reason; // short enough to show in the menu
    std::string detail; // the full failure, for the log

    bool        operator==(const MenuFailure&) const = default;
};

using MenuListing = std::variant<std::vector<MenuItem>, MenuFailure>;

// The most items, separators included, one menu shows. Output listing more fails instead of growing a menu past any
// screen.
inline constexpr size_t kMaxMenuEntries = 50;

// What a menu command printed: one JSON object, {"items":[{"id":"work","text":"Work","active":true},
// {"separator":true}, ...]}, active and tooltip optional. Output is untrusted: id, text and tooltip must be drawable
// (RequireDrawable), a tooltip may also hold line breaks, and an id is never empty and never starts with '-' or '/', so
// the program on_select starts cannot read it as an option. Throws OutputError otherwise.
std::vector<MenuItem> ParseMenuOutput(std::string_view output);

// Whether items holds an entry to pick, rather than nothing or only separators.
bool HasEntries(const std::vector<MenuItem>& items);

// Runs one menu command at a time on its own thread, and posts message to target once its listing is ready.
class MenuLister {
  public:
    MenuLister(HWND target, std::uint32_t message);
    MenuLister(const MenuLister&)            = delete;
    MenuLister& operator=(const MenuLister&) = delete;

    // Kills the listing in progress, if any, and starts command.
    void List(const Command& command, std::chrono::seconds timeout);

    // The finished listing, or nullopt when a later List call replaced it before it was taken.
    std::optional<MenuListing> Take();

  private:
    void                       Run(HANDLE stopEvent);

    HWND                       _target;
    std::uint32_t              _message;
    std::mutex                 _mutex;
    std::optional<MenuListing> _ready;
    // Captured by value, the command would make the thread body's move constructor throwing.
    Command              _command;
    std::chrono::seconds _timeout;
    // Declared last so every member above exists while a listing runs. Destruction kills a listing in progress.
    std::unique_ptr<StoppableThread> _thread;
};
