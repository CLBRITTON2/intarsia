#pragma once

#include <windows.h>

#include <cstdint>
#include <exception>
#include <mutex>
#include <string>

// Hands the error that stopped a worker thread to the UI thread: the worker reports it, which posts message to target,
// and the UI thread rethrows it as it handles the message.
class ThreadFailure {
  public:
    // component names the worker in the log lines this writes.
    ThreadFailure(HWND target, std::uint32_t message, std::string component);
    ThreadFailure(const ThreadFailure&)            = delete;
    ThreadFailure& operator=(const ThreadFailure&) = delete;

    // Any thread. Only the first error is kept and posted.
    void Report(const std::exception_ptr& error);

    // The UI thread, on message. Any process on the desktop can post that message, so one with nothing reported behind
    // it is logged and ignored.
    void RethrowReported();

  private:
    HWND               _target;
    std::uint32_t      _message;
    std::string        _component;
    std::mutex         _mutex; // guards _error
    std::exception_ptr _error;
};
