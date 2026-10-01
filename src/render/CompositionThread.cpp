#include "render/CompositionThread.hpp"

#include "platform/Errors.hpp"

#include <DispatcherQueue.h>
#include <unknwn.h>
#include <winrt/base.h>

CompositionThread::CompositionThread() : _com(ComThreading::Single, "composition") {
    const DispatcherQueueOptions options{.dwSize = sizeof(DispatcherQueueOptions), .threadType = DQTYPE_THREAD_CURRENT, .apartmentType = DQTAT_COM_NONE};
    ThrowIfFailed(CreateDispatcherQueueController(options, &_queue), "CreateDispatcherQueueController");
}

// C++/WinRT caches activation factories that belong to this apartment, and the next apartment the thread enters
// fails or crashes on them once this one ends.
CompositionThread::~CompositionThread() {
    _queue.Reset();
    winrt::clear_factory_cache();
}
