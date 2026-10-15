#include "modules/TextModule.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <utility>

const std::wstring& LevelIcon(const std::vector<std::wstring>& icons, std::int32_t percent) {
    const auto count = static_cast<std::int32_t>(icons.size());
    const auto index = std::clamp(std::clamp(percent, 0, 100) * count / 101, 0, count - 1);
    return icons[static_cast<size_t>(index)];
}

ModuleText NormalText(std::wstring icon, std::wstring text) {
    return ModuleText{
        .icon       = std::move(icon),
        .text       = std::move(text),
        .state      = TextState::Normal,
        .classIndex = 0,
    };
}

bool TextLine::Update(const RenderContext& render, ModuleText content) {
    if (_laidOut && content == _shown) {
        return false;
    }
    _text    = LayOutIconText(render, content.icon, content.text);
    _shown   = std::move(content);
    _laidOut = true;
    return true;
}

D2D1_SIZE_F TextLine::Size() const {
    return _text.size;
}

void TextLine::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    switch (_shown.state) {
        case TextState::Normal: DrawIconText(render, origin, _text, render.accent, render.foreground); return;
        case TextState::Alert: DrawIconText(render, origin, _text, render.accent, render.alert); return;
        case TextState::Warning: DrawIconText(render, origin, _text, render.warning.icon, render.warning.text); return;
        case TextState::Critical: DrawIconText(render, origin, _text, render.critical.icon, render.critical.text); return;
        case TextState::Muted: DrawIconText(render, origin, _text, render.muted.icon, render.muted.text); return;
        case TextState::Classed: {
            const StateBrushes& brushes = render.classes.at(_shown.classIndex);
            DrawIconText(render, origin, _text, brushes.icon, brushes.text);
            return;
        }
    }
    throw std::invalid_argument(std::format("unknown text state {}", static_cast<std::int32_t>(_shown.state)));
}

std::chrono::milliseconds TimeUntilNextTick(std::chrono::system_clock::time_point now, std::chrono::seconds interval) {
    const auto sinceEpoch = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    const auto period     = std::chrono::duration_cast<std::chrono::milliseconds>(interval);
    return period - (sinceEpoch % period);
}

bool SampleDue(std::chrono::steady_clock::time_point sampledAt, std::chrono::steady_clock::time_point now, std::chrono::seconds interval) {
    return now - sampledAt >= interval - std::chrono::milliseconds(50);
}

SampledTexts::SampledTexts(std::chrono::steady_clock::time_point sampledAt) : _sampledAt(sampledAt) {}

void SampledTexts::SampleIfDue(std::chrono::steady_clock::time_point now, std::chrono::seconds interval, const std::function<void()>& sample) {
    if (!SampleDue(_sampledAt, now, interval)) {
        return;
    }
    sample();
    _sampledAt = now;
    _texts.clear();
}

const std::wstring& SampledTexts::Of(const std::string& spec, const std::function<std::wstring()>& format) {
    const auto found = std::ranges::find(_texts, spec, &std::pair<std::string, std::wstring>::first);
    if (found != _texts.end()) {
        return found->second;
    }
    return _texts.emplace_back(spec, format()).second;
}

TextModule::TextModule(TextModuleConfig config, std::string_view type, const std::vector<Placeholder>& samples) : IconTextModule(std::move(config)) {
    RequirePlaceholders(std::format("{} format", type), Settings().format, samples);
    RequireOptionalPlaceholders(std::format("{} tooltip", type), Settings().tooltip, samples);
}

std::optional<std::uint32_t> TextModule::MillisecondsUntilChange() const {
    return static_cast<std::uint32_t>(TimeUntilNextTick(std::chrono::system_clock::now(), Settings().interval).count());
}

std::optional<std::wstring> TextModule::TooltipText() {
    if (!Settings().tooltip.has_value()) {
        return std::nullopt;
    }
    return Format(*Settings().tooltip).text;
}

ModuleText TextModule::Content() {
    return Format(Settings().format);
}
