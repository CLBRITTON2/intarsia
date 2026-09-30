#include "platform/StoppableThread.hpp"

#include <format>
#include <utility>

StoppableThread::StoppableThread(std::string_view name, std::function<void(HANDLE stopEvent)> body) :
    _stopEvent(OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), std::format("CreateEventW({} stop)", name))),
    _thread([stopEvent = _stopEvent.get(), body = std::move(body)] { body(stopEvent); }) {}

StoppableThread::~StoppableThread() {
    SetEvent(_stopEvent.get());
    _thread.join();
}
