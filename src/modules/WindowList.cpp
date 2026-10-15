#include "modules/WindowList.hpp"

#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Threads.hpp"
#include "platform/Win32Window.hpp"
#include "render/Icon.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>

namespace {

    // Long enough for a busy window to answer, short enough that a hung one barely delays the rest of the queue.
    constexpr std::uint32_t kIconTimeoutMs = 100;
    // Windows appear a few at a time, so a full queue means a burst: the rest are asked for again on the next layout.
    constexpr size_t kMaxQueuedReads = 64;

    // The reader thread's COM apartment and WIC factory, made and ended on that thread.
    class ReaderApartment {
      public:
        ReaderApartment() : _com(ComThreading::Multi, "window icons"), _imaging(CreateImagingFactory()) {}

        IWICImagingFactory* Imaging() const {
            return _imaging.Get();
        }

      private:
        ComScope                                   _com;
        Microsoft::WRL::ComPtr<IWICImagingFactory> _imaging; // released before _com leaves COM
    };

    // Null when the thread could not set up WIC, which is logged: the thread then reads no icons.
    std::shared_ptr<const ReaderApartment> EnterReaderApartment() {
        try {
            return std::make_shared<const ReaderApartment>();
        } catch (const Win32Error& error) {
            Log(LogLevel::Error, "window_icons", "imaging_unavailable", {{.key = "reason", .value = error.what()}});
            return nullptr;
        }
    }

    IWICImagingFactory* ImagingOf(const std::shared_ptr<const ReaderApartment>& apartment) {
        if (apartment == nullptr) {
            return nullptr;
        }
        return apartment->Imaging();
    }

} // namespace

HICON WindowIcon(HWND window, std::uint32_t timeoutMs) {
    for (const WPARAM size : {static_cast<WPARAM>(ICON_BIG), static_cast<WPARAM>(ICON_SMALL2), static_cast<WPARAM>(ICON_SMALL)}) {
        DWORD_PTR icon = 0;
        if (SendMessageTimeoutW(window, WM_GETICON, size, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, timeoutMs, &icon) != 0 && icon != 0) {
            return ToIcon(static_cast<LONG_PTR>(icon));
        }
    }
    for (const std::int32_t index : {GCLP_HICON, GCLP_HICONSM}) {
        const ULONG_PTR icon = GetClassLongPtrW(window, index);
        if (icon != 0) {
            return ToIcon(static_cast<LONG_PTR>(icon));
        }
    }
    return nullptr;
}

WindowIcons::WindowIcons(ChangeSignal& changes, std::chrono::milliseconds retryAfter) :
    _changes(changes), _retryAfter(retryAfter), _reader(kMaxQueuedReads, [this] {
        const std::shared_ptr<const ReaderApartment> apartment = EnterReaderApartment();
        return WorkQueue<std::intptr_t>::Process([this, apartment](const std::intptr_t& window) { Read(ImagingOf(apartment), window); });
    }) {}

std::shared_ptr<const IconPixels> WindowIcons::For(std::intptr_t window) {
    {
        const std::scoped_lock lock(_mutex);
        const auto             icon = _icons.find(window);
        if (icon == _icons.end()) {
            _icons.emplace(window, Entry{.pixels = nullptr, .retryAt = std::nullopt});
        } else if (!icon->second.retryAt.has_value() || std::chrono::steady_clock::now() < *icon->second.retryAt) {
            return icon->second.pixels;
        } else {
            icon->second.retryAt.reset();
        }
    }
    if (!_reader.Push(window)) {
        const std::scoped_lock lock(_mutex);
        _icons.erase(window);
    }
    return nullptr;
}

void WindowIcons::ForgetClosed() {
    const std::scoped_lock lock(_mutex);
    std::erase_if(_icons, [](const auto& icon) { return IsWindow(ToWindow(icon.first)) == FALSE; });
}

// Runs on the reader thread, so failures are logged: the window then shows no icon until a retry finds one.
void WindowIcons::Read(IWICImagingFactory* imaging, std::intptr_t window) {
    std::shared_ptr<const IconPixels> pixels;
    try {
        if (imaging != nullptr) {
            HICON icon = WindowIcon(ToWindow(window), kIconTimeoutMs);
            if (icon != nullptr) {
                pixels = std::make_shared<const IconPixels>(IconToPixels(*imaging, icon));
            }
        }
    } catch (const Win32Error& error) {
        Log(LogLevel::Warning, "window_icons", "read_failed", {{.key = "window", .value = std::to_string(window)}, {.key = "reason", .value = error.what()}});
    }
    {
        const std::scoped_lock lock(_mutex);
        const auto             icon = _icons.find(window);
        // ForgetClosed dropped it while it was being read: the window is gone.
        if (icon == _icons.end()) {
            return;
        }
        if (pixels == nullptr) {
            icon->second.retryAt = std::chrono::steady_clock::now() + _retryAfter;
        } else {
            icon->second.retryAt.reset();
        }
        icon->second.pixels = std::move(pixels);
    }
    _changes.Notify("window_icons");
}

WindowList::WindowList(IWindowManager& windowManager, WindowIcons& icons, HMONITOR monitor, ModuleStyle style) :
    _windowManager(windowManager), _icons(icons), _monitor(reinterpret_cast<std::intptr_t>(monitor)), _style(std::move(style)) {}

bool WindowList::Layout(const RenderContext& render) {
    if (!LayOutItems(render)) {
        return false;
    }
    _fades.Follow(ItemStyles(), _hovered, std::chrono::steady_clock::now());
    return true;
}

bool WindowList::LayOutItems(const RenderContext& render) {
    const std::vector<ManagedWindow>               shown = OnMonitor(_windowManager.Windows(), _monitor);
    std::vector<std::shared_ptr<const IconPixels>> pixels;
    pixels.reserve(shown.size());
    for (const ManagedWindow& window : shown) {
        pixels.push_back(_icons.For(window.handle));
    }
    const auto same = [&](size_t index) { return _slots[index].window == shown[index] && _slots[index].pixels == pixels[index]; };
    if (_laidOut && shown.size() == _slots.size() && std::ranges::all_of(std::views::iota(size_t{0}, shown.size()), same)) {
        return false;
    }

    std::vector<Slot> slots;
    slots.reserve(shown.size());
    for (size_t index = 0; index < shown.size(); ++index) {
        auto bitmap = BitmapFor(render, shown[index], pixels[index]);
        slots.push_back(Slot{
            .window = shown[index],
            .pixels = pixels[index],
            .bitmap = std::move(bitmap),
        });
    }
    _slots   = std::move(slots);
    _laidOut = true;

    const float icon = _style.icon.sizeDips;
    _row             = ArrangeItems(std::vector<D2D1_SIZE_F>(_slots.size(), D2D1_SIZE_F{.width = icon, .height = icon}), ItemStyles(), render.axis);
    return true;
}

D2D1_SIZE_F WindowList::Size() const {
    return _row.size;
}

void WindowList::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    const float icon = _style.icon.sizeDips;
    for (size_t index = 0; index < _slots.size(); ++index) {
        const Slot&      slot  = _slots[index];
        const ItemStyle& style = StyleOf(slot);
        DrawItemBackground(render, _row.boxes[index], style, _fades.Background(index, render.frameTime), _fades.Shadows(index, render.frameTime), origin);
        if (slot.bitmap != nullptr) {
            const D2D1_POINT_2F start = ItemContentOrigin(_row.boxes[index], style, origin);
            render.context->DrawBitmap(slot.bitmap.Get(), D2D1::RectF(start.x, start.y, start.x + icon, start.y + icon), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
    }
}

Microsoft::WRL::ComPtr<ID2D1Bitmap> WindowList::BitmapFor(const RenderContext& render, const ManagedWindow& window, const std::shared_ptr<const IconPixels>& pixels) const {
    const auto kept = std::ranges::find_if(_slots, [&](const Slot& slot) { return slot.window.handle == window.handle && slot.pixels == pixels; });
    if (kept != _slots.end()) {
        return kept->bitmap;
    }
    if (pixels != nullptr && pixels->width > 0) {
        return CreateIconBitmap(render.context, *pixels);
    }
    return nullptr;
}

const ItemStyle& WindowList::StyleOf(const Slot& slot) const {
    if (slot.window.focused) {
        return _style.focused;
    }
    return _style.item;
}

std::vector<const ItemStyle*> WindowList::ItemStyles() const {
    std::vector<const ItemStyle*> styles;
    styles.reserve(_slots.size());
    for (const Slot& slot : _slots) {
        styles.push_back(&StyleOf(slot));
    }
    return styles;
}

std::optional<std::uint32_t> WindowList::MillisecondsUntilChange() const {
    return _fades.Wait(std::chrono::steady_clock::now());
}

bool WindowList::Hover(std::optional<D2D1_POINT_2F> point) {
    const bool changed = _hovered.Set(ItemUnderCursor(_row, point));
    _fades.Follow(ItemStyles(), _hovered, std::chrono::steady_clock::now());
    return changed;
}

void WindowList::Click(D2D1_POINT_2F point) {
    const std::optional<size_t> index = ItemAt(_row, point);
    if (index.has_value()) {
        _windowManager.FocusWindow(_slots[*index].window.id);
    }
}

const ModuleActions& WindowList::Actions() const {
    return kNoActions;
}
