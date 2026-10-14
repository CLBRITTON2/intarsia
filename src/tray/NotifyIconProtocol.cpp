#include "tray/NotifyIconProtocol.hpp"

#include "platform/Text.hpp"

#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace {

    static_assert(sizeof(TrayNotifyData) == 964, "TrayNotifyData must match shell32's layout");
    static_assert(sizeof(NotifyIconIdentifier32) == 40, "NotifyIconIdentifier32 must match shell32's layout");

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
    TrayIcon Updated(TrayIcon icon, const NotifyIconMessage& message, std::wstring_view ownerImage, std::shared_ptr<const IconPixels> pixels) {
        icon.owner = message.owner;
        icon.id    = message.id;
        if ((message.flags & NIF_GUID) != 0) {
            icon.guid       = message.guid;
            icon.ownerImage = ownerImage;
        }
        if ((message.flags & NIF_MESSAGE) != 0) {
            icon.callbackMessage = message.callbackMessage;
        }
        if ((message.flags & NIF_TIP) != 0) {
            icon.tooltip = DrawableTooltip(message.tip);
        }
        // An app that swaps only its icon may leave NIF_SHOWTIP out without meaning to drop the tooltip.
        if (message.command == NIM_ADD || (message.flags & NIF_TIP) != 0) {
            icon.standardTooltip = (message.flags & NIF_SHOWTIP) != 0;
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

    WORD Signed16(std::int32_t value) {
        return static_cast<WORD>(static_cast<std::int16_t>(value));
    }

    WPARAM CursorWParam(POINT cursor) {
        return MAKEWPARAM(Signed16(cursor.x), Signed16(cursor.y));
    }

    // 0 inside the rect.
    std::int64_t SquaredDistanceToRect(const RECT& rect, POINT point) {
        const std::int64_t gapX = std::max({static_cast<std::int64_t>(rect.left) - point.x, std::int64_t{0}, static_cast<std::int64_t>(point.x) - rect.right});
        const std::int64_t gapY = std::max({static_cast<std::int64_t>(rect.top) - point.y, std::int64_t{0}, static_cast<std::int64_t>(point.y) - rect.bottom});
        return (gapX * gapX) + (gapY * gapY);
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
    return NotifyIconMessage{
        .command         = raw.command,
        .owner           = WidenHandle(raw.data.window),
        .id              = raw.data.id,
        .flags           = raw.data.flags,
        .callbackMessage = raw.data.callbackMessage,
        .icon            = WidenHandle(raw.data.icon),
        .tip             = UpToNull(raw.data.tip),
        .state           = raw.data.state,
        .stateMask       = raw.data.stateMask,
        .version         = raw.data.version,
        .guid            = raw.data.guid,
    };
}

IconRectQuery ParseIconRectQuery(std::span<const std::byte> payload) {
    if (payload.size() < sizeof(NotifyIconIdentifier32)) {
        throw TrayMessageError(std::format("an icon rect query is at least {} bytes, got {}", sizeof(NotifyIconIdentifier32), payload.size()));
    }
    NotifyIconIdentifier32 raw{};
    std::memcpy(&raw, payload.data(), sizeof(raw));
    if (raw.signature != kTrayNotifySignature) {
        throw TrayMessageError(std::format("an icon rect query starts with 0x{:08X}, got 0x{:08X}", kTrayNotifySignature, raw.signature));
    }
    // Checked before the cast, which would keep only the low byte.
    if (raw.part != static_cast<std::uint32_t>(IconRectPart::Position) && raw.part != static_cast<std::uint32_t>(IconRectPart::Size)) {
        throw TrayMessageError(std::format("an icon rect query asks for part 1 or 2, got {}", raw.part));
    }
    return IconRectQuery{
        .part  = static_cast<IconRectPart>(raw.part),
        .owner = WidenHandle(raw.window),
        .id    = raw.id,
        .guid  = raw.guid,
    };
}

std::optional<RECT> QueriedIconRect(const IconRectQuery& query, const std::vector<TrayIcon>& icons, const std::vector<TrayIconRect>& rects, POINT cursor) {
    const bool byGuid = query.guid != GUID{};
    const auto icon   = std::ranges::find_if(
        icons, [&query, byGuid](const TrayIcon& candidate) { return byGuid ? candidate.guid == query.guid : candidate.owner == query.owner && candidate.id == query.id; });
    if (icon == icons.end()) {
        return std::nullopt;
    }
    std::optional<RECT> nearest;
    for (const TrayIconRect& shown : rects) {
        if (shown.owner == icon->owner && shown.id == icon->id && (!nearest.has_value() || SquaredDistanceToRect(shown.rect, cursor) < SquaredDistanceToRect(*nearest, cursor))) {
            nearest = shown.rect;
        }
    }
    return nearest;
}

LRESULT IconRectAnswer(IconRectPart part, const RECT& rect) {
    switch (part) {
        case IconRectPart::Position: return MAKELONG(Signed16(rect.left), Signed16(rect.top));
        case IconRectPart::Size: return MAKELONG(Signed16(rect.right - rect.left), Signed16(rect.bottom - rect.top));
    }
    throw std::invalid_argument(std::format("unknown icon rect part {}", static_cast<std::uint32_t>(part)));
}

std::optional<std::vector<TrayIcon>> ApplyNotifyIcon(const std::vector<TrayIcon>& icons, const NotifyIconMessage& message, std::wstring_view ownerImage,
                                                     std::shared_ptr<const IconPixels> pixels) {
    std::vector<TrayIcon> updated = icons;
    const auto            icon    = FindIcon(updated, message);
    const bool            found   = icon != updated.end();
    // Any process can name any GUID, so without this one could take another app's icon and the clicks on it.
    if (found && (message.flags & NIF_GUID) != 0 && !SameFileName(icon->ownerImage, ownerImage)) {
        return std::nullopt;
    }
    if (!found && message.command != NIM_ADD) {
        return std::nullopt;
    }
    switch (message.command) {
        case NIM_ADD:
            if (found) {
                *icon = Updated(*icon, message, ownerImage, std::move(pixels));
                return updated;
            }
            if (updated.size() >= kMaxTrayIcons) {
                return std::nullopt;
            }
            updated.push_back(Updated(
                TrayIcon{
                    .owner           = message.owner,
                    .id              = message.id,
                    .guid            = std::nullopt,
                    .ownerImage      = {},
                    .callbackMessage = 0,
                    .version         = 0,
                    .tooltip         = {},
                    .standardTooltip = false,
                    .pixels          = nullptr,
                    .hidden          = false,
                },
                message, ownerImage, std::move(pixels)));
            return updated;
        case NIM_MODIFY: *icon = Updated(*icon, message, ownerImage, std::move(pixels)); return updated;
        case NIM_DELETE: updated.erase(icon); return updated;
        case NIM_SETVERSION: icon->version = message.version; return updated;
        case NIM_SETFOCUS: return updated;
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

std::vector<std::uint32_t> ClickEvents(TrayButton button, std::uint32_t version) {
    const bool notifies = version >= NOTIFYICON_VERSION;
    switch (button) {
        case TrayButton::Left: return notifies ? std::vector<std::uint32_t>{WM_LBUTTONDOWN, WM_LBUTTONUP, NIN_SELECT} : std::vector<std::uint32_t>{WM_LBUTTONDOWN, WM_LBUTTONUP};
        case TrayButton::Right:
            return notifies ? std::vector<std::uint32_t>{WM_RBUTTONDOWN, WM_RBUTTONUP, WM_CONTEXTMENU} : std::vector<std::uint32_t>{WM_RBUTTONDOWN, WM_RBUTTONUP};
        case TrayButton::Middle: return {WM_MBUTTONDOWN, WM_MBUTTONUP};
    }
    throw std::invalid_argument(std::format("unknown tray button {}", static_cast<std::int32_t>(button)));
}

std::vector<std::uint32_t> DoubleClickEvents(TrayButton button) {
    switch (button) {
        case TrayButton::Left: return {WM_LBUTTONDBLCLK, WM_LBUTTONUP};
        case TrayButton::Right: return {WM_RBUTTONDBLCLK, WM_RBUTTONUP};
        case TrayButton::Middle: return {WM_MBUTTONDBLCLK, WM_MBUTTONUP};
    }
    throw std::invalid_argument(std::format("unknown tray button {}", static_cast<std::int32_t>(button)));
}

std::vector<TrayInputMessage> TrayInputMessages(const TrayIcon& icon, const std::vector<std::uint32_t>& events, POINT cursor) {
    if (icon.callbackMessage < WM_USER) {
        return {};
    }
    std::vector<TrayInputMessage> messages;
    messages.reserve(events.size());
    for (const std::uint32_t event : events) {
        // Version 4 carries the cursor in wParam and the event and id in lParam, earlier versions the id and the event.
        if (icon.version >= NOTIFYICON_VERSION_4) {
            messages.push_back(TrayInputMessage{
                .message = icon.callbackMessage,
                .wParam  = CursorWParam(cursor),
                .lParam  = MAKELPARAM(static_cast<WORD>(event), static_cast<WORD>(icon.id)),
            });
        } else {
            messages.push_back(TrayInputMessage{
                .message = icon.callbackMessage,
                .wParam  = icon.id,
                .lParam  = static_cast<LPARAM>(event),
            });
        }
    }
    return messages;
}

bool IsDoubleClick(const std::optional<TrayClick>& previous, const TrayClick& click, std::chrono::milliseconds doubleTime, SIZE area) {
    if (!previous.has_value() || previous->owner != click.owner || previous->id != click.id || previous->button != click.button) {
        return false;
    }
    const auto elapsed = click.time - previous->time;
    return elapsed >= std::chrono::steady_clock::duration::zero() && elapsed <= doubleTime && std::abs(click.cursor.x - previous->cursor.x) <= area.cx / 2 &&
        std::abs(click.cursor.y - previous->cursor.y) <= area.cy / 2;
}
