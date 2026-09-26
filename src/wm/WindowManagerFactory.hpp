#pragma once

#include "config/Config.hpp"
#include "platform/ChangeSignal.hpp"
#include "wm/IWindowManager.hpp"

#include <windows.h>

#include <memory>

// glazeWmPort is where the GlazeWM backend connects on 127.0.0.1, unused by the others.
std::unique_ptr<IWindowManager> CreateWindowManager(WindowManagerKind kind, ChangeSignal& changes, USHORT glazeWmPort);
