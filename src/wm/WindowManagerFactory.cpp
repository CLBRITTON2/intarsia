#include "wm/WindowManagerFactory.hpp"

#include "wm/GlazeWm.hpp"
#include "wm/Native.hpp"

#include <cstdint>
#include <format>
#include <stdexcept>

std::unique_ptr<IWindowManager> CreateWindowManager(const Config& config, ChangeSignal& changes, std::uint16_t glazeWmPort) {
    switch (config.windowManager) {
        case WindowManagerKind::Windows: return std::make_unique<Native>(changes);
        case WindowManagerKind::GlazeWm:
            return std::make_unique<GlazeWm>(changes, glazeWmPort, ShowsKind(config, ModuleKind::Window) ? std::make_unique<Native>(changes) : nullptr);
    }
    throw std::invalid_argument(std::format("unknown window manager kind {}", static_cast<std::int32_t>(config.windowManager)));
}
