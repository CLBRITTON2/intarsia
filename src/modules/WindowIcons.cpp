#include "modules/WindowIcons.hpp"

#include "platform/ComScope.hpp"
#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Win32Cast.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

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
