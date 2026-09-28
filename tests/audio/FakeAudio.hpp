#pragma once

#include "audio/IAudio.hpp"

#include <optional>
#include <vector>

// An output device a test sets directly, recording the changes modules ask for.
class FakeAudio final : public IAudio {
  public:
    std::optional<AudioOutput> output;
    std::vector<float>         volumes;
    std::vector<bool>          mutes;

    std::optional<AudioOutput> Output() const override {
        return output;
    }
    void SetVolume(float volume) override {
        volumes.push_back(volume);
    }
    void SetMuted(bool muted) override {
        mutes.push_back(muted);
    }
};
