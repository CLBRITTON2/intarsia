#include "render/Surface.hpp"

#include "platform/Errors.hpp"
#include "render/Graphics.hpp"

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
    const CompositionThread compositionThread;
    const GraphicsDevice    graphics = CreateGraphicsDevice();
    HWND                    window   = CreatePopup();

    auto                    first = std::make_unique<Surface>(graphics, window, 64, 32, USER_DEFAULT_SCREEN_DPI);
    EXPECT_ANY_THROW(Surface(graphics, window, 64, 32, USER_DEFAULT_SCREEN_DPI));
    first.reset();
    EXPECT_NO_THROW(Surface(graphics, window, 640, 320, USER_DEFAULT_SCREEN_DPI));

    DestroyWindow(window);
}

// Boxes come and go, change size, an empty one draws no mask, a DPI change re-places every box, and a new tone remakes
// every brush.
TEST(Surface, BackdropsFollowTheBoxesAndToneOfEachCall) {
    const CompositionThread compositionThread;
    const GraphicsDevice    graphics = CreateGraphicsDevice();
    HWND                    window   = CreatePopup();

    Surface                 surface(graphics, window, 200, 40, USER_DEFAULT_SCREEN_DPI);
    const D2D1_ROUNDED_RECT first  = D2D1::RoundedRect(D2D1::RectF(0.0f, 0.0f, 80.0f, 40.0f), 8.0f, 8.0f);
    const D2D1_ROUNDED_RECT second = D2D1::RoundedRect(D2D1::RectF(100.0f, 4.0f, 180.0f, 36.0f), 4.0f, 4.0f);
    const D2D1_ROUNDED_RECT empty  = D2D1::RoundedRect(D2D1::RectF(190.0f, 0.0f, 190.0f, 40.0f), 0.0f, 0.0f);
    const Backdrop          plain{
                 .blur       = true,
                 .brightness = 1.0f,
                 .saturation = 1.0f,
    };
    const Backdrop toned{
        .blur       = true,
        .brightness = 0.4f,
        .saturation = 1.8f,
    };
    EXPECT_NO_THROW(surface.ShowBackdrops({first, second}, plain));
    EXPECT_NO_THROW(surface.ShowBackdrops({first, second, empty}, plain));
    EXPECT_NO_THROW(surface.ShowBackdrops({second}, toned));
    surface.Resize(300, 60, 144);
    EXPECT_NO_THROW(surface.ShowBackdrops({second, first}, toned));
    EXPECT_NO_THROW(surface.ShowBackdrops({}, plain));
    EXPECT_NO_THROW(surface.ShowBackdrops({first}, plain));

    DestroyWindow(window);
}
