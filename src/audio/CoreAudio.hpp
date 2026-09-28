#pragma once

#include "audio/IAudio.hpp"
#include "platform/ChangeSignal.hpp"
#include "platform/Handle.hpp"

#include <mutex>
#include <optional>
#include <thread>

// The default output device through Windows Core Audio (MMDevice and IAudioEndpointVolume). A worker thread owns every
// Core Audio object: it follows default device changes and volume notifications into a snapshot the UI thread reads,
// and applies queued changes. While the device cannot be read, as while the audio service restarts, it retries every
// 2 s and logs once per outage.
class CoreAudio final : public IAudio {
  public:
    explicit CoreAudio(ChangeSignal& changes);
    ~CoreAudio() override;
    CoreAudio(const CoreAudio&)                            = delete;
    CoreAudio&                 operator=(const CoreAudio&) = delete;

    std::optional<AudioOutput> Output() const override;
    void                       SetVolume(float volume) override;
    void                       SetMuted(bool muted) override;

    // Called from Core Audio's notification threads.
    void OnDefaultDeviceChanged();
    void OnVolumeNotified(float volume, bool muted);

  private:
    void                       Run();
    void                       Publish(std::optional<AudioOutput> output);

    ChangeSignal&              _changes;
    UniqueHandle               _stopEvent;
    UniqueHandle               _deviceEvent;  // the default device changed, so the worker attaches to it
    UniqueHandle               _requestEvent; // a change is waiting in _pendingVolume or _pendingMuted
    mutable std::mutex         _mutex;        // guards everything below
    std::optional<AudioOutput> _output;
    // Only the latest request of each kind matters, so a burst of scrolls lands as one change.
    std::optional<float> _pendingVolume;
    std::optional<bool>  _pendingMuted;
    // Declared last so every member above exists before the thread starts.
    std::thread _thread;
};
