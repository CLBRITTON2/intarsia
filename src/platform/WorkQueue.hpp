#pragma once

#include "platform/Handle.hpp"

#include <windows.h>

#include <array>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

// A bounded queue drained in order by its own thread, for work the UI thread must not wait on. process runs on that
// thread and must not throw: nothing there could catch it.
template <typename Item>
class WorkQueue {
  public:
    WorkQueue(size_t capacity, std::function<void(const Item&)> process) :
        _capacity(capacity), _process(std::move(process)), _stopEvent(OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(work queue stop)")),
        _queuedEvent(OwnHandle(CreateEventW(nullptr, FALSE, FALSE, nullptr), "CreateEventW(work queue queued)")), _thread([this] { Run(); }) {}

    // Stops after the item in progress: whatever is still queued is dropped.
    ~WorkQueue() {
        SetEvent(_stopEvent.get());
        _thread.join();
    }

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
    void Run() {
        const std::array<HANDLE, 2> waits{_stopEvent.get(), _queuedEvent.get()};
        while (WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
            while (WaitForSingleObject(_stopEvent.get(), 0) == WAIT_TIMEOUT) {
                std::optional<Item> item = Pop();
                if (!item.has_value()) {
                    break;
                }
                _process(*item);
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

    size_t                           _capacity;
    std::function<void(const Item&)> _process;
    UniqueHandle                     _stopEvent;
    UniqueHandle                     _queuedEvent;
    std::mutex                       _mutex;
    std::deque<Item>                 _queue;
    // Declared last so every member above exists before the thread starts.
    std::thread _thread;
};
