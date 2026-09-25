#include "modules/IconText.hpp"

#include "platform/Errors.hpp"

#include <limits>

IconText LayOutIconText(const RenderContext& render, std::wstring_view icon, std::wstring_view text) {
    const std::wstring line = icon.empty() ? std::wstring(text) : std::wstring(icon) + L"  " + std::wstring(text);

    IconText           laidOut{.layout = nullptr, .size = {.width = 0.0f, .height = 0.0f}};
    ThrowIfFailed(render.dwrite->CreateTextLayout(line.c_str(), static_cast<UINT32>(line.size()), render.textFormat, std::numeric_limits<float>::max(),
                                                  std::numeric_limits<float>::max(), &laidOut.layout),
                  "IDWriteFactory::CreateTextLayout");
    ThrowIfFailed(laidOut.layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP), "IDWriteTextLayout::SetWordWrapping");
    if (!icon.empty()) {
        const DWRITE_TEXT_RANGE iconRange{.startPosition = 0, .length = static_cast<UINT32>(icon.size())};
        ThrowIfFailed(laidOut.layout->SetFontSize(render.iconSizeDips, iconRange), "IDWriteTextLayout::SetFontSize");
        ThrowIfFailed(laidOut.layout->SetDrawingEffect(render.accent, iconRange), "IDWriteTextLayout::SetDrawingEffect");
    }

    DWRITE_TEXT_METRICS metrics{};
    ThrowIfFailed(laidOut.layout->GetMetrics(&metrics), "IDWriteTextLayout::GetMetrics");
    laidOut.size = D2D1_SIZE_F{.width = metrics.widthIncludingTrailingWhitespace, .height = metrics.height};
    return laidOut;
}
