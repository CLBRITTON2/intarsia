#pragma once

#include "tray/ShellNotifyIcon.hpp"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <vector>

// A handle as the 32-bit field the tray payload carries, which Windows sign-extends back.
inline std::uint32_t Handle32(const void* handle) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(handle));
}

inline TrayNotifyData NotifyData(std::uint32_t command, std::uint32_t owner, std::uint32_t id, std::uint32_t flags, std::uint32_t callbackMessage) {
    TrayNotifyData data{};
    data.signature = kTrayNotifySignature;
    data.command = command;
    data.data.size = sizeof(NotifyIconData32);
    data.data.window = owner;
    data.data.id = id;
    data.data.flags = flags;
    data.data.callbackMessage = callbackMessage;
    return data;
}

// The caller destroys it with DestroyIcon.
inline HICON CreateSolidIcon(std::uint32_t rgb) {
    constexpr std::uint32_t kSide = 16;
    const std::vector<std::uint32_t> pixels(size_t{kSide} * kSide, rgb | 0xFF000000U);
    const std::array<std::byte, 32> mask{};
    HBITMAP colorBitmap = CreateBitmap(kSide, kSide, 1, 32, pixels.data());
    HBITMAP maskBitmap = CreateBitmap(kSide, kSide, 1, 1, mask.data());
    if (colorBitmap == nullptr || maskBitmap == nullptr) {
        const DWORD error = GetLastError();
        DeleteObject(colorBitmap);
        DeleteObject(maskBitmap);
        throw std::runtime_error(std::format("CreateBitmap failed with {}", error));
    }
    ICONINFO info{
        .fIcon = TRUE,
        .xHotspot = 0,
        .yHotspot = 0,
        .hbmMask = maskBitmap,
        .hbmColor = colorBitmap,
    };
    HICON icon = CreateIconIndirect(&info);
    const DWORD error = GetLastError();
    DeleteObject(colorBitmap);
    DeleteObject(maskBitmap);
    if (icon == nullptr) {
        throw std::runtime_error(std::format("CreateIconIndirect failed with {}", error));
    }
    return icon;
}
