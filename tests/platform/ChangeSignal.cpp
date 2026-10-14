#include "platform/ChangeSignal.hpp"
#include "support/ChangeTarget.hpp"

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
    ChangeTarget target(kChanged);

    for (std::int32_t change = 0; change < 1000; ++change) {
        target.signal.Notify("test");
    }
    EXPECT_EQ(QueuedChanges(target.window.Get()), 1);

    target.signal.Acknowledge();
    target.signal.Notify("test");
    target.signal.Notify("test");
    EXPECT_EQ(QueuedChanges(target.window.Get()), 1);
}
