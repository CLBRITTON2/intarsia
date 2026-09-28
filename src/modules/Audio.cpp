#include "modules/Audio.hpp"

#include "modules/Placeholders.hpp"
#include "platform/Text.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {

    std::vector<Placeholder> AudioPlaceholders(const AudioOutput& output) {
        return {{.name = "volume", .value = std::int64_t{VolumePercent(output.volume)}}, {.name = "device", .value = ToUtf8(output.device)}};
    }

} // namespace

Audio::Audio(AudioConfig config, IAudio& audio) : _config(std::move(config)), _audio(audio), _shown{.icon = {}, .text = {}}, _text(EmptyIconText()) {
    const std::vector<Placeholder> samples = AudioPlaceholders(AudioOutput{.device = L"Speakers", .volume = 0.5f, .muted = false});
    RequirePlaceholders("audio format", _config.format, samples);
    RequirePlaceholders("audio format_muted", _config.formatMuted, samples);
    if (_config.tooltip.has_value()) {
        RequirePlaceholders("audio tooltip", *_config.tooltip, samples);
    }
}

bool Audio::Layout(const RenderContext& render) {
    AudioText shown = FormatAudio(_config, _audio.Output());
    if (_laidOut && shown == _shown) {
        return false;
    }
    _text    = shown.text.empty() && shown.icon.empty() ? EmptyIconText() : LayOutIconText(render, shown.icon, shown.text);
    _shown   = std::move(shown);
    _laidOut = true;
    return true;
}

D2D1_SIZE_F Audio::Size() const {
    return _text.size;
}

void Audio::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    DrawIconText(render, origin, _text, render.foreground);
}

std::optional<std::uint32_t> Audio::MillisecondsUntilChange() const {
    return std::nullopt;
}

void                       Audio::RightClick(D2D1_POINT_2F) {}

std::optional<D2D1_RECT_F> Audio::TooltipItem() const {
    return std::nullopt;
}

void Audio::Click(D2D1_POINT_2F) {
    const std::optional<AudioOutput> output = _audio.Output();
    if (output.has_value()) {
        _audio.SetMuted(!output->muted);
    }
}

bool Audio::Hover(std::optional<D2D1_POINT_2F>) {
    return false;
}

void Audio::Scroll(ScrollDirection direction) {
    const std::optional<AudioOutput> output = _audio.Output();
    if (output.has_value()) {
        _audio.SetVolume(SteppedVolume(output->volume, _config.stepPercent, direction));
    }
}

const ModuleActions& Audio::Actions() const {
    return _config.actions;
}

std::optional<std::wstring> Audio::TooltipText() {
    const std::optional<AudioOutput> output = _audio.Output();
    if (!_config.tooltip.has_value() || !output.has_value()) {
        return std::nullopt;
    }
    return ToWide(FormatPlaceholders(*_config.tooltip, AudioPlaceholders(*output)));
}

std::int32_t VolumePercent(float volume) {
    return static_cast<std::int32_t>(std::lround(volume * 100.0f));
}

AudioText FormatAudio(const AudioConfig& config, const std::optional<AudioOutput>& output) {
    if (!output.has_value()) {
        return AudioText{.icon = {}, .text = {}};
    }
    const std::string& format = output->muted ? config.formatMuted : config.format;
    return AudioText{.icon = output->muted ? config.iconMuted : config.icon, .text = ToWide(FormatPlaceholders(format, AudioPlaceholders(*output)))};
}

float SteppedVolume(float volume, std::int32_t stepPercent, ScrollDirection direction) {
    const std::int32_t step    = direction == ScrollDirection::Up ? stepPercent : -stepPercent;
    const std::int32_t percent = std::clamp(VolumePercent(volume) + step, 0, 100);
    return static_cast<float>(percent) / 100.0f;
}
