#pragma once

#include "audio/IAudio.hpp"
#include "platform/ChangeSignal.hpp"
#include "platform/Handle.hpp"
#include "platform/StoppableThread.hpp"

#include <mutex>
#include <optional>

// The default device of one flow through Windows Core Audio (MMDevice and IAudioEndpointVolume). A worker thread owns
// every Core Audio object: it follows default device changes and volume notifications into a snapshot the UI thread
// reads, and applies queued changes. While the device cannot be read, as while the audio service restarts, it retries
// every 2 s and logs once per outage.
class CoreAudio final : public IAudio {
  public:
    CoreAudio(ChangeSignal& changes, AudioFlow flow);
    CoreAudio(const CoreAudio&)                            = delete;
    CoreAudio&                 operator=(const CoreAudio&) = delete;

    std::optional<AudioDevice> Device() const override;
    void                       SetVolume(float volume) override;
    void                       SetMuted(bool muted) override;

    // Called from Core Audio's notification threads.
    void OnDefaultDeviceChanged();
    void OnVolumeNotified(float volume, bool muted);

  private:
    void                       Run(HANDLE stopEvent);
    void                       Publish(std::optional<AudioDevice> device);

    ChangeSignal&              _changes;
    AudioFlow                  _flow;
    UniqueHandle               _deviceEvent;  // the default device changed, so the worker attaches to it
    UniqueHandle               _requestEvent; // a change is waiting in _pendingVolume or _pendingMuted
    mutable std::mutex         _mutex;        // guards everything below
    std::optional<AudioDevice> _device;
    // Only the latest request of each kind matters, so a burst of scrolls lands as one change.
    std::optional<float> _pendingVolume;
    std::optional<bool>  _pendingMuted;
    // Declared last so every member above exists before the thread starts.
    StoppableThread _thread;
};
