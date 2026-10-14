#include "render/Axis.hpp"

#include <algorithm>

Axis AxisOf(Edge edge) {
    if (edge == Edge::Top || edge == Edge::Bottom) {
        return Axis::Horizontal;
    }
    return Axis::Vertical;
}

float MainLength(D2D1_SIZE_F size, Axis axis) {
    if (axis == Axis::Horizontal) {
        return size.width;
    }
    return size.height;
}

float CrossLength(D2D1_SIZE_F size, Axis axis) {
    if (axis == Axis::Horizontal) {
        return size.height;
    }
    return size.width;
}

float MainOf(const Spacing& spacing, Axis axis) {
    if (axis == Axis::Horizontal) {
        return spacing.xDips;
    }
    return spacing.yDips;
}

float CrossOf(const Spacing& spacing, Axis axis) {
    if (axis == Axis::Horizontal) {
        return spacing.yDips;
    }
    return spacing.xDips;
}

float MainOf(D2D1_POINT_2F point, Axis axis) {
    if (axis == Axis::Horizontal) {
        return point.x;
    }
    return point.y;
}

float MainStart(const D2D1_RECT_F& rect, Axis axis) {
    if (axis == Axis::Horizontal) {
        return rect.left;
    }
    return rect.top;
}

float MainEnd(const D2D1_RECT_F& rect, Axis axis) {
    if (axis == Axis::Horizontal) {
        return rect.right;
    }
    return rect.bottom;
}

D2D1_POINT_2F MovedAlong(D2D1_POINT_2F point, float distance, Axis axis) {
    if (axis == Axis::Horizontal) {
        return D2D1::Point2F(point.x + distance, point.y);
    }
    return D2D1::Point2F(point.x, point.y + distance);
}

D2D1_SIZE_F MainCrossSize(Axis axis, float main, float cross) {
    if (axis == Axis::Horizontal) {
        return D2D1::SizeF(main, cross);
    }
    return D2D1::SizeF(cross, main);
}

D2D1_RECT_F MainCrossRect(Axis axis, float mainStart, float mainLength, float crossStart, float crossLength) {
    if (axis == Axis::Horizontal) {
        return D2D1::RectF(mainStart, crossStart, mainStart + mainLength, crossStart + crossLength);
    }
    return D2D1::RectF(crossStart, mainStart, crossStart + crossLength, mainStart + mainLength);
}

bool Contains(const D2D1_RECT_F& rect, D2D1_POINT_2F point) {
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
}

D2D1_RECT_F Offset(const D2D1_RECT_F& rect, D2D1_POINT_2F by) {
    return D2D1::RectF(rect.left + by.x, rect.top + by.y, rect.right + by.x, rect.bottom + by.y);
}

D2D1_RECT_F Intersection(const D2D1_RECT_F& first, const D2D1_RECT_F& second) {
    return D2D1::RectF(std::max(first.left, second.left), std::max(first.top, second.top), std::min(first.right, second.right), std::min(first.bottom, second.bottom));
}
