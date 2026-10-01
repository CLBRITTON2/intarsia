#include "render/Graphics.hpp"

#include "platform/Errors.hpp"

#include <gtest/gtest.h>

TEST(PixelsToDips, ScalesByTheDefaultDpiOverTheMonitors) {
    EXPECT_FLOAT_EQ(PixelsToDips(120.0f, 96), 120.0f);
    EXPECT_FLOAT_EQ(PixelsToDips(120.0f, 144), 80.0f);
}

TEST(ClientDips, ScalesBothCoordinates) {
    const D2D1_POINT_2F point = ClientDips(POINT{.x = 30, .y = -60}, 192);
    EXPECT_FLOAT_EQ(point.x, 15.0f);
    EXPECT_FLOAT_EQ(point.y, -30.0f);
}

TEST(OutputForMonitor, ThrowsMonitorGoneForAHandleNamingNoMonitor) {
    EXPECT_THROW(OutputForMonitor(nullptr), MonitorGoneError);
}
