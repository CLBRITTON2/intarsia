#include "modules/WindowIcons.hpp"

#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Win32Cast.hpp"

#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <algorithm>
#include <array>
#include <string>
#include <type_traits>

using Microsoft::WRL::ComPtr;

namespace {

    // Long enough for a busy window to answer, short enough that a hung one barely delays the rest of the queue.
    constexpr std::uint32_t kIconTimeoutMs = 100;
    // Windows appear a few at a time, so a full queue means a burst: the rest are asked for again on the next layout.
    constexpr size_t kMaxQueuedReads = 64;

} // namespace

HICON WindowIcon(HWND window, std::uint32_t timeoutMs) {
    for (const WPARAM size : {static_cast<WPARAM>(ICON_BIG), static_cast<WPARAM>(ICON_SMALL2), static_cast<WPARAM>(ICON_SMALL)}) {
        DWORD_PTR icon = 0;
        if (SendMessageTimeoutW(window, WM_GETICON, size, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, timeoutMs, &icon) != 0 && icon != 0) {
            return IntToPointer<std::remove_pointer_t<HICON>>(static_cast<LONG_PTR>(icon));
        }
    }
    for (const std::int32_t index : {GCLP_HICON, GCLP_HICONSM}) {
        const ULONG_PTR icon = GetClassLongPtrW(window, index);
        if (icon != 0) {
            return IntToPointer<std::remove_pointer_t<HICON>>(static_cast<LONG_PTR>(icon));
        }
    }
    return nullptr;
}

IconPixels IconToPixels(HICON icon) {
    ComPtr<IWICImagingFactory> factory;
    ThrowIfFailed(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "CoCreateInstance(CLSID_WICImagingFactory)");
    ComPtr<IWICBitmap> bitmap;
    ThrowIfFailed(factory->CreateBitmapFromHICON(icon, &bitmap), "IWICImagingFactory::CreateBitmapFromHICON");
    ComPtr<IWICFormatConverter> converter;
    ThrowIfFailed(factory->CreateFormatConverter(&converter), "IWICImagingFactory::CreateFormatConverter");
    ThrowIfFailed(converter->Initialize(bitmap.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom),
                  "IWICFormatConverter::Initialize");

    IconPixels pixels{.width = 0, .height = 0, .pbgra = {}};
    ThrowIfFailed(converter->GetSize(&pixels.width, &pixels.height), "IWICFormatConverter::GetSize");
    const std::uint32_t stride = pixels.width * 4;
    pixels.pbgra.resize(static_cast<size_t>(stride) * pixels.height);
    ThrowIfFailed(converter->CopyPixels(nullptr, stride, static_cast<std::uint32_t>(pixels.pbgra.size()), pixels.pbgra.data()), "IWICFormatConverter::CopyPixels");
    return pixels;
}

WindowIcons::WindowIcons(ChangeSignal& changes) : _changes(changes), _reader(kMaxQueuedReads, [this](const std::intptr_t& window) { Read(window); }) {}

std::shared_ptr<const IconPixels> WindowIcons::For(std::intptr_t window) {
    {
        const std::scoped_lock lock(_mutex);
        const auto             icon = _icons.find(window);
        if (icon != _icons.end()) {
            return icon->second;
        }
        _icons.emplace(window, nullptr);
    }
    if (!_reader.Push(window)) {
        const std::scoped_lock lock(_mutex);
        _icons.erase(window);
    }
    return nullptr;
}

void WindowIcons::Retain(const std::vector<std::intptr_t>& windows) {
    const std::scoped_lock lock(_mutex);
    std::erase_if(_icons, [&windows](const auto& icon) { return std::ranges::find(windows, icon.first) == windows.end(); });
}

// Runs on the reader thread, so failures are logged: the window then shows no icon.
void WindowIcons::Read(std::intptr_t window) {
    std::shared_ptr<const IconPixels> pixels;
    // WIC needs an apartment on the calling thread.
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
    try {
        HICON icon = WindowIcon(IntToPointer<std::remove_pointer_t<HWND>>(window), kIconTimeoutMs);
        if (icon != nullptr) {
            pixels = std::make_shared<const IconPixels>(IconToPixels(icon));
        }
    } catch (const Win32Error& error) {
        Log(LogLevel::Warning, "window_icons", "read_failed", {{.key = "window", .value = std::to_string(window)}, {.key = "reason", .value = error.what()}});
    }
    if (SUCCEEDED(initialized)) {
        CoUninitialize();
    }
    {
        const std::scoped_lock lock(_mutex);
        const auto             icon = _icons.find(window);
        // Retain dropped it while it was being read: the window is gone.
        if (icon == _icons.end()) {
            return;
        }
        icon->second = std::move(pixels);
    }
    _changes.Notify("window_icons");
}
