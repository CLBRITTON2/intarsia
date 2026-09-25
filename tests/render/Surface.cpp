#include "render/Surface.hpp"

#include "platform/Errors.hpp"

#include <gtest/gtest.h>

#include <memory>

namespace {

    HWND CreatePopup() {
        HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP, L"STATIC", L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
        if (window == nullptr) {
            ThrowLastError("CreateWindowExW");
        }
        return window;
    }

} // namespace

// The tooltip releases its surface on every hide and makes a new one on the next show, on the same window.
TEST(Surface, AWindowTakesANewSurfaceOnlyOnceTheOldOneIsGone) {
    const GraphicsDevice graphics = CreateGraphicsDevice();
    HWND                 window   = CreatePopup();

    auto                 first = std::make_unique<Surface>(graphics, window, 64, 32, USER_DEFAULT_SCREEN_DPI);
    EXPECT_ANY_THROW(Surface(graphics, window, 64, 32, USER_DEFAULT_SCREEN_DPI));
    first.reset();
    EXPECT_NO_THROW(Surface(graphics, window, 640, 320, USER_DEFAULT_SCREEN_DPI));

    DestroyWindow(window);
}
