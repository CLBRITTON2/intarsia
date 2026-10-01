#include "bar/Box.hpp"

#include "platform/Errors.hpp"

#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ranges>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

    // Each corner's horizontal (width) and vertical (height) radius.
    struct CornerRadii {
        D2D1_SIZE_F topLeft;
        D2D1_SIZE_F topRight;
        D2D1_SIZE_F bottomRight;
        D2D1_SIZE_F bottomLeft;
    };

    struct Figure {
        D2D1_RECT_F rect;
        CornerRadii radii;
    };

    // One ring of an inset shadow: the shadow's edge moved inward by inset, filled at alpha.
    struct ShadowRing {
        float inset;
        float alpha;
    };

    bool IsEmpty(const D2D1_RECT_F& rect) {
        return rect.right <= rect.left || rect.bottom <= rect.top;
    }

    bool SameColor(const D2D1_COLOR_F& first, const D2D1_COLOR_F& second) {
        return first.r == second.r && first.g == second.g && first.b == second.b && first.a == second.a;
    }

    bool IsDrawn(const BorderSide& side) {
        return side.widthDips > 0.0f && side.color.a > 0.0f;
    }

    D2D1_SIZE_F Shrink(const D2D1_SIZE_F& radius, float width, float height) {
        return D2D1::SizeF(std::max(radius.width - width, 0.0f), std::max(radius.height - height, 0.0f));
    }

    CornerRadii ShrinkAll(const CornerRadii& radii, float by) {
        return CornerRadii{.topLeft     = Shrink(radii.topLeft, by, by),
                           .topRight    = Shrink(radii.topRight, by, by),
                           .bottomRight = Shrink(radii.bottomRight, by, by),
                           .bottomLeft  = Shrink(radii.bottomLeft, by, by)};
    }

    void AddCorner(ID2D1GeometrySink* sink, const D2D1_POINT_2F& end, const D2D1_SIZE_F& radius) {
        if (radius.width > 0.0f && radius.height > 0.0f) {
            sink->AddArc(D2D1::ArcSegment(end, radius, 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        } else {
            sink->AddLine(end);
        }
    }

    void AddRoundedFigure(ID2D1GeometrySink* sink, const Figure& figure) {
        const D2D1_RECT_F& rect  = figure.rect;
        const CornerRadii& radii = figure.radii;
        sink->BeginFigure(D2D1::Point2F(rect.left + radii.topLeft.width, rect.top), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(rect.right - radii.topRight.width, rect.top));
        AddCorner(sink, D2D1::Point2F(rect.right, rect.top + radii.topRight.height), radii.topRight);
        sink->AddLine(D2D1::Point2F(rect.right, rect.bottom - radii.bottomRight.height));
        AddCorner(sink, D2D1::Point2F(rect.right - radii.bottomRight.width, rect.bottom), radii.bottomRight);
        sink->AddLine(D2D1::Point2F(rect.left + radii.bottomLeft.width, rect.bottom));
        AddCorner(sink, D2D1::Point2F(rect.left, rect.bottom - radii.bottomLeft.height), radii.bottomLeft);
        sink->AddLine(D2D1::Point2F(rect.left, rect.top + radii.topLeft.height));
        AddCorner(sink, D2D1::Point2F(rect.left + radii.topLeft.width, rect.top), radii.topLeft);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }

    ComPtr<ID2D1PathGeometry> OpenPath(ID2D1Factory* factory, ComPtr<ID2D1GeometrySink>& sink) {
        ComPtr<ID2D1PathGeometry> path;
        ThrowIfFailed(factory->CreatePathGeometry(&path), "ID2D1Factory::CreatePathGeometry");
        ThrowIfFailed(path->Open(&sink), "ID2D1PathGeometry::Open");
        return path;
    }

    // Filled even-odd, so a figure inside another cuts a hole in it.
    ComPtr<ID2D1PathGeometry> CreatePath(ID2D1Factory* factory, const std::vector<Figure>& figures) {
        ComPtr<ID2D1GeometrySink>       sink;
        const ComPtr<ID2D1PathGeometry> path = OpenPath(factory, sink);
        sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
        for (const Figure& figure : figures) {
            AddRoundedFigure(sink.Get(), figure);
        }
        ThrowIfFailed(sink->Close(), "ID2D1GeometrySink::Close");
        return path;
    }

    ComPtr<ID2D1PathGeometry> CreatePolygon(ID2D1Factory* factory, const std::array<D2D1_POINT_2F, 4>& points) {
        ComPtr<ID2D1GeometrySink>       sink;
        const ComPtr<ID2D1PathGeometry> path = OpenPath(factory, sink);
        sink->BeginFigure(points[0], D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLines(&points[1], static_cast<UINT32>(points.size() - 1));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        ThrowIfFailed(sink->Close(), "ID2D1GeometrySink::Close");
        return path;
    }

    ComPtr<ID2D1Factory> FactoryOf(ID2D1DeviceContext* context) {
        ComPtr<ID2D1Factory> factory;
        context->GetFactory(&factory);
        return factory;
    }

    // Stacked rings whose coverage follows CSS's Gaussian blur of the shadow's edge: the edge's sample at inset t
    // weighs the Gaussian at t, and each ring's alpha makes the rings under any point add up to the color's alpha
    // times the weight of the samples whose edge has passed it.
    std::vector<ShadowRing> Rings(const InsetShadow& shadow) {
        const float alpha = shadow.color.a;
        if (shadow.blurDips <= 0.0f) {
            return {ShadowRing{.inset = 0.0f, .alpha = alpha}};
        }
        const float        sigma = shadow.blurDips / 2.0f; // CSS's blur radius is twice the deviation
        const size_t       count = std::clamp(static_cast<size_t>(std::ceil(3.0f * shadow.blurDips)), size_t{2}, size_t{16});
        std::vector<float> insets;
        std::vector<float> weights;
        float              total = 0.0f;
        for (size_t sample = 0; sample < count; ++sample) {
            const float inset = (3.0f * sigma) - (6.0f * sigma * (static_cast<float>(sample) + 0.5f) / static_cast<float>(count));
            insets.push_back(inset);
            weights.push_back(std::exp(-(inset * inset) / (2.0f * sigma * sigma)));
            total += weights.back();
        }
        std::vector<ShadowRing> rings;
        float                   covered = 0.0f;
        for (size_t sample = 0; sample < count; ++sample) {
            const float before = covered;
            covered += weights[sample] / total;
            rings.push_back(ShadowRing{.inset = insets[sample], .alpha = 1.0f - ((1.0f - (alpha * covered)) / (1.0f - (alpha * before)))});
        }
        return rings;
    }

    D2D1_RECT_F Union(const D2D1_RECT_F& first, const D2D1_RECT_F& second) {
        return D2D1::RectF(std::min(first.left, second.left), std::min(first.top, second.top), std::max(first.right, second.right), std::max(first.bottom, second.bottom));
    }

    // Clipped to the box inside its border, as CSS clips an inset shadow, and painted last first, so the first is on top.
    void DrawShadows(ID2D1DeviceContext* context, BrushCache& brushes, ID2D1Factory* factory, const Figure& padding, const std::vector<InsetShadow>& shadows) {
        const ComPtr<ID2D1PathGeometry> clip = CreatePath(factory, {padding});
        context->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), clip.Get()), nullptr);
        for (const InsetShadow& shadow : shadows | std::views::reverse) {
            for (const ShadowRing& ring : Rings(shadow)) {
                const float       by   = shadow.spreadDips + ring.inset;
                const D2D1_RECT_F hole = D2D1::RectF(padding.rect.left + shadow.xDips + by, padding.rect.top + shadow.yDips + by, padding.rect.right + shadow.xDips - by,
                                                     padding.rect.bottom + shadow.yDips - by);
                // The hole may sit partly outside the box, and even-odd filling cuts it only from a figure around it.
                const std::vector<Figure> figures = IsEmpty(hole) ?
                    std::vector<Figure>{Figure{.rect = padding.rect, .radii = {}}} :
                    std::vector<Figure>{Figure{.rect = Union(padding.rect, hole), .radii = {}}, Figure{.rect = hole, .radii = ShrinkAll(padding.radii, by)}};
                const D2D1_COLOR_F        color{.r = shadow.color.r, .g = shadow.color.g, .b = shadow.color.b, .a = ring.alpha};
                context->FillGeometry(CreatePath(factory, figures).Get(), brushes.Get(color));
            }
        }
        context->PopLayer();
    }

    // From a side's outer corners to its inner ones, where CSS splits a corner between two sides' colors. The outer
    // corners sit a DIP out so the mask never softens the ring's own outer edge.
    std::array<D2D1_POINT_2F, 4> Wedge(const D2D1_RECT_F& box, const D2D1_RECT_F& inner, size_t side) {
        const D2D1_RECT_F outer = D2D1::RectF(box.left - 1.0f, box.top - 1.0f, box.right + 1.0f, box.bottom + 1.0f);
        switch (side) {
            case 0:
                return {D2D1::Point2F(outer.left, outer.top), D2D1::Point2F(outer.right, outer.top), D2D1::Point2F(inner.right, inner.top), D2D1::Point2F(inner.left, inner.top)};
            case 1:
                return {D2D1::Point2F(outer.right, outer.top), D2D1::Point2F(outer.right, outer.bottom), D2D1::Point2F(inner.right, inner.bottom),
                        D2D1::Point2F(inner.right, inner.top)};
            case 2:
                return {D2D1::Point2F(outer.right, outer.bottom), D2D1::Point2F(outer.left, outer.bottom), D2D1::Point2F(inner.left, inner.bottom),
                        D2D1::Point2F(inner.right, inner.bottom)};
            default:
                return {D2D1::Point2F(outer.left, outer.bottom), D2D1::Point2F(outer.left, outer.top), D2D1::Point2F(inner.left, inner.top),
                        D2D1::Point2F(inner.left, inner.bottom)};
        }
    }

    void DrawBorder(ID2D1DeviceContext* context, BrushCache& brushes, const Figure& outer, const Figure& inner, const Border& border) {
        const std::array<BorderSide, 4> sides{border.top, border.right, border.bottom, border.left};
        if (std::ranges::none_of(sides, IsDrawn)) {
            return;
        }
        const bool uniform =
            std::ranges::all_of(sides, [&border](const BorderSide& side) { return side.widthDips == border.top.widthDips && SameColor(side.color, border.top.color); });
        if (uniform) {
            // A stroke is centered on its outline, so the outline is inset by half the width to keep it inside.
            const float       inset  = border.top.widthDips / 2.0f;
            const D2D1_RECT_F edge   = D2D1::RectF(outer.rect.left + inset, outer.rect.top + inset, outer.rect.right - inset, outer.rect.bottom - inset);
            const float       radius = std::max(outer.radii.topLeft.width - inset, 0.0f);
            context->DrawRoundedRectangle(D2D1::RoundedRect(edge, radius, radius), brushes.Get(border.top.color), border.top.widthDips);
            return;
        }
        const ComPtr<ID2D1Factory>      factory = FactoryOf(context);
        const ComPtr<ID2D1PathGeometry> ring    = CreatePath(factory.Get(), IsEmpty(inner.rect) ? std::vector<Figure>{outer} : std::vector<Figure>{outer, inner});
        auto                            widened = sides | std::views::filter([](const BorderSide& side) { return side.widthDips > 0.0f; });
        const BorderSide&               first   = *widened.begin();
        if (std::ranges::all_of(widened, [&first](const BorderSide& side) { return SameColor(side.color, first.color); })) {
            context->FillGeometry(ring.Get(), brushes.Get(first.color));
            return;
        }
        for (size_t side = 0; side < sides.size(); ++side) {
            if (!IsDrawn(sides.at(side))) {
                continue;
            }
            const ComPtr<ID2D1PathGeometry> wedge = CreatePolygon(factory.Get(), Wedge(outer.rect, inner.rect, side));
            context->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), wedge.Get()), nullptr);
            context->FillGeometry(ring.Get(), brushes.Get(sides.at(side).color));
            context->PopLayer();
        }
    }

} // namespace

Insets ContentInsets(const BoxStyle& style) {
    return Insets{.left   = style.padding.xDips + style.border.left.widthDips,
                  .top    = style.padding.yDips + style.border.top.widthDips,
                  .right  = style.padding.xDips + style.border.right.widthDips,
                  .bottom = style.padding.yDips + style.border.bottom.widthDips};
}

void DrawBox(ID2D1DeviceContext* context, BrushCache& brushes, const D2D1_RECT_F& box, const BoxStyle& style, const D2D1_COLOR_F& fill) {
    // An empty module's box has no length, and its inset border would draw inside out.
    if (IsEmpty(box)) {
        return;
    }
    const float radius = std::min(style.radiusDips, std::min(box.right - box.left, box.bottom - box.top) / 2.0f);
    if (fill.a > 0.0f) {
        context->FillRoundedRectangle(D2D1::RoundedRect(box, radius, radius), brushes.Get(fill));
    }
    const Border&     border = style.border;
    const D2D1_SIZE_F corner = D2D1::SizeF(radius, radius);
    const Figure      outer{.rect = box, .radii = {.topLeft = corner, .topRight = corner, .bottomRight = corner, .bottomLeft = corner}};
    // CSS's inner edge: each corner's radius less the widths of the two sides that meet there.
    const Figure inner{.rect =
                           D2D1::RectF(box.left + border.left.widthDips, box.top + border.top.widthDips, box.right - border.right.widthDips, box.bottom - border.bottom.widthDips),
                       .radii = {.topLeft     = Shrink(corner, border.left.widthDips, border.top.widthDips),
                                 .topRight    = Shrink(corner, border.right.widthDips, border.top.widthDips),
                                 .bottomRight = Shrink(corner, border.right.widthDips, border.bottom.widthDips),
                                 .bottomLeft  = Shrink(corner, border.left.widthDips, border.bottom.widthDips)}};
    if (!style.shadows.empty() && !IsEmpty(inner.rect)) {
        DrawShadows(context, brushes, FactoryOf(context).Get(), inner, style.shadows);
    }
    DrawBorder(context, brushes, outer, inner, border);
}
