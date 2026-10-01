#include "render/Axis.hpp"

Axis AxisOf(Edge edge) {
    return edge == Edge::Top || edge == Edge::Bottom ? Axis::Horizontal : Axis::Vertical;
}

float MainLength(D2D1_SIZE_F size, Axis axis) {
    return axis == Axis::Horizontal ? size.width : size.height;
}

float CrossLength(D2D1_SIZE_F size, Axis axis) {
    return axis == Axis::Horizontal ? size.height : size.width;
}

float MainOf(const Spacing& spacing, Axis axis) {
    return axis == Axis::Horizontal ? spacing.xDips : spacing.yDips;
}

float CrossOf(const Spacing& spacing, Axis axis) {
    return axis == Axis::Horizontal ? spacing.yDips : spacing.xDips;
}

float MainOf(D2D1_POINT_2F point, Axis axis) {
    return axis == Axis::Horizontal ? point.x : point.y;
}

float MainStart(const D2D1_RECT_F& rect, Axis axis) {
    return axis == Axis::Horizontal ? rect.left : rect.top;
}

float MainEnd(const D2D1_RECT_F& rect, Axis axis) {
    return axis == Axis::Horizontal ? rect.right : rect.bottom;
}

D2D1_POINT_2F MovedAlong(D2D1_POINT_2F point, float distance, Axis axis) {
    return axis == Axis::Horizontal ? D2D1::Point2F(point.x + distance, point.y) : D2D1::Point2F(point.x, point.y + distance);
}

D2D1_SIZE_F MainCrossSize(Axis axis, float main, float cross) {
    return axis == Axis::Horizontal ? D2D1::SizeF(main, cross) : D2D1::SizeF(cross, main);
}

D2D1_RECT_F MainCrossRect(Axis axis, float mainStart, float mainLength, float crossStart, float crossLength) {
    if (axis == Axis::Horizontal) {
        return D2D1::RectF(mainStart, crossStart, mainStart + mainLength, crossStart + crossLength);
    }
    return D2D1::RectF(crossStart, mainStart, crossStart + crossLength, mainStart + mainLength);
}
