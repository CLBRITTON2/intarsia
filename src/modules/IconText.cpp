#include "modules/IconText.hpp"

#include "platform/Errors.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace {

    ComPtr<IDWriteTextLayout> CreateLine(const RenderContext& render, std::wstring_view line) {
        ComPtr<IDWriteTextLayout> layout;
        ThrowIfFailed(render.dwrite->CreateTextLayout(line.data(), static_cast<std::uint32_t>(line.size()), render.textFormat, std::numeric_limits<float>::max(),
                                                      std::numeric_limits<float>::max(), &layout),
                      "IDWriteFactory::CreateTextLayout");
        ThrowIfFailed(layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP), "IDWriteTextLayout::SetWordWrapping");
        return layout;
    }

    // Down a vertical bar the text's lines are centered in the room across it and cut with an ellipsis where they pass it.
    ComPtr<IDWriteTextLayout> CreateText(const RenderContext& render, std::wstring_view text, float insetDips) {
        ComPtr<IDWriteTextLayout> layout = CreateLine(render, text);
        if (render.axis == Axis::Horizontal) {
            return layout;
        }
        ThrowIfFailed(layout->SetMaxWidth(std::max(render.boxCrossDips - (2.0f * insetDips), 0.0f)), "IDWriteTextLayout::SetMaxWidth");
        ThrowIfFailed(layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER), "IDWriteTextLayout::SetTextAlignment");
        ComPtr<IDWriteInlineObject> ellipsis;
        ThrowIfFailed(render.dwrite->CreateEllipsisTrimmingSign(render.textFormat, &ellipsis), "IDWriteFactory::CreateEllipsisTrimmingSign");
        const DWRITE_TRIMMING trimming{
            .granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER,
            .delimiter = 0,
            .delimiterCount = 0,
        };
        ThrowIfFailed(layout->SetTrimming(&trimming, ellipsis.Get()), "IDWriteTextLayout::SetTrimming");
        return layout;
    }

    // Measured last: every font change to a layout moves its size.
    DWRITE_TEXT_METRICS Metrics(IDWriteTextLayout* layout) {
        DWRITE_TEXT_METRICS metrics{};
        if (layout != nullptr) {
            ThrowIfFailed(layout->GetMetrics(&metrics), "IDWriteTextLayout::GetMetrics");
        }
        return metrics;
    }

    D2D1_SIZE_F SizeOf(const DWRITE_TEXT_METRICS& metrics) {
        return D2D1_SIZE_F{.width = metrics.widthIncludingTrailingWhitespace, .height = metrics.height};
    }

    // Overhangs are measured from the layout's max box, so the max height is first cut to the line's own height.
    float InkShift(IDWriteTextLayout* layout, D2D1_SIZE_F size) {
        if (layout == nullptr) {
            return 0.0f;
        }
        ThrowIfFailed(layout->SetMaxHeight(size.height), "IDWriteTextLayout::SetMaxHeight");
        DWRITE_OVERHANG_METRICS overhang{};
        ThrowIfFailed(layout->GetOverhangMetrics(&overhang), "IDWriteTextLayout::GetOverhangMetrics");
        const float inkTop = -overhang.top;
        const float inkBottom = size.height + overhang.bottom;
        // A glyph with no ink, such as a space, has nothing to center.
        if (inkBottom <= inkTop) {
            return 0.0f;
        }
        return (size.height / 2.0f) - ((inkTop + inkBottom) / 2.0f);
    }

    float GapDips(IDWriteTextLayout* icon, IDWriteTextLayout* text, float iconGapDips) {
        if (icon != nullptr && text != nullptr) {
            return iconGapDips;
        }
        return 0.0f;
    }

    D2D1_SIZE_F CombinedSize(Axis axis, D2D1_SIZE_F iconSize, float gapDips, D2D1_SIZE_F textSize) {
        if (axis == Axis::Horizontal) {
            return D2D1::SizeF(iconSize.width + gapDips + textSize.width, std::max(iconSize.height, textSize.height));
        }
        return D2D1::SizeF(std::max(iconSize.width, textSize.width), iconSize.height + gapDips + textSize.height);
    }

    IconText Combined(Axis axis, ComPtr<IDWriteTextLayout> icon, ComPtr<IDWriteTextLayout> text, float iconGapDips) {
        const D2D1_SIZE_F iconSize = SizeOf(Metrics(icon.Get()));
        const float inkShift = InkShift(icon.Get(), iconSize);
        const DWRITE_TEXT_METRICS textMetrics = Metrics(text.Get());
        const D2D1_SIZE_F textSize = SizeOf(textMetrics);
        const float gap = GapDips(icon.Get(), text.Get(), iconGapDips);
        const D2D1_SIZE_F size = CombinedSize(axis, iconSize, gap, textSize);
        return IconText{
            .icon = std::move(icon),
            .text = std::move(text),
            .axis = axis,
            .iconSize = iconSize,
            .iconInkShiftDips = inkShift,
            .textSize = textSize,
            .textLeftDips = textMetrics.left,
            .gapDips = gap,
            .size = size,
        };
    }

    void DrawBeside(const RenderContext& render, D2D1_POINT_2F origin, const IconText& laidOut, ID2D1Brush* iconBrush, ID2D1Brush* textBrush) {
        if (laidOut.icon != nullptr) {
            render.context->DrawTextLayout(D2D1::Point2F(origin.x, origin.y + ((laidOut.size.height - laidOut.iconSize.height) / 2.0f) + laidOut.iconInkShiftDips),
                                           laidOut.icon.Get(), iconBrush);
        }
        if (laidOut.text != nullptr) {
            const float left = origin.x + laidOut.iconSize.width + laidOut.gapDips;
            render.context->DrawTextLayout(D2D1::Point2F(left, origin.y + ((laidOut.size.height - laidOut.textSize.height) / 2.0f)), laidOut.text.Get(), textBrush);
        }
    }

    void DrawStacked(const RenderContext& render, D2D1_POINT_2F origin, const IconText& laidOut, ID2D1Brush* iconBrush, ID2D1Brush* textBrush) {
        if (laidOut.icon != nullptr) {
            render.context->DrawTextLayout(D2D1::Point2F(origin.x + ((laidOut.size.width - laidOut.iconSize.width) / 2.0f), origin.y + laidOut.iconInkShiftDips),
                                           laidOut.icon.Get(), iconBrush);
        }
        if (laidOut.text != nullptr) {
            const float top = origin.y + laidOut.iconSize.height + laidOut.gapDips;
            render.context->DrawTextLayout(D2D1::Point2F(origin.x + ((laidOut.size.width - laidOut.textSize.width) / 2.0f) - laidOut.textLeftDips, top), laidOut.text.Get(),
                                           textBrush);
        }
    }

} // namespace

IconText LayOutIconText(const RenderContext& render, std::wstring_view icon, std::wstring_view text) {
    ComPtr<IDWriteTextLayout> iconLayout;
    if (!icon.empty()) {
        iconLayout = CreateLine(render, icon);
        const DWRITE_TEXT_RANGE all{.startPosition = 0, .length = static_cast<std::uint32_t>(icon.size())};
        ThrowIfFailed(iconLayout->SetFontFamilyName(render.iconFont, all), "IDWriteTextLayout::SetFontFamilyName");
        ThrowIfFailed(iconLayout->SetFontSize(render.iconSizeDips, all), "IDWriteTextLayout::SetFontSize");
    }
    ComPtr<IDWriteTextLayout> textLayout;
    if (!text.empty()) {
        textLayout = CreateText(render, text, 0.0f);
    }
    return Combined(render.axis, std::move(iconLayout), std::move(textLayout), render.iconGapDips);
}

IconText LayOutWeightedText(const RenderContext& render, std::wstring_view text, DWRITE_FONT_WEIGHT weight, float insetDips) {
    ComPtr<IDWriteTextLayout> layout = CreateText(render, text, insetDips);
    ThrowIfFailed(layout->SetFontWeight(weight, DWRITE_TEXT_RANGE{.startPosition = 0, .length = static_cast<std::uint32_t>(text.size())}), "IDWriteTextLayout::SetFontWeight");
    return Combined(render.axis, nullptr, std::move(layout), 0.0f);
}

void DrawIconText(const RenderContext& render, D2D1_POINT_2F origin, const IconText& laidOut, ID2D1Brush* iconBrush, ID2D1Brush* textBrush) {
    if (laidOut.axis == Axis::Horizontal) {
        DrawBeside(render, origin, laidOut, iconBrush, textBrush);
    } else {
        DrawStacked(render, origin, laidOut, iconBrush, textBrush);
    }
}

void DrawTurnedIconText(const RenderContext& render, D2D1_POINT_2F origin, const IconText& laidOut, float degrees, ID2D1Brush* iconBrush, ID2D1Brush* textBrush) {
    const D2D1_POINT_2F center = D2D1::Point2F(origin.x + (laidOut.size.width / 2.0f), origin.y + (laidOut.size.height / 2.0f));
    D2D1_MATRIX_3X2_F transform{};
    render.context->GetTransform(&transform);
    render.context->SetTransform(D2D1::Matrix3x2F::Rotation(degrees, center) * *D2D1::Matrix3x2F::ReinterpretBaseType(&transform));
    DrawIconText(render, origin, laidOut, iconBrush, textBrush);
    render.context->SetTransform(transform);
}

IconText EmptyIconText() {
    return IconText{
        .icon = nullptr,
        .text = nullptr,
        .axis = Axis::Horizontal,
        .iconSize = {.width = 0.0f, .height = 0.0f},
        .iconInkShiftDips = 0.0f,
        .textSize = {.width = 0.0f, .height = 0.0f},
        .textLeftDips = 0.0f,
        .gapDips = 0.0f,
        .size = {.width = 0.0f, .height = 0.0f},
    };
}
