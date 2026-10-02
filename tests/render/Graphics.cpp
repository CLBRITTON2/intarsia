#include "render/Graphics.hpp"

#include "platform/Errors.hpp"

#include <gtest/gtest.h>

#include <d2d1_1.h>
#include <windows.h>

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

TEST(ThrowIfDeviceFailed, ThrowsDeviceLostOnlyForTheCodesOfALostDevice) {
    EXPECT_THROW(ThrowIfDeviceFailed(DXGI_ERROR_DEVICE_REMOVED, "Present"), DeviceLostError);
    EXPECT_THROW(ThrowIfDeviceFailed(DXGI_ERROR_DEVICE_RESET, "Present"), DeviceLostError);
    EXPECT_THROW(ThrowIfDeviceFailed(D2DERR_RECREATE_TARGET, "EndDraw"), DeviceLostError);
    EXPECT_THROW(ThrowIfDeviceFailed(E_FAIL, "Present"), Win32Error);
    EXPECT_NO_THROW(ThrowIfDeviceFailed(S_OK, "Present"));
}
