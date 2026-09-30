#include "modules/WindowIcons.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <chrono>
#include <memory>

namespace {

    constexpr std::uint32_t             kIconChanged = WM_APP + 1;

    constexpr std::chrono::milliseconds kRetry{50};

    // A message-only window of this thread, with no icon until SetIcon gives it the stock application icon.
    class IconWindow {
      public:
        IconWindow() : _window(CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr)) {}
        ~IconWindow() {
            DestroyWindow(_window);
        }
        IconWindow(const IconWindow&)            = delete;
        IconWindow& operator=(const IconWindow&) = delete;

        void        SetIcon() const {
            SendMessageW(_window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(LoadIconW(nullptr, IDI_APPLICATION)));
        }

        HWND Get() const {
            return _window;
        }

      private:
        HWND _window;
    };

    // Handles this thread's messages for a moment: the reader thread's WM_GETICON waits on this thread. True once the
    // cache has posted its change message.
    bool Pump(std::uint32_t changeMessage) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        bool changed = false;
        MSG  message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            changed = changed || message.message == changeMessage;
            DispatchMessageW(&message);
        }
        return changed;
    }

    // Asks until the icon arrives.
    std::shared_ptr<const IconPixels> WaitForIcon(WindowIcons& icons, HWND window) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            std::shared_ptr<const IconPixels> pixels = icons.For(reinterpret_cast<std::intptr_t>(window));
            if (pixels != nullptr) {
                return pixels;
            }
            Pump(kIconChanged);
        }
        return nullptr;
    }

    // Waits for a read to end, whether or not it found an icon.
    bool WaitForReadToEnd(ChangeSignal& changes) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            if (Pump(kIconChanged)) {
                changes.Acknowledge();
                return true;
            }
        }
        return false;
    }

} // namespace

TEST(WindowIcons, AWindowThatIsGoneHasNoIcon) {
    HWND window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    DestroyWindow(window);
    EXPECT_EQ(WindowIcon(window, 100), nullptr);
}

// A workspace switch hides a window without closing it, and switching back must find its icon without a new read.
TEST(WindowIcons, KeepsTheIconOfAWindowThatStillExists) {
    const IconWindow window;
    window.SetIcon();
    HWND target = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    {
        ChangeSignal changes(target, kIconChanged);
        WindowIcons  icons(changes, kRetry);
        EXPECT_EQ(icons.For(reinterpret_cast<std::intptr_t>(window.Get())), nullptr);
        const std::shared_ptr<const IconPixels> pixels = WaitForIcon(icons, window.Get());
        ASSERT_NE(pixels, nullptr);

        icons.ForgetClosed();
        EXPECT_EQ(icons.For(reinterpret_cast<std::intptr_t>(window.Get())), pixels);
    }
    DestroyWindow(target);
}

TEST(WindowIcons, ForgetsAClosedWindow) {
    HWND target = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    {
        ChangeSignal  changes(target, kIconChanged);
        WindowIcons   icons(changes, kRetry);
        std::intptr_t handle = 0;
        {
            const IconWindow window;
            window.SetIcon();
            handle = reinterpret_cast<std::intptr_t>(window.Get());
            ASSERT_NE(WaitForIcon(icons, window.Get()), nullptr);
        }
        icons.ForgetClosed();
        // Forgotten, so the ask starts a new read.
        EXPECT_EQ(icons.For(handle), nullptr);
    }
    DestroyWindow(target);
}

// Apps often set their icon after the window appears, so a first read finds none.
TEST(WindowIcons, ReadsAgainAWindowThatShowedNoIcon) {
    const IconWindow window;
    HWND             target = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    {
        ChangeSignal changes(target, kIconChanged);
        WindowIcons  icons(changes, kRetry);
        EXPECT_EQ(icons.For(reinterpret_cast<std::intptr_t>(window.Get())), nullptr);
        ASSERT_TRUE(WaitForReadToEnd(changes));
        EXPECT_EQ(icons.For(reinterpret_cast<std::intptr_t>(window.Get())), nullptr);

        window.SetIcon();
        EXPECT_NE(WaitForIcon(icons, window.Get()), nullptr);
    }
    DestroyWindow(target);
}
