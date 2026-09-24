#include "graphics.h"

#include "errors.h"
#include "text.h"

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

void RequireFontFamily(IDWriteFactory* dwrite, const wchar_t* family) {
    ComPtr<IDWriteFontCollection> fonts;
    ThrowIfFailed(dwrite->GetSystemFontCollection(&fonts, FALSE), "IDWriteFactory::GetSystemFontCollection");

    UINT32 index  = 0;
    BOOL   exists = FALSE;
    ThrowIfFailed(fonts->FindFamilyName(family, &index, &exists), "IDWriteFontCollection::FindFamilyName");
    if (!exists) {
        throw std::runtime_error(std::format("font family is not installed: {}", ToUtf8(family)));
    }
}
