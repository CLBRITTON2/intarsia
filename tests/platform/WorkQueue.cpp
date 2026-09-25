#include "platform/WorkQueue.hpp"

#include <gtest/gtest.h>

#include <future>
#include <mutex>
#include <vector>

TEST(WorkQueue, ProcessesItemsInOrder) {
    std::mutex         mutex;
    std::vector<int>   processed;
    std::promise<void> done;
    {
        WorkQueue<int> queue(8, [&](const int& item) {
            const std::scoped_lock lock(mutex);
            processed.push_back(item);
            if (item == 3) {
                done.set_value();
            }
        });
        for (int item = 1; item <= 3; ++item) {
            ASSERT_TRUE(queue.Push(item));
        }
        done.get_future().wait();
    }
    EXPECT_EQ(processed, (std::vector<int>{1, 2, 3}));
}

TEST(WorkQueue, RejectsAnItemPastCapacity) {
    std::promise<void>       started;
    std::promise<void>       release;
    const std::shared_future released = release.get_future().share();
    WorkQueue<int>           queue(2, [&](const int& item) {
        if (item == 0) {
            started.set_value();
            released.wait();
        }
    });
    ASSERT_TRUE(queue.Push(0));
    started.get_future().wait();
    EXPECT_TRUE(queue.Push(1));
    EXPECT_TRUE(queue.Push(2));
    EXPECT_FALSE(queue.Push(3));
    release.set_value();
}
