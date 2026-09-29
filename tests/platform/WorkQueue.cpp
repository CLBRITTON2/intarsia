#include "platform/WorkQueue.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
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

// A queued item can block for seconds (a send to a peer that stopped reading), so stopping must not wait for them all.
TEST(WorkQueue, StopsAfterTheItemInProgressAndDropsTheRest) {
    std::mutex                mutex;
    std::vector<std::int32_t> processed;
    std::promise<void>        firstStarted;
    std::promise<void>        firstRelease;
    const std::shared_future  firstReleased = firstRelease.get_future().share();
    std::promise<void>        secondStarted;
    std::promise<void>        secondRelease;
    const std::shared_future  secondReleased = secondRelease.get_future().share();
    auto                      queue          = std::make_unique<WorkQueue<std::int32_t>>(8, [&](const std::int32_t& item) {
        if (item == 0) {
            firstStarted.set_value();
            firstReleased.wait();
        }
        if (item == 1) {
            secondStarted.set_value();
            secondReleased.wait();
        }
        const std::scoped_lock lock(mutex);
        processed.push_back(item);
    });
    // Item 0 holds the thread so 1 to 3 wait together, then item 1 holds it while the queue stops.
    ASSERT_TRUE(queue->Push(0));
    firstStarted.get_future().wait();
    for (std::int32_t item = 1; item <= 3; ++item) {
        ASSERT_TRUE(queue->Push(item));
    }
    firstRelease.set_value();
    secondStarted.get_future().wait();

    auto stopped = std::async(std::launch::async, [&queue] { queue.reset(); });
    // Time for the destructor to signal stop before item 1 returns: the queue offers no way to observe it.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    secondRelease.set_value();
    stopped.get();
    EXPECT_EQ(processed, (std::vector<std::int32_t>{0, 1}));
}
