#pragma once

#include "config/Config.hpp"
#include "platform/ChangeSignal.hpp"
#include "wm/IWindowManager.hpp"

#include <cstdint>
#include <memory>

// The config's window manager backend. glazeWmPort is where the GlazeWM backend connects on 127.0.0.1, unused by the
// others.
std::unique_ptr<IWindowManager> CreateWindowManager(const Config& config, ChangeSignal& changes, std::uint16_t glazeWmPort);
