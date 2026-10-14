#include "platform/StoppableThread.hpp"

#include <format>
#include <utility>

StoppableThread::StoppableThread(std::string_view name, std::function<void(HANDLE stopEvent)> body) :
    _stopEvent(ManualResetEvent(std::format("{} stop", name))), _thread([stopEvent = _stopEvent.get(), body = std::move(body)] { body(stopEvent); }) {}

StoppableThread::~StoppableThread() {
    SetEvent(_stopEvent.get());
    _thread.join();
}
