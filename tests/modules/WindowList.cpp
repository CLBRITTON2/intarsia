#include "modules/WindowList.hpp"

#include "../wm/FakeWindowManager.hpp"
#include "TestStyle.hpp"
#include "render/BrushCache.hpp"
#include "render/Graphics.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

namespace {

    // Handles no window has, so every icon read finds nothing and no bitmap is ever made.
    const std::vector<ManagedWindow> kWindows{
        {.id = "a", .handle = 0x7FFF0001, .focused = false, .monitor = 10},
        {.id = "b", .handle = 0x7FFF0002, .focused = true, .monitor = 10},
        {.id = "c", .handle = 0x7FFF0003, .focused = false, .monitor = 20},
    };

    RenderContext NoDrawing() {
        return RenderContext{.context       = nullptr,
                             .dwrite        = nullptr,
                             .textFormat    = nullptr,
                             .iconFont      = nullptr,
                             .iconSizeDips  = 20.0f,
                             .iconGapDips   = 0.0f,
                             .foreground    = nullptr,
                             .accent        = nullptr,
                             .alert         = nullptr,
                             .warning       = {.text = nullptr, .icon = nullptr},
                             .critical      = {.text = nullptr, .icon = nullptr},
                             .brushes       = nullptr,
                             .boxHeightDips = 24.0f};
    }

    class WindowListModule : public testing::Test {
      public:
        WindowListModule() : _target(CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr)), _changes(_target, WM_APP + 1), _icons(_changes) {}
        ~WindowListModule() override {
            DestroyWindow(_target);
        }
        WindowListModule(const WindowListModule&)            = delete;
        WindowListModule& operator=(const WindowListModule&) = delete;

      protected:
        HWND              _target;
        ChangeSignal      _changes;
        WindowIcons       _icons;
        FakeWindowManager _windowManager;
    };

} // namespace

TEST(WindowList, WindowsOnMonitorKeepsOnlyThatMonitorsInOrder) {
    const std::vector<ManagedWindow> shown = WindowsOnMonitor(kWindows, 10);
    ASSERT_EQ(shown.size(), 2u);
    EXPECT_EQ(shown[0].id, "a");
    EXPECT_EQ(shown[1].id, "b");
    EXPECT_TRUE(WindowsOnMonitor(kWindows, 30).empty());
}

TEST_F(WindowListModule, LayoutReportsAChangeOnlyWhenTheWindowsChange) {
    WindowList module(_windowManager, _icons, reinterpret_cast<HMONITOR>(10), TestModuleStyle());
    _windowManager.windows = kWindows;
    EXPECT_TRUE(module.Layout(NoDrawing()));
    EXPECT_FALSE(module.Layout(NoDrawing()));

    _windowManager.windows[0].focused = true;
    EXPECT_TRUE(module.Layout(NoDrawing()));
    // A window on another monitor is not this bar's to show.
    _windowManager.windows[2].focused = true;
    EXPECT_FALSE(module.Layout(NoDrawing()));
    _windowManager.windows.pop_back();
    _windowManager.windows.pop_back();
    EXPECT_TRUE(module.Layout(NoDrawing()));
}

TEST_F(WindowListModule, SizeCoversEveryPaddedIconAndTheGapsBetween) {
    WindowList module(_windowManager, _icons, reinterpret_cast<HMONITOR>(10), TestModuleStyle());
    _windowManager.windows = kWindows;
    module.Layout(NoDrawing());
    // Two items of a 20 icon padded 4 across and 2 down, with 5 of margin on either side of each.
    EXPECT_FLOAT_EQ(module.Size().width, 76.0f);
    EXPECT_FLOAT_EQ(module.Size().height, 24.0f);

    _windowManager.windows.clear();
    module.Layout(NoDrawing());
    EXPECT_FLOAT_EQ(module.Size().width, 0.0f);
}

// The icon's pixels go up to the GPU as a bitmap and draw: a real device, a real window's icon.
TEST_F(WindowListModule, AnIconThatArrivesIsLaidOutAndDrawn) {
    HWND iconWindow = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    SendMessageW(iconWindow, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(LoadIconW(nullptr, IDI_APPLICATION)));
    _windowManager.windows = {{.id = "a", .handle = reinterpret_cast<std::intptr_t>(iconWindow), .focused = true, .monitor = 10}};

    const GraphicsDevice                       graphics = CreateGraphicsDevice();
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> context;
    ASSERT_TRUE(SUCCEEDED(graphics.d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context)));
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> target;
    ASSERT_TRUE(SUCCEEDED(context->CreateBitmap(D2D1::SizeU(64, 64), nullptr, 0,
                                                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                                                &target)));
    context->SetTarget(target.Get());
    BrushCache    brushes(context.Get());
    RenderContext render = NoDrawing();
    render.context       = context.Get();
    render.brushes       = &brushes;

    WindowList module(_windowManager, _icons, reinterpret_cast<HMONITOR>(10), TestModuleStyle());
    EXPECT_TRUE(module.Layout(render));
    // The icon is read on the reader thread, whose WM_GETICON this thread answers while it pumps.
    bool       arrived  = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!arrived && std::chrono::steady_clock::now() < deadline) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            DispatchMessageW(&message);
        }
        arrived = module.Layout(render);
    }
    EXPECT_TRUE(arrived);

    context->BeginDraw();
    module.Draw(render, D2D1::Point2F(0.0f, 0.0f));
    EXPECT_TRUE(SUCCEEDED(context->EndDraw()));
    DestroyWindow(iconWindow);
}

TEST_F(WindowListModule, ClickFocusesTheWindowUnderIt) {
    WindowList module(_windowManager, _icons, reinterpret_cast<HMONITOR>(10), TestModuleStyle());
    _windowManager.windows = kWindows;
    module.Layout(NoDrawing());
    // Items at 5 to 33 and 43 to 71.
    module.Click(D2D1::Point2F(45.0f, 5.0f));
    EXPECT_EQ(_windowManager.focusedWindow, "b");

    _windowManager.focusedWindow.clear();
    module.Click(D2D1::Point2F(38.0f, 5.0f));
    EXPECT_TRUE(_windowManager.focusedWindow.empty());
}
