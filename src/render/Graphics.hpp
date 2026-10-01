#pragma once

#include "platform/Errors.hpp"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

// Holds C++/WinRT types, which only render/ sources include (render/Composition.hpp).
struct CompositionDevice;

// Shared by every bar: one D3D device feeds all swap chains and one compositor holds all visuals.
struct GraphicsDevice {
    Microsoft::WRL::ComPtr<ID3D11Device>   d3d;
    Microsoft::WRL::ComPtr<IDXGIFactory2>  dxgiFactory;
    Microsoft::WRL::ComPtr<ID2D1Factory1>  d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1Device>    d2d;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite;
    std::shared_ptr<CompositionDevice>     composition;
};

// Logs the adapter it renders on, which may not be the one showing a given monitor. Needs a CompositionThread on the
// calling thread.
GraphicsDevice CreateGraphicsDevice();

struct MonitorOutput {
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    std::wstring                        adapter; // the adapter showing the monitor, not always the one rendering
    std::uint32_t                       refreshHz;
};

// The DXGI output showing monitor, read from a new factory so a monitor attached since the device was made is listed.
// Throws MonitorGoneError when the monitor is gone or no adapter lists it.
MonitorOutput OutputForMonitor(HMONITOR monitor);

// ThrowIfFailed, except that the results meaning the device is lost throw DeviceLostError.
void                                         ThrowIfDeviceFailed(HRESULT result, std::string_view call);

Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> CreateBrush(ID2D1DeviceContext* context, const D2D1_COLOR_F& color);

// Rounded to the nearest pixel.
std::int32_t DipsToPixels(float dips, std::uint32_t dpi);

float        PixelsToDips(float pixels, std::uint32_t dpi);
// A client-area point in pixels, in DIPs.
D2D1_POINT_2F ClientDips(POINT client, std::uint32_t dpi);

// Rounded up, so text sized by it is never clipped by a partial pixel.
std::int32_t DipsToWholePixels(float dips, std::uint32_t dpi);

// A rect in DIPs from a window's top left corner, in screen pixels.
RECT DipsToScreen(const RECT& window, const D2D1_RECT_F& dips, std::uint32_t dpi);

// DirectWrite silently substitutes a missing family, which would hide a broken font install.
void RequireFontFamily(IDWriteFactory* dwrite, const wchar_t* family);
