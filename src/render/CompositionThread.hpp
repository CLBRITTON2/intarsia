#pragma once

#include "platform/ComScope.hpp"

#include <wrl/client.h>

namespace ABI::Windows::System {
    struct IDispatcherQueueController;
} // namespace ABI::Windows::System

// The COM apartment and dispatcher queue a compositor needs on the thread that creates it, for as long as it lives.
// The queue runs inside the thread's own message loop.
class CompositionThread {
  public:
    CompositionThread();
    ~CompositionThread();
    CompositionThread(const CompositionThread&)            = delete;
    CompositionThread& operator=(const CompositionThread&) = delete;

  private:
    ComScope                                                                 _com;
    Microsoft::WRL::ComPtr<ABI::Windows::System::IDispatcherQueueController> _queue;
};
