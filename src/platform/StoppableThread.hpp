#pragma once

#include "platform/Handle.hpp"

#include <windows.h>

#include <functional>
#include <string_view>
#include <thread>

// A thread and the manual reset event that asks it to stop. The thread starts at construction, and destruction sets
// the event and joins, so an owner declares it after every member its body uses.
class StoppableThread {
  public:
    // body runs on the thread with the stop event and must not throw: nothing there could catch it. name labels the
    // event in the error a failed creation throws.
    StoppableThread(std::string_view name, std::function<void(HANDLE stopEvent)> body);
    ~StoppableThread();
    StoppableThread(const StoppableThread&)            = delete;
    StoppableThread& operator=(const StoppableThread&) = delete;

  private:
    UniqueHandle _stopEvent;
    std::thread  _thread;
};
