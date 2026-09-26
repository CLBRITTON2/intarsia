#include "platform/ChangeSignal.hpp"

#include "platform/Log.hpp"

#include <string>

ChangeSignal::ChangeSignal(HWND target, UINT message) : _target(target), _message(message) {}

void ChangeSignal::Notify(std::string_view component) {
    if (_pending.exchange(true)) {
        return;
    }
    if (!PostMessageW(_target, _message, 0, 0)) {
        const DWORD error = GetLastError();
        _pending          = false;
        Log(LogLevel::Warning, component, "post_failed", {{.key = "error", .value = std::to_string(error)}});
    }
}

void ChangeSignal::Acknowledge() {
    _pending = false;
}
