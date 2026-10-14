#pragma once

#include "platform/Handle.hpp"
#include "platform/StoppableThread.hpp"

#include <windows.h>

#include <array>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

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
