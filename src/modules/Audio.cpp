#include "modules/Audio.hpp"

#include "modules/Placeholders.hpp"
#include "platform/Text.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {

    std::vector<Placeholder> AudioPlaceholders(const AudioDevice& device) {
        return {{.name = "volume", .value = std::int64_t{VolumePercent(device.volume)}}, {.name = "device", .value = ToUtf8(device.name)}};
    }

} // namespace

Audio::Audio(AudioConfig config, IAudio& audio) : IconTextModule(std::move(config)), _audio(audio) {
    const std::vector<Placeholder> samples = AudioPlaceholders(AudioDevice{
        .name   = L"Speakers",
        .volume = 0.5f,
        .muted  = false,
    });
    RequirePlaceholders("audio format", Settings().format, samples);
    RequirePlaceholders("audio format_muted", Settings().formatMuted, samples);
    RequireOptionalPlaceholders("audio tooltip", Settings().tooltip, samples);
}

ModuleText Audio::Content() {
    return FormatAudio(Settings(), _audio.Device());
}

void Audio::Click(D2D1_POINT_2F) {
    const std::optional<AudioDevice> device = _audio.Device();
    if (device.has_value()) {
        _audio.SetMuted(!device->muted);
    }
}

void Audio::Scroll(ScrollDirection direction) {
    const std::optional<AudioDevice> device = _audio.Device();
    if (device.has_value()) {
        _audio.SetVolume(SteppedVolume(device->volume, Settings().stepPercent, direction));
    }
}

std::optional<std::wstring> Audio::TooltipText() {
    const std::optional<AudioDevice> device = _audio.Device();
    if (!Settings().tooltip.has_value() || !device.has_value()) {
        return std::nullopt;
    }
    return ToWide(FormatPlaceholders(*Settings().tooltip, AudioPlaceholders(*device)));
}

std::int32_t VolumePercent(float volume) {
    return static_cast<std::int32_t>(std::lround(volume * 100.0f));
}

ModuleText FormatAudio(const AudioConfig& config, const std::optional<AudioDevice>& device) {
    if (!device.has_value()) {
        return ModuleText{
            .icon       = {},
            .text       = {},
            .state      = TextState::Normal,
            .classIndex = 0,
        };
    }
    const std::string& format = device->muted ? config.formatMuted : config.format;
    return ModuleText{
        .icon       = device->muted ? config.iconMuted : config.icon,
        .text       = ToWide(FormatPlaceholders(format, AudioPlaceholders(*device))),
        .state      = device->muted ? TextState::Muted : TextState::Normal,
        .classIndex = 0,
    };
}

float SteppedVolume(float volume, std::int32_t stepPercent, ScrollDirection direction) {
    const std::int32_t step    = direction == ScrollDirection::Up ? stepPercent : -stepPercent;
    const std::int32_t percent = std::clamp(VolumePercent(volume) + step, 0, 100);
    return static_cast<float>(percent) / 100.0f;
}
