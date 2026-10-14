#pragma once

#include "audio/IAudio.hpp"
#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/IconTextModule.hpp"
#include "modules/TextLine.hpp"

#include <cstdint>
#include <optional>
#include <string>

// The default output or input device's volume through format, or format_muted and icon_muted in the module's muted
// colors while it is muted. A click toggles mute and a scroll changes the volume by step, unless the config binds those
// inputs to actions. Shows nothing while there is no device of its flow.
class Audio final : public IconTextModule<AudioConfig> {
  public:
    // Throws ConfigError when a format is not a valid spec for its values.
    Audio(AudioConfig config, IAudio& audio);

    void                        Click(D2D1_POINT_2F point) override;
    void                        Scroll(ScrollDirection direction) override;
    std::optional<std::wstring> TooltipText() override;

  private:
    ModuleText Content() override;

    IAudio&    _audio;
};

// The icon and text for device, empty while there is none.
ModuleText FormatAudio(const AudioConfig& config, const std::optional<AudioDevice>& device);

// The volume one scroll notch away: stepPercent up or down, landing on a whole percent within 0 to 1.
float SteppedVolume(float volume, std::int32_t stepPercent, ScrollDirection direction);
