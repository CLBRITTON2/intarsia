#pragma once

#include "tray/NotifyIconProtocol.hpp"

#include <cstdint>

// A handle as the 32-bit field the tray payload carries, which Windows sign-extends back.
inline std::uint32_t Handle32(const void* handle) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(handle));
}

// A notify icon message for owner's icon id with every other field empty, as an app's Shell_NotifyIconW sends it.
inline TrayNotifyData NotifyData(std::uint32_t command, std::uint32_t owner, std::uint32_t id, std::uint32_t flags, std::uint32_t callbackMessage) {
    TrayNotifyData data{};
    data.signature            = kTrayNotifySignature;
    data.command              = command;
    data.data.size            = sizeof(NotifyIconData32);
    data.data.window          = owner;
    data.data.id              = id;
    data.data.flags           = flags;
    data.data.callbackMessage = callbackMessage;
    return data;
}
