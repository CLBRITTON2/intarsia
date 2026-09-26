#include "platform/ChangeSignal.hpp"

#include <gtest/gtest.h>

namespace {

    constexpr UINT kChanged = WM_APP + 7;

    int            QueuedChanges(HWND target) {
        int count   = 0;
        MSG message = {};
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

    for (int change = 0; change < 1000; ++change) {
        changes.Notify("test");
    }
    EXPECT_EQ(QueuedChanges(target), 1);

    changes.Acknowledge();
    changes.Notify("test");
    changes.Notify("test");
    EXPECT_EQ(QueuedChanges(target), 1);
    DestroyWindow(target);
}
