#pragma once

#include "audio/IAudio.hpp"
#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/IconText.hpp"

#include <cstdint>
#include <optional>
#include <string>

// The default output device's volume through format, or format_muted and icon_muted in the module's .muted colors
// while it is muted. A click
// toggles mute and a scroll changes the volume by step, unless the config binds those inputs to actions. Shows
// nothing while there is no output device.
class Audio final : public IModule {
  public:
    // Throws ConfigError when a format is not a valid spec for its values.
    Audio(AudioConfig config, IAudio& audio);

    bool                        Layout(const RenderContext& render) override;
    D2D1_SIZE_F                 Size() const override;
    void                        Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    void                        Click(D2D1_POINT_2F point) override;
    void                        Scroll(ScrollDirection direction) override;
    const ModuleActions&        Actions() const override;
    std::optional<std::wstring> TooltipText() override;

  private:
    AudioConfig _config;
    IAudio&     _audio;
    TextLine    _line;
};

// The volume as the formats get it: a whole percent.
std::int32_t VolumePercent(float volume);

// The icon and text for output, empty while there is none.
ModuleText FormatAudio(const AudioConfig& config, const std::optional<AudioOutput>& output);

// The volume one scroll notch away: stepPercent up or down, landing on a whole percent within 0 to 1.
float SteppedVolume(float volume, std::int32_t stepPercent, ScrollDirection direction);
