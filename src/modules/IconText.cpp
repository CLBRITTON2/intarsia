#include "modules/IconText.hpp"

#include "platform/Errors.hpp"

#include <cstdint>
#include <limits>
#include <utility>

IconText LayOutIconText(const RenderContext& render, std::wstring_view icon, std::wstring_view text, ID2D1Brush* textBrush) {
    const std::wstring line = icon.empty() ? std::wstring(text) : std::wstring(icon) + L"  " + std::wstring(text);

    IconText           laidOut{.layout = nullptr, .size = {.width = 0.0f, .height = 0.0f}};
    ThrowIfFailed(render.dwrite->CreateTextLayout(line.c_str(), static_cast<std::uint32_t>(line.size()), render.textFormat, std::numeric_limits<float>::max(),
                                                  std::numeric_limits<float>::max(), &laidOut.layout),
                  "IDWriteFactory::CreateTextLayout");
    ThrowIfFailed(laidOut.layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP), "IDWriteTextLayout::SetWordWrapping");
    if (!icon.empty()) {
        const DWRITE_TEXT_RANGE iconRange{.startPosition = 0, .length = static_cast<std::uint32_t>(icon.size())};
        ThrowIfFailed(laidOut.layout->SetFontSize(render.iconSizeDips, iconRange), "IDWriteTextLayout::SetFontSize");
        ThrowIfFailed(laidOut.layout->SetDrawingEffect(render.accent, iconRange), "IDWriteTextLayout::SetDrawingEffect");
    }
    const DWRITE_TEXT_RANGE textRange{.startPosition = static_cast<std::uint32_t>(line.size() - text.size()), .length = static_cast<std::uint32_t>(text.size())};
    ThrowIfFailed(laidOut.layout->SetDrawingEffect(textBrush, textRange), "IDWriteTextLayout::SetDrawingEffect");

    DWRITE_TEXT_METRICS metrics{};
    ThrowIfFailed(laidOut.layout->GetMetrics(&metrics), "IDWriteTextLayout::GetMetrics");
    laidOut.size = D2D1_SIZE_F{.width = metrics.widthIncludingTrailingWhitespace, .height = metrics.height};
    return laidOut;
}

bool TextLine::Update(const RenderContext& render, std::wstring text, ID2D1Brush* brush) {
    if (_text.layout != nullptr && text == _shown) {
        return false;
    }
    _text  = LayOutIconText(render, L"", text, brush);
    _shown = std::move(text);
    return true;
}

D2D1_SIZE_F TextLine::Size() const {
    return _text.size;
}

void TextLine::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    render.context->DrawTextLayout(origin, _text.layout.Get(), render.foreground);
}
