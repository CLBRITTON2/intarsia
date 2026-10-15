#pragma once

#include "platform/Handle.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

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

// A bounded queue drained in order by its own thread, for work the UI thread must not wait on.
template <typename Item>
class WorkQueue {
  public:
    using Process = std::function<void(const Item&)>;

    // makeProcess runs first on the queue's thread, and the process it returns handles every item there and is destroyed
    // there, so it can own what has to live on that thread (a COM apartment). Neither may throw: nothing there could
    // catch it.
    WorkQueue(size_t capacity, std::function<Process()> makeProcess) :
        _capacity(capacity), _makeProcess(std::move(makeProcess)), _queuedEvent(AutoResetEvent("work queue queued")),
        _thread("work queue", [this](HANDLE stopEvent) { Run(stopEvent); }) {}

    WorkQueue(const WorkQueue&)            = delete;
    WorkQueue& operator=(const WorkQueue&) = delete;

    // False, dropping the item, when capacity items are already waiting.
    bool Push(Item item) {
        {
            const std::scoped_lock lock(_mutex);
            if (_queue.size() >= _capacity) {
                return false;
            }
            _queue.push_back(std::move(item));
        }
        SetEvent(_queuedEvent.get());
        return true;
    }

  private:
    void Run(HANDLE stopEvent) {
        const Process               process = _makeProcess();
        const std::array<HANDLE, 2> waits{stopEvent, _queuedEvent.get()};
        while (WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
            while (WaitForSingleObject(stopEvent, 0) == WAIT_TIMEOUT) {
                std::optional<Item> item = Pop();
                if (!item.has_value()) {
                    break;
                }
                process(*item);
            }
        }
    }

    std::optional<Item> Pop() {
        const std::scoped_lock lock(_mutex);
        if (_queue.empty()) {
            return std::nullopt;
        }
        Item item = std::move(_queue.front());
        _queue.pop_front();
        return item;
    }

    size_t                   _capacity;
    std::function<Process()> _makeProcess;
    UniqueHandle             _queuedEvent;
    std::mutex               _mutex;
    std::deque<Item>         _queue;
    // Declared last so every member above exists before the thread starts. Destruction stops after the item in
    // progress: whatever is still queued is dropped.
    StoppableThread _thread;
};

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

// Tells the UI thread that data a module shows has changed. Posts at most one message until the UI thread acknowledges
// it: a source that changes faster than the UI thread keeps up would otherwise fill its queue, and GetMessage returns
// a posted WM_QUIT only once no other posted message is waiting, so the bar could never quit.
class ChangeSignal {
  public:
    ChangeSignal(HWND target, std::uint32_t message);
    ChangeSignal(const ChangeSignal&)            = delete;
    ChangeSignal& operator=(const ChangeSignal&) = delete;

    // Any thread. component names the source in the log line a failed post writes.
    void Notify(std::string_view component);

    // The UI thread, as it handles the message and before it reads the sources, so a change made after that posts again.
    void Acknowledge();

  private:
    HWND              _target;
    std::uint32_t     _message;
    std::atomic<bool> _pending = false;
};

enum class ComThreading : std::uint8_t {
    // A single-threaded apartment, which the shell's objects need.
    Single,
    // The multithreaded apartment, so objects calling back on COM's own threads need no marshalling.
    Multi
};

// COM entered on the calling thread, with OLE1 DDE off, for as long as it lives. Every COM object the thread holds is
// released before it goes. name labels the error a failed entry throws, such as one into the other apartment.
class ComScope {
  public:
    ComScope(ComThreading threading, std::string_view name);
    ~ComScope();
    ComScope(const ComScope&)            = delete;
    ComScope& operator=(const ComScope&) = delete;
};
