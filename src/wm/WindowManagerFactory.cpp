#include "wm/WindowManagerFactory.hpp"

#include "wm/NativeWindowManager.hpp"

#include <format>
#include <stdexcept>

std::unique_ptr<IWindowManager> CreateWindowManager(WindowManagerKind kind, HWND target, UINT changedMessage) {
    switch (kind) {
        case WindowManagerKind::Windows: return std::make_unique<NativeWindowManager>(target, changedMessage);
    }
    throw std::invalid_argument(std::format("unknown window manager kind {}", static_cast<int>(kind)));
}
