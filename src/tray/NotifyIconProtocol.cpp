#include "tray/NotifyIconProtocol.hpp"

#include "platform/Text.hpp"

#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <iterator>
#include <utility>

namespace {

    static_assert(sizeof(TrayNotifyData) == 964, "TrayNotifyData must match shell32's layout");

    // Windows keeps handles to 32 significant bits and sign-extends them on 64-bit.
    std::intptr_t WidenHandle(std::uint32_t handle) {
        return static_cast<std::intptr_t>(static_cast<std::int32_t>(handle));
    }

    std::wstring UpToNull(const std::array<wchar_t, 128>& text) {
        return {text.begin(), std::ranges::find(text, L'\0')};
    }

    // An icon with a GUID is found by it alone, one without by its owner and id.
    auto FindIcon(std::vector<TrayIcon>& icons, const NotifyIconMessage& message) {
        if ((message.flags & NIF_GUID) != 0) {
            return std::ranges::find_if(icons, [&message](const TrayIcon& icon) { return icon.guid == message.guid; });
        }
        return std::ranges::find_if(icons, [&message](const TrayIcon& icon) { return icon.owner == message.owner && icon.id == message.id; });
    }

    // The fields the message's flags say it sets. The same pixels as before keep their pointer, so an app that sets its
    // icon again unchanged costs no upload or redraw.
    TrayIcon Updated(TrayIcon icon, const NotifyIconMessage& message, std::shared_ptr<const IconPixels> pixels) {
        icon.owner = message.owner;
        icon.id    = message.id;
        if ((message.flags & NIF_GUID) != 0) {
            icon.guid = message.guid;
        }
        if ((message.flags & NIF_MESSAGE) != 0) {
            icon.callbackMessage = message.callbackMessage;
        }
        if ((message.flags & NIF_TIP) != 0) {
            icon.tooltip = DrawableTooltip(message.tip);
        }
        if ((message.flags & NIF_ICON) != 0) {
            const bool same = icon.pixels != nullptr && pixels != nullptr && *icon.pixels == *pixels;
            icon.pixels     = same ? icon.pixels : std::move(pixels);
        }
        if ((message.flags & NIF_STATE) != 0 && (message.stateMask & NIS_HIDDEN) != 0) {
            icon.hidden = (message.state & NIS_HIDDEN) != 0;
        }
        return icon;
    }

    WPARAM CursorWParam(POINT cursor) {
        return MAKEWPARAM(static_cast<WORD>(static_cast<std::int16_t>(cursor.x)), static_cast<WORD>(static_cast<std::int16_t>(cursor.y)));
    }

} // namespace

NotifyIconMessage ParseNotifyIconMessage(std::span<const std::byte> payload) {
    if (payload.size() < sizeof(TrayNotifyData)) {
        throw TrayMessageError(std::format("a notify icon message is at least {} bytes, got {}", sizeof(TrayNotifyData), payload.size()));
    }
    TrayNotifyData raw{};
    std::memcpy(&raw, payload.data(), sizeof(raw));
    if (raw.signature != kTrayNotifySignature) {
        throw TrayMessageError(std::format("a notify icon message starts with 0x{:08X}, got 0x{:08X}", kTrayNotifySignature, raw.signature));
    }
    return NotifyIconMessage{.command         = raw.command,
                             .owner           = WidenHandle(raw.data.window),
                             .id              = raw.data.id,
                             .flags           = raw.data.flags,
                             .callbackMessage = raw.data.callbackMessage,
                             .icon            = WidenHandle(raw.data.icon),
                             .tip             = UpToNull(raw.data.tip),
                             .state           = raw.data.state,
                             .stateMask       = raw.data.stateMask,
                             .version         = raw.data.version,
                             .guid            = raw.data.guid};
}

std::optional<std::vector<TrayIcon>> ApplyNotifyIcon(const std::vector<TrayIcon>& icons, const NotifyIconMessage& message, std::shared_ptr<const IconPixels> pixels) {
    std::vector<TrayIcon> updated = icons;
    const auto            icon    = FindIcon(updated, message);
    const bool            found   = icon != updated.end();
    switch (message.command) {
        case NIM_ADD:
            if (found) {
                *icon = Updated(*icon, message, std::move(pixels));
                return updated;
            }
            if (updated.size() >= kMaxTrayIcons) {
                return std::nullopt;
            }
            updated.push_back(Updated(
                TrayIcon{.owner = message.owner, .id = message.id, .guid = std::nullopt, .callbackMessage = 0, .version = 0, .tooltip = {}, .pixels = nullptr, .hidden = false},
                message, std::move(pixels)));
            return updated;
        case NIM_MODIFY:
            if (!found) {
                return std::nullopt;
            }
            *icon = Updated(*icon, message, std::move(pixels));
            return updated;
        case NIM_DELETE:
            if (!found) {
                return std::nullopt;
            }
            updated.erase(icon);
            return updated;
        case NIM_SETVERSION:
            if (!found) {
                return std::nullopt;
            }
            icon->version = message.version;
            return updated;
        case NIM_SETFOCUS:
            if (!found) {
                return std::nullopt;
            }
            return updated;
        default: return std::nullopt;
    }
}

std::wstring DrawableTooltip(std::wstring_view tip) {
    std::wstring kept;
    kept.reserve(tip.size());
    std::ranges::copy_if(tip, std::back_inserter(kept), [](wchar_t unit) { return unit != L'\r'; });
    std::ranges::replace_if(kept, [](wchar_t unit) { return unit != L'\n' && IsUnsafeCharacter(unit); }, L'�');
    return ReplaceLoneSurrogates(kept);
}

// Explorer's order: the button's down and up, then from version 3 on the select or context menu notification.
std::vector<TrayInputMessage> TrayInputMessages(const TrayIcon& icon, TrayButton button, POINT cursor) {
    if (icon.callbackMessage < WM_USER) {
        return {};
    }
    std::vector<std::uint32_t> events;
    switch (button) {
        case TrayButton::Left: events = {WM_LBUTTONDOWN, WM_LBUTTONUP}; break;
        case TrayButton::Right: events = {WM_RBUTTONDOWN, WM_RBUTTONUP}; break;
    }
    if (icon.version >= NOTIFYICON_VERSION) {
        events.push_back(button == TrayButton::Left ? NIN_SELECT : WM_CONTEXTMENU);
    }
    std::vector<TrayInputMessage> messages;
    messages.reserve(events.size());
    for (const std::uint32_t event : events) {
        // Version 4 carries the cursor in wParam and the event and id in lParam, earlier versions the id and the event.
        if (icon.version >= NOTIFYICON_VERSION_4) {
            messages.push_back(
                TrayInputMessage{.message = icon.callbackMessage, .wParam = CursorWParam(cursor), .lParam = MAKELPARAM(static_cast<WORD>(event), static_cast<WORD>(icon.id))});
        } else {
            messages.push_back(TrayInputMessage{.message = icon.callbackMessage, .wParam = icon.id, .lParam = static_cast<LPARAM>(event)});
        }
    }
    return messages;
}
