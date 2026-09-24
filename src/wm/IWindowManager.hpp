#pragma once

#include <optional>
#include <string>

struct FocusedWindow {
    std::wstring title;
};

// The bar's view of whatever manages windows: a tiling window manager over IPC, or Windows itself. Lives on the UI
// thread, and posts its change message to the target window it was created with whenever what it reports changes.
class IWindowManager {
  public:
    virtual ~IWindowManager() = default;

    virtual std::optional<FocusedWindow> Focused() const = 0;
};
