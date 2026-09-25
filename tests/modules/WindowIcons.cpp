#include "modules/WindowIcons.hpp"

#include <objbase.h>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>

namespace {

    constexpr UINT kIconChanged = WM_APP + 1;

    // A message-only window of this thread, carrying the stock application icon as its own.
    class IconWindow {
      public:
        IconWindow() : _window(CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr)) {
            SendMessageW(_window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(LoadIconW(nullptr, IDI_APPLICATION)));
        }
        ~IconWindow() {
            DestroyWindow(_window);
        }
        IconWindow(const IconWindow&)            = delete;
        IconWindow& operator=(const IconWindow&) = delete;

        HWND        Get() const {
            return _window;
        }

      private:
        HWND _window;
    };

    // Asks until the icon arrives, pumping messages meanwhile: the reader thread's WM_GETICON waits on this thread.
    std::shared_ptr<const IconPixels> WaitForIcon(WindowIcons& icons, HWND window) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            std::shared_ptr<const IconPixels> pixels = icons.For(reinterpret_cast<std::intptr_t>(window));
            if (pixels != nullptr) {
                return pixels;
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                DispatchMessageW(&message);
            }
        }
        return nullptr;
    }

} // namespace

TEST(WindowIcons, WindowIconReadsTheIconAWindowWasGiven) {
    const IconWindow window;
    EXPECT_EQ(WindowIcon(window.Get(), 100), LoadIconW(nullptr, IDI_APPLICATION));
}

TEST(WindowIcons, AWindowThatIsGoneHasNoIcon) {
    HWND window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    DestroyWindow(window);
    EXPECT_EQ(WindowIcon(window, 100), nullptr);
}

TEST(WindowIcons, IconToPixelsGivesPremultipliedPixelsAtTheIconSize) {
    ASSERT_TRUE(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
    const IconPixels pixels = IconToPixels(LoadIconW(nullptr, IDI_APPLICATION));
    CoUninitialize();
    EXPECT_GT(pixels.width, 0u);
    EXPECT_GT(pixels.height, 0u);
    EXPECT_EQ(pixels.pbgra.size(), static_cast<size_t>(pixels.width) * pixels.height * 4);
}

TEST(WindowIcons, ReadsOnItsThreadAndForgetsWindowsRetainDrops) {
    const IconWindow window;
    HWND             target = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    {
        WindowIcons icons(target, kIconChanged);
        EXPECT_EQ(icons.For(reinterpret_cast<std::intptr_t>(window.Get())), nullptr);
        const std::shared_ptr<const IconPixels> pixels = WaitForIcon(icons, window.Get());
        ASSERT_NE(pixels, nullptr);
        EXPECT_EQ(icons.For(reinterpret_cast<std::intptr_t>(window.Get())), pixels);

        icons.Retain({});
        // Forgotten, so the next ask starts a new read.
        EXPECT_EQ(icons.For(reinterpret_cast<std::intptr_t>(window.Get())), nullptr);
    }
    DestroyWindow(target);
}
