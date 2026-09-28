#include "render/Graphics.hpp"

#include "platform/Errors.hpp"
#include "platform/Text.hpp"

#include <windows.h>

#include <cstdint>
#include <cmath>
#include <format>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

GraphicsDevice CreateGraphicsDevice() {
    GraphicsDevice device;

    ThrowIfFailed(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device.d3d, nullptr, nullptr),
                  "D3D11CreateDevice");

    ComPtr<IDXGIDevice> dxgiDevice;
    ThrowIfFailed(device.d3d.As(&dxgiDevice), "ID3D11Device::QueryInterface(IDXGIDevice)");
    ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&device.dxgiFactory)), "CreateDXGIFactory2");

    ThrowIfFailed(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, device.d2dFactory.GetAddressOf()), "D2D1CreateFactory");
    ThrowIfFailed(device.d2dFactory->CreateDevice(dxgiDevice.Get(), &device.d2d), "ID2D1Factory1::CreateDevice");

    ThrowIfFailed(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(device.dwrite.GetAddressOf())), "DWriteCreateFactory");

    ThrowIfFailed(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&device.composition)), "DCompositionCreateDevice");

    return device;
}

void ThrowIfDeviceFailed(HRESULT result, std::string_view call) {
    if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET || result == D2DERR_RECREATE_TARGET) {
        throw DeviceLostError(std::format("{} lost the graphics device with 0x{:08X}", call, static_cast<std::uint32_t>(result)));
    }
    ThrowIfFailed(result, call);
}

ComPtr<ID2D1SolidColorBrush> CreateBrush(ID2D1DeviceContext* context, const D2D1_COLOR_F& color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    ThrowIfDeviceFailed(context->CreateSolidColorBrush(color, &brush), "ID2D1DeviceContext::CreateSolidColorBrush");
    return brush;
}

std::int32_t DipsToPixels(float dips, std::uint32_t dpi) {
    return static_cast<std::int32_t>(std::lround(dips * static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI));
}

RECT DipsToScreen(const RECT& window, const D2D1_RECT_F& dips, std::uint32_t dpi) {
    return RECT{.left   = window.left + DipsToPixels(dips.left, dpi),
                .top    = window.top + DipsToPixels(dips.top, dpi),
                .right  = window.left + DipsToPixels(dips.right, dpi),
                .bottom = window.top + DipsToPixels(dips.bottom, dpi)};
}

void RequireFontFamily(IDWriteFactory* dwrite, const wchar_t* family) {
    ComPtr<IDWriteFontCollection> fonts;
    ThrowIfFailed(dwrite->GetSystemFontCollection(&fonts, FALSE), "IDWriteFactory::GetSystemFontCollection");

    std::uint32_t index  = 0;
    BOOL          exists = FALSE;
    ThrowIfFailed(fonts->FindFamilyName(family, &index, &exists), "IDWriteFontCollection::FindFamilyName");
    if (!exists) {
        throw std::runtime_error(std::format("font family is not installed: {}", ToUtf8(family)));
    }
}
