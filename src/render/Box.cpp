#include "render/Box.hpp"

#include "platform/Errors.hpp"
#include "render/Graphics.hpp"

#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ranges>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

    float SnapCoordinate(float pixelsPerDip, float offset, float dips) {
        return (std::round((dips + offset) * pixelsPerDip) / pixelsPerDip) - offset;
    }

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

    bool IsDrawn(const BorderSide& side) {
        return side.widthDips > 0.0f && side.color.a > 0.0f;
    }

    D2D1_SIZE_F Shrink(const D2D1_SIZE_F& radius, float width, float height) {
        return D2D1::SizeF(std::max(radius.width - width, 0.0f), std::max(radius.height - height, 0.0f));
    }

    CornerRadii ShrinkAll(const CornerRadii& radii, float by) {
        return CornerRadii{
            .topLeft     = Shrink(radii.topLeft, by, by),
            .topRight    = Shrink(radii.topRight, by, by),
            .bottomRight = Shrink(radii.bottomRight, by, by),
            .bottomLeft  = Shrink(radii.bottomLeft, by, by),
        };
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

    // One shadow ring's figures: the padding box with the hole, moved in by by, cut out of it. The hole may sit partly
    // outside the box, and even-odd filling cuts it only from a figure around it.
    std::vector<Figure> ShadowRingFigures(const Figure& padding, const D2D1_RECT_F& hole, float by) {
        if (IsEmpty(hole)) {
            return {Figure{.rect = padding.rect, .radii = {}}};
        }
        return {Figure{.rect = Union(padding.rect, hole), .radii = {}}, Figure{.rect = hole, .radii = ShrinkAll(padding.radii, by)}};
    }

    // Clipped to the box inside its border, as CSS clips an inset shadow, and painted last first, so the first is on top.
    void DrawShadows(ID2D1DeviceContext* context, BrushCache& brushes, ID2D1Factory* factory, const Figure& padding, const std::vector<InsetShadow>& shadows) {
        const ComPtr<ID2D1PathGeometry> clip = CreatePath(factory, {padding});
        context->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), clip.Get()), nullptr);
        for (const InsetShadow& shadow : shadows | std::views::reverse) {
            for (const ShadowRing& ring : Rings(shadow)) {
                const float        by   = shadow.spreadDips + ring.inset;
                const D2D1_RECT_F  hole = D2D1::RectF(padding.rect.left + shadow.xDips + by, padding.rect.top + shadow.yDips + by, padding.rect.right + shadow.xDips - by,
                                                      padding.rect.bottom + shadow.yDips - by);
                const D2D1_COLOR_F color{
                    .r = shadow.color.r,
                    .g = shadow.color.g,
                    .b = shadow.color.b,
                    .a = ring.alpha,
                };
                context->FillGeometry(CreatePath(factory, ShadowRingFigures(padding, hole, by)).Get(), brushes.Get(color));
            }
        }
        context->PopLayer();
    }

    // CSS splits a corner between two sides' colors on the line from its outer corner through its inner one. Stopped at
    // the inner corner, the wedges on either side of that line miss most of a curve wider than the border, which then
    // draws in neither color: it runs on as deep as the radius, but never past the box's middle.
    D2D1_POINT_2F SplitEnd(const D2D1_POINT_2F& outer, const D2D1_POINT_2F& inner, float radius, const D2D1_SIZE_F& half) {
        const float across = std::abs(inner.x - outer.x);
        const float down   = std::abs(inner.y - outer.y);
        const float widest = std::max(across, down);
        if (widest <= 0.0f) {
            return inner;
        }
        float reach = std::max(radius, widest) / widest;
        if (across > 0.0f) {
            reach = std::min(reach, half.width / across);
        }
        if (down > 0.0f) {
            reach = std::min(reach, half.height / down);
        }
        return D2D1::Point2F(outer.x + ((inner.x - outer.x) * reach), outer.y + ((inner.y - outer.y) * reach));
    }

    // One side's share of the ring, between the corner splits at its two ends. The outer corners sit a DIP out so the
    // mask never softens the ring's own outer edge.
    std::array<D2D1_POINT_2F, 4> Wedge(const D2D1_RECT_F& box, const D2D1_RECT_F& inner, float radius, size_t side) {
        const D2D1_RECT_F   outer       = D2D1::RectF(box.left - 1.0f, box.top - 1.0f, box.right + 1.0f, box.bottom + 1.0f);
        const D2D1_SIZE_F   half        = D2D1::SizeF((box.right - box.left) / 2.0f, (box.bottom - box.top) / 2.0f);
        const D2D1_POINT_2F topLeft     = SplitEnd(D2D1::Point2F(box.left, box.top), D2D1::Point2F(inner.left, inner.top), radius, half);
        const D2D1_POINT_2F topRight    = SplitEnd(D2D1::Point2F(box.right, box.top), D2D1::Point2F(inner.right, inner.top), radius, half);
        const D2D1_POINT_2F bottomRight = SplitEnd(D2D1::Point2F(box.right, box.bottom), D2D1::Point2F(inner.right, inner.bottom), radius, half);
        const D2D1_POINT_2F bottomLeft  = SplitEnd(D2D1::Point2F(box.left, box.bottom), D2D1::Point2F(inner.left, inner.bottom), radius, half);
        switch (side) {
            case 0: return {D2D1::Point2F(outer.left, outer.top), D2D1::Point2F(outer.right, outer.top), topRight, topLeft};
            case 1: return {D2D1::Point2F(outer.right, outer.top), D2D1::Point2F(outer.right, outer.bottom), bottomRight, topRight};
            case 2: return {D2D1::Point2F(outer.right, outer.bottom), D2D1::Point2F(outer.left, outer.bottom), bottomLeft, bottomRight};
            default: return {D2D1::Point2F(outer.left, outer.bottom), D2D1::Point2F(outer.left, outer.top), topLeft, bottomLeft};
        }
    }

    bool SameSide(const BorderSide& first, const BorderSide& second) {
        return first.widthDips == second.widthDips && SameColor(first.color, second.color);
    }

    std::array<BorderSide, 4> Sides(const Border& border) {
        return {border.top, border.right, border.bottom, border.left};
    }

    bool IsUniform(const Border& border) {
        return std::ranges::all_of(Sides(border), [&border](const BorderSide& side) { return SameSide(side, border.top); });
    }

    bool SameBorder(const Border& first, const Border& second) {
        return SameSide(first.top, second.top) && SameSide(first.right, second.right) && SameSide(first.bottom, second.bottom) && SameSide(first.left, second.left);
    }

    // The ring between outer and inner, or the whole of outer when the border leaves no inside.
    std::vector<Figure> BorderRingFigures(const Figure& outer, const Figure& inner) {
        if (IsEmpty(inner.rect)) {
            return {outer};
        }
        return {outer, inner};
    }

    void DrawBorder(ID2D1DeviceContext* context, BrushCache& brushes, const Figure& outer, const Figure& inner, const Border& border) {
        const std::array<BorderSide, 4> sides = Sides(border);
        if (std::ranges::none_of(sides, IsDrawn)) {
            return;
        }
        if (IsUniform(border)) {
            // A stroke is centered on its outline, so the outline is inset by half the width to keep it inside.
            const float       inset  = border.top.widthDips / 2.0f;
            const D2D1_RECT_F edge   = D2D1::RectF(outer.rect.left + inset, outer.rect.top + inset, outer.rect.right - inset, outer.rect.bottom - inset);
            const float       radius = std::max(outer.radii.topLeft.width - inset, 0.0f);
            context->DrawRoundedRectangle(D2D1::RoundedRect(edge, radius, radius), brushes.Get(border.top.color), border.top.widthDips);
            return;
        }
        const ComPtr<ID2D1Factory>      factory = FactoryOf(context);
        const ComPtr<ID2D1PathGeometry> ring    = CreatePath(factory.Get(), BorderRingFigures(outer, inner));
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
            const ComPtr<ID2D1PathGeometry> wedge = CreatePolygon(factory.Get(), Wedge(outer.rect, inner.rect, outer.radii.topLeft.width, side));
            context->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), wedge.Get()), nullptr);
            context->FillGeometry(ring.Get(), brushes.Get(sides.at(side).color));
            context->PopLayer();
        }
    }

    // Paints the shadows and border inside box, a snapped outline of radius, as CSS paints them over the background.
    void DrawDecorations(ID2D1DeviceContext* context, BrushCache& brushes, const D2D1_RECT_F& box, float radius, const BoxStyle& lines) {
        const Border&     border = lines.border;
        const D2D1_SIZE_F corner = D2D1::SizeF(radius, radius);
        const Figure      outer{.rect  = box,
                                .radii = {
                                    .topLeft     = corner,
                                    .topRight    = corner,
                                    .bottomRight = corner,
                                    .bottomLeft  = corner,
                           }};
        // CSS's inner edge: each corner's radius less the widths of the two sides that meet there.
        const Figure inner{
            .rect  = D2D1::RectF(box.left + border.left.widthDips, box.top + border.top.widthDips, box.right - border.right.widthDips, box.bottom - border.bottom.widthDips),
            .radii = {
                .topLeft     = Shrink(corner, border.left.widthDips, border.top.widthDips),
                .topRight    = Shrink(corner, border.right.widthDips, border.top.widthDips),
                .bottomRight = Shrink(corner, border.right.widthDips, border.bottom.widthDips),
                .bottomLeft  = Shrink(corner, border.left.widthDips, border.bottom.widthDips),
            }};
        if (!lines.shadows.empty() && !IsEmpty(inner.rect)) {
            DrawShadows(context, brushes, FactoryOf(context).Get(), inner, lines.shadows);
        }
        DrawBorder(context, brushes, outer, inner, border);
    }

    // A uniform border is one stroke, cheaper to draw again than to draw from a bitmap.
    bool IsWorthCaching(const BoxStyle& lines) {
        return !lines.shadows.empty() || (std::ranges::any_of(Sides(lines.border), IsDrawn) && !IsUniform(lines.border));
    }

    // How far in from an edge the decorations can still change along it: the widest side and the corner, then the
    // farthest a shadow's rings reach (offset, spread and three deviations of blur), and a DIP for antialiasing. Past
    // it, every row (or column) across the box is the same.
    float ReachDips(const BoxStyle& lines, float radius) {
        float shadowReach = 0.0f;
        for (const InsetShadow& shadow : lines.shadows) {
            shadowReach = std::max(shadowReach, std::max(std::abs(shadow.xDips), std::abs(shadow.yDips)) + std::abs(shadow.spreadDips) + (1.5f * shadow.blurDips));
        }
        const float widest = std::ranges::max(Sides(lines.border) | std::views::transform(&BorderSide::widthDips));
        return widest + radius + shadowReach + 1.0f;
    }

    // One piece of a nine-sliced bitmap along one axis: where it sits in the bitmap and where it is drawn, in DIPs.
    struct Slice {
        float sourceFrom;
        float sourceTo;
        float drawnFrom;
        float drawnTo;
    };

    // The bitmap's length along an axis for a box of length pixels: both ends and one pixel between them when it is
    // longer than that, else the box's whole length.
    std::uint32_t SlicedLength(std::uint32_t length, std::uint32_t reach) {
        return std::min(length, (2 * reach) + 1);
    }

    std::vector<Slice> Slices(float from, float to, std::uint32_t sliced, std::uint32_t length, std::uint32_t reach, float pixelsPerDip) {
        if (sliced == length) {
            return {Slice{
                .sourceFrom = 0.0f,
                .sourceTo   = to - from,
                .drawnFrom  = from,
                .drawnTo    = to,
            }};
        }
        const float end = static_cast<float>(reach) / pixelsPerDip;
        const float one = 1.0f / pixelsPerDip;
        return {Slice{
                    .sourceFrom = 0.0f,
                    .sourceTo   = end,
                    .drawnFrom  = from,
                    .drawnTo    = from + end,
                },
                Slice{
                    .sourceFrom = end,
                    .sourceTo   = end + one,
                    .drawnFrom  = from + end,
                    .drawnTo    = to - end,
                },
                Slice{
                    .sourceFrom = end + one,
                    .sourceTo   = end + one + end,
                    .drawnFrom  = to - end,
                    .drawnTo    = to,
                }};
    }

    std::uint32_t PixelLength(float dips, float pixelsPerDip) {
        return static_cast<std::uint32_t>(std::max(std::lround(dips * pixelsPerDip), 0L));
    }

    constexpr size_t kMostCachedBoxes = 64; // a shadow fade makes one a frame, so they start over

    // The box DrawBox fills: bounds snapped to device pixels, rounded to style's radius, at most half its shorter side.
    D2D1_ROUNDED_RECT SnappedOutline(const PixelGrid& grid, const D2D1_RECT_F& bounds, const BoxStyle& style) {
        const D2D1_RECT_F box    = SnapRect(grid, bounds);
        const float       radius = std::max(std::min(style.radiusDips, std::min(box.right - box.left, box.bottom - box.top) / 2.0f), 0.0f);
        return D2D1::RoundedRect(box, radius, radius);
    }

    BorderSide SnapSide(float pixelsPerDip, const BorderSide& side) {
        if (side.widthDips <= 0.0f) {
            return side;
        }
        // The small allowance keeps a width that is whole pixels in float error from losing one.
        const float pixels = std::max(std::floor((side.widthDips * pixelsPerDip) + 0.001f), 1.0f);
        return BorderSide{.widthDips = pixels / pixelsPerDip, .color = side.color};
    }

} // namespace

PixelGrid PixelGridOf(ID2D1DeviceContext* context) {
    float dpiX = 0.0f;
    float dpiY = 0.0f;
    context->GetDpi(&dpiX, &dpiY);
    D2D1_MATRIX_3X2_F transform{};
    context->GetTransform(&transform);
    return PixelGrid{
        .pixelsPerDip = dpiX / USER_DEFAULT_SCREEN_DPI,
        .offsetX      = transform._31,
        .offsetY      = transform._32,
    };
}

float SnapLength(const PixelGrid& grid, float dips) {
    return std::round(dips * grid.pixelsPerDip) / grid.pixelsPerDip;
}

D2D1_RECT_F SnapRect(const PixelGrid& grid, const D2D1_RECT_F& rect) {
    return D2D1::RectF(SnapCoordinate(grid.pixelsPerDip, grid.offsetX, rect.left), SnapCoordinate(grid.pixelsPerDip, grid.offsetY, rect.top),
                       SnapCoordinate(grid.pixelsPerDip, grid.offsetX, rect.right), SnapCoordinate(grid.pixelsPerDip, grid.offsetY, rect.bottom));
}

Insets ContentInsets(const BoxStyle& style) {
    return Insets{
        .left   = style.padding.xDips + style.border.left.widthDips,
        .top    = style.padding.yDips + style.border.top.widthDips,
        .right  = style.padding.xDips + style.border.right.widthDips,
        .bottom = style.padding.yDips + style.border.bottom.widthDips,
    };
}

bool SameColor(const D2D1_COLOR_F& first, const D2D1_COLOR_F& second) {
    return first.r == second.r && first.g == second.g && first.b == second.b && first.a == second.a;
}

bool SameShadow(const InsetShadow& first, const InsetShadow& second) {
    return first.xDips == second.xDips && first.yDips == second.yDips && first.blurDips == second.blurDips && first.spreadDips == second.spreadDips &&
        SameColor(first.color, second.color);
}

BoxStyle SnapLines(const PixelGrid& grid, const BoxStyle& style) {
    const float pixelsPerDip = grid.pixelsPerDip;
    BoxStyle    snapped      = style;
    snapped.border           = Border{
                  .top    = SnapSide(pixelsPerDip, style.border.top),
                  .right  = SnapSide(pixelsPerDip, style.border.right),
                  .bottom = SnapSide(pixelsPerDip, style.border.bottom),
                  .left   = SnapSide(pixelsPerDip, style.border.left),
    };
    for (InsetShadow& shadow : snapped.shadows) {
        shadow.xDips      = SnapLength(grid, shadow.xDips);
        shadow.yDips      = SnapLength(grid, shadow.yDips);
        shadow.spreadDips = SnapLength(grid, shadow.spreadDips);
    }
    return snapped;
}

std::vector<D2D1_ROUNDED_RECT> BackdropOutlines(const Backdrop& backdrop, const PixelGrid& grid, const std::vector<D2D1_RECT_F>& boxes, const BoxStyle& style) {
    std::vector<D2D1_ROUNDED_RECT> outlines;
    if (backdrop.source == BackdropSource::None) {
        return outlines;
    }
    outlines.reserve(boxes.size());
    for (const D2D1_RECT_F& box : boxes) {
        outlines.push_back(SnappedOutline(grid, box, style));
    }
    return outlines;
}

void DrawBackdrops(Surface& surface, const std::vector<WallpaperBlur>& wallpapers, const Backdrop& backdrop, const std::vector<D2D1_RECT_F>& boxes, const BoxStyle& style,
                   const RECT& windowRect) {
    const std::vector<D2D1_ROUNDED_RECT> outlines = BackdropOutlines(backdrop, PixelGridOf(surface.Context()), boxes, style);
    if (backdrop.source == BackdropSource::Wallpaper) {
        BlurFor(wallpapers, backdrop).Fill(surface.Context(), windowRect, outlines);
    }
    surface.ShowBackdrops(outlines, backdrop);
}

BoxCache::BoxCache(ID2D1DeviceContext* context) :
    _context([context] {
        ComPtr<ID2D1Device> device;
        context->GetDevice(&device);
        ComPtr<ID2D1DeviceContext> own;
        ThrowIfDeviceFailed(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &own), "ID2D1Device::CreateDeviceContext(box cache)");
        return own;
    }()),
    _brushes(_context.Get()) {}

ID2D1Bitmap1* BoxCache::BitmapFor(const BoxStyle& lines, float radius, float pixelsPerDip, std::uint32_t width, std::uint32_t height) {
    const auto found = std::ranges::find_if(_entries, [&](const Entry& entry) {
        return entry.radius == radius && entry.pixelsPerDip == pixelsPerDip && entry.width == width && entry.height == height && SameBorder(entry.border, lines.border) &&
            std::ranges::equal(entry.shadows, lines.shadows, SameShadow);
    });
    if (found == _entries.end()) {
        return Render(lines, radius, pixelsPerDip, width, height);
    }
    std::rotate(found, found + 1, _entries.end());
    return _entries.back().bitmap.Get();
}

ID2D1Bitmap1* BoxCache::Render(const BoxStyle& lines, float radius, float pixelsPerDip, std::uint32_t width, std::uint32_t height) {
    const float          dpi = pixelsPerDip * USER_DEFAULT_SCREEN_DPI;
    ComPtr<ID2D1Bitmap1> drawn;
    ThrowIfDeviceFailed(
        _context->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
                               D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi), &drawn),
        "ID2D1DeviceContext::CreateBitmap(box)");
    _context->SetTarget(drawn.Get());
    _context->SetDpi(dpi, dpi);
    _context->BeginDraw();
    _context->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    DrawDecorations(_context.Get(), _brushes, D2D1::RectF(0.0f, 0.0f, static_cast<float>(width) / pixelsPerDip, static_cast<float>(height) / pixelsPerDip), radius, lines);
    const HRESULT ended = _context->EndDraw();
    _context->SetTarget(nullptr);
    ThrowIfDeviceFailed(ended, "ID2D1DeviceContext::EndDraw(box)");
    if (_entries.size() == kMostCachedBoxes) {
        _entries.erase(_entries.begin());
    }
    _entries.push_back(Entry{
        .border       = lines.border,
        .shadows      = lines.shadows,
        .radius       = radius,
        .pixelsPerDip = pixelsPerDip,
        .width        = width,
        .height       = height,
        .bitmap       = drawn,
    });
    return drawn.Get();
}

void BoxCache::Draw(ID2D1DeviceContext* context, const PixelGrid& grid, const D2D1_RECT_F& box, float radius, const BoxStyle& lines) {
    const float         pixelsPerDip = grid.pixelsPerDip;
    const std::uint32_t reach        = static_cast<std::uint32_t>(std::ceil(ReachDips(lines, radius) * pixelsPerDip));
    const std::uint32_t length       = PixelLength(box.right - box.left, pixelsPerDip);
    const std::uint32_t depth        = PixelLength(box.bottom - box.top, pixelsPerDip);
    const std::uint32_t width        = SlicedLength(length, reach);
    const std::uint32_t height       = SlicedLength(depth, reach);
    ID2D1Bitmap1* const bitmap       = BitmapFor(lines, radius, pixelsPerDip, width, height);
    for (const Slice& across : Slices(box.left, box.right, width, length, reach, pixelsPerDip)) {
        for (const Slice& down : Slices(box.top, box.bottom, height, depth, reach, pixelsPerDip)) {
            const D2D1_RECT_F drawnRect = D2D1::RectF(across.drawnFrom, down.drawnFrom, across.drawnTo, down.drawnTo);
            const D2D1_RECT_F source    = D2D1::RectF(across.sourceFrom, down.sourceFrom, across.sourceTo, down.sourceTo);
            context->DrawBitmap(bitmap, drawnRect, 1.0f, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, source);
        }
    }
}

void DrawBox(ID2D1DeviceContext* context, BrushCache& brushes, BoxCache& boxes, const D2D1_RECT_F& bounds, const BoxStyle& style, const D2D1_COLOR_F& fill) {
    const PixelGrid         grid    = PixelGridOf(context);
    const D2D1_ROUNDED_RECT outline = SnappedOutline(grid, bounds, style);
    // An empty module's box has no length, and its inset border would draw inside out.
    if (IsEmpty(outline.rect)) {
        return;
    }
    const BoxStyle lines = SnapLines(grid, style);
    if (fill.a > 0.0f) {
        context->FillRoundedRectangle(outline, brushes.Get(fill));
    }
    if (IsWorthCaching(lines)) {
        boxes.Draw(context, grid, outline.rect, outline.radiusX, lines);
        return;
    }
    DrawDecorations(context, brushes, outline.rect, outline.radiusX, lines);
}
