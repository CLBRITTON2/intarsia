#include "Capture.hpp"

#include "platform/Errors.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>

namespace integration {

    RECT ScreenRect(HWND window) {
        RECT rect{};
        if (!GetWindowRect(window, &rect)) {
            ThrowLastError("GetWindowRect");
        }
        return rect;
    }

    Capture CaptureScreenOver(HWND window) {
        const RECT rect = ScreenRect(window);
        Capture    capture{
               .width  = rect.right - rect.left,
               .height = rect.bottom - rect.top,
               .pixels = {},
        };
        capture.pixels.resize(static_cast<size_t>(capture.width) * static_cast<size_t>(capture.height));

        HDC screen = GetDC(nullptr);
        if (screen == nullptr) {
            throw std::runtime_error("GetDC(screen) failed");
        }
        HDC     memory = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, capture.width, capture.height);
        HGDIOBJ old    = SelectObject(memory, bitmap);
        // CAPTUREBLT takes layered windows too, as the bar is one.
        const BOOL copied = BitBlt(memory, 0, 0, capture.width, capture.height, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT);
        SelectObject(memory, old);
        // A negative height asks for rows top to bottom.
        BITMAPINFO info{};
        info.bmiHeader = BITMAPINFOHEADER{
            .biSize          = sizeof(BITMAPINFOHEADER),
            .biWidth         = capture.width,
            .biHeight        = -capture.height,
            .biPlanes        = 1,
            .biBitCount      = 32,
            .biCompression   = BI_RGB,
            .biSizeImage     = 0,
            .biXPelsPerMeter = 0,
            .biYPelsPerMeter = 0,
            .biClrUsed       = 0,
            .biClrImportant  = 0,
        };
        const std::int32_t rows = GetDIBits(memory, bitmap, 0, static_cast<UINT>(capture.height), capture.pixels.data(), &info, DIB_RGB_COLORS);
        DeleteObject(bitmap);
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        if (!copied) {
            ThrowLastError("BitBlt(screen)");
        }
        if (rows != capture.height) {
            throw std::runtime_error(std::format("GetDIBits copied {} of {} rows", rows, capture.height));
        }
        for (std::uint32_t& pixel : capture.pixels) {
            pixel &= 0xFFFFFF;
        }
        return capture;
    }

    size_t CountPixels(const Capture& capture, std::uint32_t rgb) {
        return static_cast<size_t>(std::ranges::count(capture.pixels, rgb));
    }

    std::optional<std::int32_t> ColumnSpan(const Capture& capture, std::uint32_t rgb) {
        std::optional<std::int32_t> leftmost;
        std::optional<std::int32_t> rightmost;
        for (size_t index = 0; index < capture.pixels.size(); ++index) {
            if (capture.pixels[index] == rgb) {
                const auto column = static_cast<std::int32_t>(index % static_cast<size_t>(capture.width));
                leftmost          = std::min(leftmost.value_or(column), column);
                rightmost         = std::max(rightmost.value_or(column), column);
            }
        }
        if (!leftmost.has_value() || !rightmost.has_value()) {
            return std::nullopt;
        }
        return *rightmost - *leftmost + 1;
    }

    std::optional<double> MeanColumn(const Capture& capture, std::uint32_t rgb) {
        double sum   = 0;
        size_t count = 0;
        for (size_t index = 0; index < capture.pixels.size(); ++index) {
            if (capture.pixels[index] == rgb) {
                sum += static_cast<double>(index % static_cast<size_t>(capture.width));
                ++count;
            }
        }
        if (count == 0) {
            return std::nullopt;
        }
        return sum / static_cast<double>(count);
    }

} // namespace integration
