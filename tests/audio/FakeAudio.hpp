#pragma once

#include "audio/IAudio.hpp"

#include <optional>
#include <vector>

// A default device a test sets directly, recording the changes modules ask for.
class FakeAudio final : public IAudio {
  public:
    std::optional<AudioDevice> device;
    std::vector<float>         volumes;
    std::vector<bool>          mutes;

    std::optional<AudioDevice> Device() const override {
        return device;
    }
    void SetVolume(float volume) override {
        volumes.push_back(volume);
    }
    void SetMuted(bool muted) override {
        mutes.push_back(muted);
    }
};
