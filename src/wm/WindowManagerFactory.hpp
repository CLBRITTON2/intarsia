#pragma once

#include "config/Config.hpp"
#include "wm/IWindowManager.hpp"

#include <windows.h>

#include <memory>

std::unique_ptr<IWindowManager> CreateWindowManager(WindowManagerKind kind, HWND target, UINT changedMessage);
