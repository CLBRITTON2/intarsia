#include "platform/WorkQueue.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <future>
#include <mutex>
#include <vector>

TEST(WorkQueue, ProcessesItemsInOrder) {
    std::mutex                mutex;
    std::vector<std::int32_t> processed;
    std::promise<void>        done;
    {
        WorkQueue<std::int32_t> queue(8, [&](const std::int32_t& item) {
            const std::scoped_lock lock(mutex);
            processed.push_back(item);
            if (item == 3) {
                done.set_value();
            }
        });
        for (std::int32_t item = 1; item <= 3; ++item) {
            ASSERT_TRUE(queue.Push(item));
        }
        done.get_future().wait();
    }
    EXPECT_EQ(processed, (std::vector<std::int32_t>{1, 2, 3}));
}

TEST(WorkQueue, RejectsAnItemPastCapacity) {
    std::promise<void>       started;
    std::promise<void>       release;
    const std::shared_future released = release.get_future().share();
    WorkQueue<std::int32_t>  queue(2, [&](const std::int32_t& item) {
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
