#include "render/Graphics.hpp"

#include "platform/Errors.hpp"

#include <gtest/gtest.h>

#include <d2d1_1.h>
#include <windows.h>

TEST(ThrowIfDeviceFailed, ThrowsDeviceLostOnlyForTheCodesOfALostDevice) {
    EXPECT_THROW(ThrowIfDeviceFailed(DXGI_ERROR_DEVICE_REMOVED, "Present"), DeviceLostError);
    EXPECT_THROW(ThrowIfDeviceFailed(DXGI_ERROR_DEVICE_RESET, "Present"), DeviceLostError);
    EXPECT_THROW(ThrowIfDeviceFailed(D2DERR_RECREATE_TARGET, "EndDraw"), DeviceLostError);
    EXPECT_THROW(ThrowIfDeviceFailed(E_FAIL, "Present"), Win32Error);
    EXPECT_NO_THROW(ThrowIfDeviceFailed(S_OK, "Present"));
}
