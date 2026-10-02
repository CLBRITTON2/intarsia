#include "render/Graphics.hpp"

#include "platform/Errors.hpp"
#include "render/CompositionThread.hpp"
#include "support/FirstAdapter.hpp"

#include <gtest/gtest.h>

#include <d2d1_1.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

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

// App matches each monitor's adapter to the device made on it by this id.
TEST(CreateGraphicsDevice, RendersOnTheAdapterItWasGiven) {
    const CompositionThread                     compositionThread;
    const Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter = FirstAdapter();
    const GraphicsDevice                        device  = CreateGraphicsDevice(adapter.Get());

    DXGI_ADAPTER_DESC1                          given{};
    ASSERT_TRUE(SUCCEEDED(adapter->GetDesc1(&given)));
    EXPECT_TRUE(SameAdapter(device.adapterId, given.AdapterLuid));

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    ASSERT_TRUE(SUCCEEDED(device.d3d.As(&dxgiDevice)));
    Microsoft::WRL::ComPtr<IDXGIAdapter> rendering;
    ASSERT_TRUE(SUCCEEDED(dxgiDevice->GetAdapter(&rendering)));
    DXGI_ADAPTER_DESC used{};
    ASSERT_TRUE(SUCCEEDED(rendering->GetDesc(&used)));
    EXPECT_TRUE(SameAdapter(used.AdapterLuid, given.AdapterLuid));
}

TEST(SameAdapter, ComparesBothHalvesOfTheId) {
    EXPECT_TRUE(SameAdapter(LUID{.LowPart = 1, .HighPart = 2}, LUID{.LowPart = 1, .HighPart = 2}));
    EXPECT_FALSE(SameAdapter(LUID{.LowPart = 1, .HighPart = 2}, LUID{.LowPart = 1, .HighPart = 3}));
    EXPECT_FALSE(SameAdapter(LUID{.LowPart = 4, .HighPart = 2}, LUID{.LowPart = 1, .HighPart = 2}));
}

TEST(ThrowIfDeviceFailed, ThrowsDeviceLostOnlyForTheCodesOfALostDevice) {
    EXPECT_THROW(ThrowIfDeviceFailed(DXGI_ERROR_DEVICE_REMOVED, "Present"), DeviceLostError);
    EXPECT_THROW(ThrowIfDeviceFailed(DXGI_ERROR_DEVICE_RESET, "Present"), DeviceLostError);
    EXPECT_THROW(ThrowIfDeviceFailed(D2DERR_RECREATE_TARGET, "EndDraw"), DeviceLostError);
    EXPECT_THROW(ThrowIfDeviceFailed(E_FAIL, "Present"), Win32Error);
    EXPECT_NO_THROW(ThrowIfDeviceFailed(S_OK, "Present"));
}
