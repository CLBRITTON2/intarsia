#include "modules/WindowIcons.hpp"
#include "support/ChangeTarget.hpp"
#include "support/Eventually.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <chrono>
#include <memory>

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
