#pragma once

#include <cstdint>
#include <optional>
#include <string>

// Which default device an audio source follows: the one sound plays through, or the one it records from.
enum class AudioFlow : std::uint8_t {
    Output,
    Input
};

// The default device of one flow as the audio module shows it.
struct AudioDevice {
    std::wstring name; // as the system's sound settings show it
    float volume;      // 0 to 1
    bool muted;

    bool operator==(const AudioDevice&) const = default;
};

// The system's default device of one flow, followed as it changes. Reads come from the UI thread, and changes are
// queued to the backend, so neither ever waits on the audio service.
class IAudio {
  public:
    virtual ~IAudio() = default;

    // The default device, or nullopt while there is none or it cannot be read.
    virtual std::optional<AudioDevice> Device() const = 0;

    // Sets the default device's volume, 0 to 1. It lands after the call returns, and Device shows it once it has.
    virtual void SetVolume(float volume) = 0;

    virtual void SetMuted(bool muted) = 0;
};
