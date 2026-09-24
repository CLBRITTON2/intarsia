#include "graphics.h"

#include "errors.h"

#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

using Microsoft::WRL::ComPtr;

namespace {

    std::string ToUtf8(std::wstring_view text) {
        const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (length == 0) {
            ThrowLastError("WideCharToMultiByte");
        }
        std::string utf8(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), length, nullptr, nullptr);
        return utf8;
    }

} // namespace

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
