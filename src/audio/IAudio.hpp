#pragma once

#include <optional>
#include <string>

// The default output device as the audio module shows it.
struct AudioOutput {
    std::wstring device; // its name, as the system's sound settings show it
    float        volume; // 0 to 1
    bool         muted;

    bool         operator==(const AudioOutput&) const = default;
};

// The system's default output device, followed as it changes. Reads come from the UI thread, and changes are queued to
// the backend, so neither ever waits on the audio service.
class IAudio {
  public:
    virtual ~IAudio() = default;

    // The default output device, or nullopt while there is none or it cannot be read.
    virtual std::optional<AudioOutput> Output() const = 0;

    // Sets the default output's volume, 0 to 1. It lands after the call returns, and Output shows it once it has.
    virtual void SetVolume(float volume) = 0;

    virtual void SetMuted(bool muted) = 0;
};
