#include "platform/ThreadFailure.hpp"
#include "support/MessageWindow.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>

namespace {

    constexpr std::uint32_t kFailed = WM_APP + 7;

    std::int32_t            QueuedFailures(HWND target) {
        std::int32_t count   = 0;
        MSG          message = {};
        while (PeekMessageW(&message, target, kFailed, kFailed, PM_REMOVE)) {
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

// A worker that fails again while stopping must not replace the cause the UI thread ends the process with.
TEST(ThreadFailure, KeepsAndPostsOnlyTheFirstError) {
    const MessageWindow target;
    ThreadFailure       failure(target.Get(), kFailed, "test");

    failure.Report(std::make_exception_ptr(std::runtime_error("first")));
    failure.Report(std::make_exception_ptr(std::runtime_error("second")));
    EXPECT_EQ(QueuedFailures(target.Get()), 1);
    EXPECT_EQ(RethrownMessage(failure), "first");
}

// Any process can post the message, so one with nothing reported behind it returns instead of throwing.
TEST(ThreadFailure, AMessageWithNothingReportedDoesNotThrow) {
    const MessageWindow target;
    ThreadFailure       failure(target.Get(), kFailed, "test");

    EXPECT_NO_THROW(failure.RethrowReported());
    failure.Report(std::make_exception_ptr(std::runtime_error("first")));
    EXPECT_EQ(RethrownMessage(failure), "first");
    EXPECT_NO_THROW(failure.RethrowReported());
}
