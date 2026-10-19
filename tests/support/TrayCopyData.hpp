#pragma once

#include "platform/Errors.hpp"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <optional>

inline constexpr std::uint32_t kTrayAppCallback = WM_APP + 9;
inline constexpr std::uint32_t kTrayAnswerMs = 5000;

// One WM_COPYDATA, as shell32 sends the taskbar. nullopt on timeout, Win32Error on any other failure.
inline std::optional<LRESULT> SendTrayCopyData(HWND tray, HWND sender, ULONG_PTR kind, void* payload, size_t size) {
    COPYDATASTRUCT data{
        .dwData = kind,
        .cbData = static_cast<DWORD>(size),
        .lpData = payload,
    };
    DWORD_PTR result = 0;
    if (SendMessageTimeoutW(tray, WM_COPYDATA, reinterpret_cast<WPARAM>(sender), reinterpret_cast<LPARAM>(&data), SMTO_NORMAL, kTrayAnswerMs, &result) == 0) {
        const DWORD error = GetLastError();
        if (error == ERROR_TIMEOUT) {
            return std::nullopt;
        }
        throw Win32Error("SendMessageTimeoutW(WM_COPYDATA to the tray)", HRESULT_FROM_WIN32(error));
    }
    return static_cast<LRESULT>(result);
}
