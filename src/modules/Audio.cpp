#include "modules/Audio.hpp"

#include "platform/Text.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

Audio::Audio(AudioConfig config, IAudio& audio) : _config(std::move(config)), _audio(audio), _shown{.icon = {}, .text = {}}, _text(EmptyIconText()) {
    RequireFormats("audio", _config.format, _config.tooltip, std::int32_t{50}, std::string("Speakers"));
    RequireFormat("audio format_muted", _config.formatMuted, std::int32_t{50}, std::string("Speakers"));
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

void Audio::Click(D2D1_POINT_2F) {
    const std::optional<AudioOutput> output = _audio.Output();
    if (output.has_value()) {
        _audio.SetMuted(!output->muted);
    }
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
    return ToWide(FormatValue(*_config.tooltip, VolumePercent(output->volume), ToUtf8(output->device)));
}

std::int32_t VolumePercent(float volume) {
    return static_cast<std::int32_t>(std::lround(volume * 100.0f));
}

AudioText FormatAudio(const AudioConfig& config, const std::optional<AudioOutput>& output) {
    if (!output.has_value()) {
        return AudioText{.icon = {}, .text = {}};
    }
    const std::string& format = output->muted ? config.formatMuted : config.format;
    return AudioText{.icon = output->muted ? config.iconMuted : config.icon, .text = ToWide(FormatValue(format, VolumePercent(output->volume), ToUtf8(output->device)))};
}

float SteppedVolume(float volume, std::int32_t stepPercent, ScrollDirection direction) {
    const std::int32_t step    = direction == ScrollDirection::Up ? stepPercent : -stepPercent;
    const std::int32_t percent = std::clamp(VolumePercent(volume) + step, 0, 100);
    return static_cast<float>(percent) / 100.0f;
}
