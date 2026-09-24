#include "wm/WindowManagerFactory.hpp"

#include "wm/GlazeWm.hpp"
#include "wm/Native.hpp"

#include <format>
#include <stdexcept>

std::unique_ptr<IWindowManager> CreateWindowManager(WindowManagerKind kind, HWND target, UINT changedMessage) {
    switch (kind) {
        case WindowManagerKind::Windows: return std::make_unique<Native>(target, changedMessage);
        case WindowManagerKind::GlazeWm: return std::make_unique<GlazeWm>(target, changedMessage);
    }
    throw std::invalid_argument(std::format("unknown window manager kind {}", static_cast<int>(kind)));
}
