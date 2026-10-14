#include "modules/TextLine.hpp"

#include <algorithm>
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
