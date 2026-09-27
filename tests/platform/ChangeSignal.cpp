#include "platform/ChangeSignal.hpp"

#include <gtest/gtest.h>

namespace {

    constexpr std::uint32_t kChanged = WM_APP + 7;

    std::int32_t            QueuedChanges(HWND target) {
        std::int32_t count   = 0;
        MSG          message = {};
        while (PeekMessageW(&message, target, kChanged, kChanged, PM_REMOVE)) {
            ++count;
        }
        return count;
    }

} // namespace

// A source changing faster than the UI thread reads must not fill the queue: WM_QUIT waits behind every posted message.
TEST(ChangeSignal, PostsOnceUntilAcknowledged) {
    HWND         target = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    ChangeSignal changes(target, kChanged);

    for (std::int32_t change = 0; change < 1000; ++change) {
        changes.Notify("test");
    }
    EXPECT_EQ(QueuedChanges(target), 1);

    changes.Acknowledge();
    changes.Notify("test");
    changes.Notify("test");
    EXPECT_EQ(QueuedChanges(target), 1);
    DestroyWindow(target);
}
