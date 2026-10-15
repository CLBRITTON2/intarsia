#include "modules/WindowList.hpp"

#include "../wm/FakeWindowManager.hpp"
#include "TestStyle.hpp"
#include "TextOnlyRender.hpp"
#include "render/Box.hpp"
#include "render/Graphics.hpp"
#include "support/MessageWindow.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

    constexpr std::uint32_t             kIconChanged = WM_APP + 1;

    constexpr std::chrono::milliseconds kRetry{50};

    void                                SetStockIcon(const MessageWindow& window) {
        SendMessageW(window.Get(), WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(LoadIconW(nullptr, IDI_APPLICATION)));
    }

    // Asks until the icon arrives. The reader thread's WM_GETICON waits on this thread.
    std::shared_ptr<const IconPixels> WaitForIcon(WindowIcons& icons, HWND window) {
        std::shared_ptr<const IconPixels> pixels;
        AnsweringUntil(
            [&] {
                pixels = icons.For(reinterpret_cast<std::intptr_t>(window));
                return pixels != nullptr;
            },
            std::chrono::seconds(5));
        return pixels;
    }

    // Waits for a read to end, whether or not it found an icon.
    bool WaitForReadToEnd(ChangeTarget& target) {
        MSG message{};
        if (!AnsweringUntil([&] { return PeekMessageW(&message, target.window.Get(), kIconChanged, kIconChanged, PM_REMOVE) != FALSE; }, std::chrono::seconds(5))) {
            return false;
        }
        target.signal.Acknowledge();
        return true;
    }

    // Handles no window has, so every icon read finds nothing and no bitmap is ever made.
    const std::vector<ManagedWindow> kWindows{
        {
            .id      = "a",
            .handle  = 0x7FFF0001,
            .focused = false,
            .monitor = 10,
        },
        {
            .id      = "b",
            .handle  = 0x7FFF0002,
            .focused = true,
            .monitor = 10,
        },
        {
            .id      = "c",
            .handle  = 0x7FFF0003,
            .focused = false,
            .monitor = 20,
        },
    };

    class WindowListModule : public testing::Test {
      public:
        WindowListModule() : _target(WM_APP + 1), _icons(_target.signal, std::chrono::milliseconds(10000)) {}

      protected:
        TextOnlyRender    _render;
        ChangeTarget      _target;
        WindowIcons       _icons;
        FakeWindowManager _windowManager;
    };

} // namespace

TEST(WindowIcons, AWindowThatIsGoneHasNoIcon) {
    HWND gone = nullptr;
    {
        const MessageWindow window;
        gone = window.Get();
    }
    EXPECT_EQ(WindowIcon(gone, 100), nullptr);
}

// A workspace switch hides a window without closing it, and switching back must find its icon without a new read.
TEST(WindowIcons, KeepsTheIconOfAWindowThatStillExists) {
    const MessageWindow window;
    SetStockIcon(window);
    ChangeTarget target(kIconChanged);
    WindowIcons  icons(target.signal, kRetry);
    EXPECT_EQ(icons.For(reinterpret_cast<std::intptr_t>(window.Get())), nullptr);
    const std::shared_ptr<const IconPixels> pixels = WaitForIcon(icons, window.Get());
    ASSERT_NE(pixels, nullptr);

    icons.ForgetClosed();
    EXPECT_EQ(icons.For(reinterpret_cast<std::intptr_t>(window.Get())), pixels);
}

TEST(WindowIcons, ForgetsAClosedWindow) {
    ChangeTarget  target(kIconChanged);
    WindowIcons   icons(target.signal, kRetry);
    std::intptr_t handle = 0;
    {
        const MessageWindow window;
        SetStockIcon(window);
        handle = reinterpret_cast<std::intptr_t>(window.Get());
        ASSERT_NE(WaitForIcon(icons, window.Get()), nullptr);
    }
    icons.ForgetClosed();
    // Forgotten, so the ask starts a new read.
    EXPECT_EQ(icons.For(handle), nullptr);
}

// Apps often set their icon after the window appears, so a first read finds none.
TEST(WindowIcons, ReadsAgainAWindowThatShowedNoIcon) {
    const MessageWindow window;
    ChangeTarget        target(kIconChanged);
    WindowIcons         icons(target.signal, kRetry);
    EXPECT_EQ(icons.For(reinterpret_cast<std::intptr_t>(window.Get())), nullptr);
    ASSERT_TRUE(WaitForReadToEnd(target));
    EXPECT_EQ(icons.For(reinterpret_cast<std::intptr_t>(window.Get())), nullptr);

    SetStockIcon(window);
    EXPECT_NE(WaitForIcon(icons, window.Get()), nullptr);
}

TEST_F(WindowListModule, LayoutReportsAChangeOnlyWhenTheWindowsChange) {
    WindowList module(_windowManager, _icons, reinterpret_cast<HMONITOR>(10), TestModuleStyle());
    _windowManager.windows = kWindows;
    EXPECT_TRUE(module.Layout(_render.Context()));
    EXPECT_FALSE(module.Layout(_render.Context()));

    _windowManager.windows[0].focused = true;
    EXPECT_TRUE(module.Layout(_render.Context()));
    // A window on another monitor is not this bar's to show.
    _windowManager.windows[2].focused = true;
    EXPECT_FALSE(module.Layout(_render.Context()));
    _windowManager.windows.pop_back();
    _windowManager.windows.pop_back();
    EXPECT_TRUE(module.Layout(_render.Context()));
}

TEST_F(WindowListModule, SizeCoversEveryPaddedIconAndTheGapsBetween) {
    WindowList module(_windowManager, _icons, reinterpret_cast<HMONITOR>(10), TestModuleStyle());
    _windowManager.windows = kWindows;
    module.Layout(_render.Context());
    // Two items of a 20 icon padded 4 across and 2 down, with 5 of margin on either side of each.
    EXPECT_FLOAT_EQ(module.Size().width, 76.0f);
    EXPECT_FLOAT_EQ(module.Size().height, 24.0f);

    _windowManager.windows.clear();
    module.Layout(_render.Context());
    EXPECT_FLOAT_EQ(module.Size().width, 0.0f);
}

// The icon's pixels go up to the GPU as a bitmap and draw: a real device, a real window's icon.
TEST_F(WindowListModule, AnIconThatArrivesIsLaidOutAndDrawn) {
    const MessageWindow iconWindow;
    SendMessageW(iconWindow.Get(), WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(LoadIconW(nullptr, IDI_APPLICATION)));
    _windowManager.windows = {{
        .id      = "a",
        .handle  = reinterpret_cast<std::intptr_t>(iconWindow.Get()),
        .focused = true,
        .monitor = 10,
    }};

    const CompositionThread                    compositionThread;
    const GraphicsDevice                       graphics = CreateGraphicsDevice();
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> context;
    ASSERT_TRUE(SUCCEEDED(graphics.d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context)));
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> target;
    ASSERT_TRUE(SUCCEEDED(context->CreateBitmap(D2D1::SizeU(64, 64), nullptr, 0,
                                                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                                                &target)));
    context->SetTarget(target.Get());
    BrushCache    brushes(context.Get());
    BoxCache      boxes(context.Get());
    RenderContext render = _render.Context();
    render.context       = context.Get();
    render.brushes       = &brushes;
    render.boxes         = &boxes;

    WindowList module(_windowManager, _icons, reinterpret_cast<HMONITOR>(10), TestModuleStyle());
    EXPECT_TRUE(module.Layout(render));
    // The icon is read on the reader thread, whose WM_GETICON this thread answers while it waits.
    EXPECT_TRUE(AnsweringUntil([&] { return module.Layout(render); }, std::chrono::seconds(5)));

    context->BeginDraw();
    module.Draw(render, D2D1::Point2F(0.0f, 0.0f));
    EXPECT_TRUE(SUCCEEDED(context->EndDraw()));
}
