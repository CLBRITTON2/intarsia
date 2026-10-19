#include "platform/Threads.hpp"

#include "support/MessageWindow.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

    // A makeProcess that hands the queue process as it is.
    std::function<WorkQueue<std::int32_t>::Process()> Handling(const WorkQueue<std::int32_t>::Process& process) {
        return [process] { return process; };
    }

    // Notes the thread it is destroyed on.
    class ThreadNoter {
      public:
        explicit ThreadNoter(std::thread::id* destroyedOn) : _destroyedOn(destroyedOn) {}
        ~ThreadNoter() {
            *_destroyedOn = std::this_thread::get_id();
        }
        ThreadNoter(const ThreadNoter&) = delete;
        ThreadNoter& operator=(const ThreadNoter&) = delete;

      private:
        std::thread::id* _destroyedOn;
    };

    constexpr std::uint32_t kPosted = WM_APP + 7;

    // Removes every kPosted message queued for target and counts them.
    std::int32_t TakePosted(HWND target) {
        std::int32_t count = 0;
        MSG message = {};
        while (PeekMessageW(&message, target, kPosted, kPosted, PM_REMOVE)) {
            ++count;
        }
        return count;
    }

    std::string RethrownMessage(ThreadFailure& failure) {
        try {
            failure.RethrowReported();
        } catch (const std::runtime_error& error) { return error.what(); }
        return "";
    }

} // namespace

TEST(WorkQueue, RejectsAnItemPastCapacity) {
    std::promise<void> started;
    std::promise<void> release;
    const std::shared_future released = release.get_future().share();
    WorkQueue<std::int32_t> queue(2, Handling([&](const std::int32_t& item) {
                                      if (item == 0) {
                                          started.set_value();
                                          released.wait();
                                      }
                                  }));
    ASSERT_TRUE(queue.Push(0));
    started.get_future().wait();
    EXPECT_TRUE(queue.Push(1));
    EXPECT_TRUE(queue.Push(2));
    EXPECT_FALSE(queue.Push(3));
    release.set_value();
}

// A queued item can block for seconds (a send to a peer that stopped reading), so stopping must not wait for them all.
TEST(WorkQueue, StopsAfterTheItemInProgressAndDropsTheRest) {
    std::mutex mutex;
    std::vector<std::int32_t> processed;
    std::promise<void> firstStarted;
    std::promise<void> firstRelease;
    const std::shared_future firstReleased = firstRelease.get_future().share();
    std::promise<void> secondStarted;
    std::promise<void> secondRelease;
    const std::shared_future secondReleased = secondRelease.get_future().share();
    auto queue = std::make_unique<WorkQueue<std::int32_t>>(8, Handling([&](const std::int32_t& item) {
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
                                                           }));
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

// A process can own what is bound to a thread (a COM apartment), so it is made and destroyed where the items run.
TEST(WorkQueue, MakesAndDestroysItsProcessOnItsOwnThread) {
    std::thread::id madeOn;
    std::thread::id processedOn;
    std::thread::id destroyedOn;
    std::promise<void> done;
    {
        WorkQueue<std::int32_t> queue(8, [&] {
            madeOn = std::this_thread::get_id();
            auto noter = std::make_shared<ThreadNoter>(&destroyedOn);
            return WorkQueue<std::int32_t>::Process([&processedOn, &done, noter](const std::int32_t&) {
                processedOn = std::this_thread::get_id();
                done.set_value();
            });
        });
        ASSERT_TRUE(queue.Push(1));
        done.get_future().wait();
    }
    EXPECT_NE(madeOn, std::this_thread::get_id());
    EXPECT_EQ(madeOn, processedOn);
    EXPECT_EQ(destroyedOn, processedOn);
}

// A worker that fails again while stopping must not replace the cause the UI thread ends the process with.
TEST(ThreadFailure, KeepsAndPostsOnlyTheFirstError) {
    const MessageWindow target;
    ThreadFailure failure(target.Get(), kPosted, "test");

    failure.Report(std::make_exception_ptr(std::runtime_error("first")));
    failure.Report(std::make_exception_ptr(std::runtime_error("second")));
    EXPECT_EQ(TakePosted(target.Get()), 1);
    EXPECT_EQ(RethrownMessage(failure), "first");
    EXPECT_NO_THROW(failure.RethrowReported());
}

// Any process can post the message, so one with nothing reported behind it returns instead of throwing.
TEST(ThreadFailure, AMessageWithNothingReportedDoesNotThrow) {
    const MessageWindow target;
    ThreadFailure failure(target.Get(), kPosted, "test");
    EXPECT_NO_THROW(failure.RethrowReported());
}

// A source changing faster than the UI thread reads must not fill the queue: WM_QUIT waits behind every posted message.
TEST(ChangeSignal, PostsOnceUntilAcknowledged) {
    ChangeTarget target(kPosted);

    for (std::int32_t change = 0; change < 1000; ++change) {
        target.signal.Notify("test");
    }
    EXPECT_EQ(TakePosted(target.window.Get()), 1);

    target.signal.Acknowledge();
    target.signal.Notify("test");
    target.signal.Notify("test");
    EXPECT_EQ(TakePosted(target.window.Get()), 1);
}
