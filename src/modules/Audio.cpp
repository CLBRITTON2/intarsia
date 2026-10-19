#include "modules/Audio.hpp"

#include "modules/Placeholders.hpp"
#include "platform/Text.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {

    // The volume as the formats get it: a whole percent.
    std::int32_t VolumePercent(float volume) {
        return static_cast<std::int32_t>(std::lround(volume * 100.0f));
    }

    // A whole percent as a volume, within 0 to 1.
    float PercentVolume(std::int32_t percent) {
        return static_cast<float>(std::clamp(percent, 0, 100)) / 100.0f;
    }

    std::vector<Placeholder> AudioPlaceholders(const AudioDevice& device) {
        return {{.name = "volume", .value = std::int64_t{VolumePercent(device.volume)}}, {.name = "device", .value = ToUtf8(device.name)}};
    }

} // namespace

Audio::Audio(AudioConfig config, IAudio& audio) : IconTextModule(std::move(config)), _audio(audio) {
    const std::vector<Placeholder> samples = AudioPlaceholders(AudioDevice{
        .name = L"Speakers",
        .volume = 0.5f,
        .muted = false,
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
    return FormatPlaceholders(*Settings().tooltip, AudioPlaceholders(*device));
}

ModuleText FormatAudio(const AudioConfig& config, const std::optional<AudioDevice>& device) {
    if (!device.has_value()) {
        return NormalText(L"", L"");
    }
    if (device->muted) {
        return ModuleText{
            .icon = config.iconMuted,
            .text = FormatPlaceholders(config.formatMuted, AudioPlaceholders(*device)),
            .state = TextState::Muted,
            .classIndex = 0,
        };
    }
    return NormalText(config.icon, FormatPlaceholders(config.format, AudioPlaceholders(*device)));
}

float SteppedVolume(float volume, std::int32_t stepPercent, ScrollDirection direction) {
    if (direction == ScrollDirection::Up) {
        return PercentVolume(VolumePercent(volume) + stepPercent);
    }
    return PercentVolume(VolumePercent(volume) - stepPercent);
}
