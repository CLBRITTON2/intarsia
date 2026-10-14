#include "render/Graphics.hpp"

#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Monitors.hpp"
#include "platform/Text.hpp"
#include "render/Composition.hpp"

#include <dxgi1_3.h>
#include <windows.h>

#include <cmath>
#include <cstdint>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {

    std::wstring AdapterName(IDXGIAdapter* adapter) {
        DXGI_ADAPTER_DESC description{};
        ThrowIfFailed(adapter->GetDesc(&description), "IDXGIAdapter::GetDesc");
        return description.Description;
    }

    std::uint32_t RefreshHz(const std::wstring& display) {
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsW(display.c_str(), ENUM_CURRENT_SETTINGS, &mode)) {
            throw MonitorGoneError(std::format("EnumDisplaySettingsW found no current mode for display {}", ToUtf8(display)));
        }
        return mode.dmDisplayFrequency;
    }

} // namespace

GraphicsDevice CreateGraphicsDevice() {
    GraphicsDevice device;

    // A driver's worker thread gains nothing on frames this small, and one driver's threaded path keeps about 50 KB for
    // good of every bitmap released while two swap chains present.
    constexpr UINT kDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
    ThrowIfFailed(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, kDeviceFlags, nullptr, 0, D3D11_SDK_VERSION, &device.d3d, nullptr, nullptr), "D3D11CreateDevice");

    ComPtr<IDXGIDevice> dxgiDevice;
    ThrowIfFailed(device.d3d.As(&dxgiDevice), "ID3D11Device::QueryInterface(IDXGIDevice)");
    ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&device.dxgiFactory)), "CreateDXGIFactory2");
    ComPtr<IDXGIAdapter> adapter;
    ThrowIfFailed(dxgiDevice->GetAdapter(&adapter), "IDXGIDevice::GetAdapter");
    Log(LogLevel::Info, "graphics", "device_created", {{.key = "adapter", .value = ToUtf8(AdapterName(adapter.Get()))}});

    ComPtr<ID2D1Factory1> d2dFactory;
    ThrowIfFailed(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory.GetAddressOf()), "D2D1CreateFactory");
    ThrowIfFailed(d2dFactory->CreateDevice(dxgiDevice.Get(), &device.d2d), "ID2D1Factory1::CreateDevice");

    ThrowIfFailed(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(device.dwrite.GetAddressOf())), "DWriteCreateFactory");

    device.composition = std::make_shared<CompositionDevice>(CreateCompositionDevice(device.d2d.Get()));

    return device;
}

MonitorOutput OutputForMonitor(HMONITOR monitor) {
    ComPtr<IDXGIFactory2> factory;
    ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");
    const std::wstring display = MonitorInfo(monitor).szDevice;
    for (std::uint32_t adapterIndex = 0;; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT         adapterFound = factory->EnumAdapters1(adapterIndex, &adapter);
        if (adapterFound == DXGI_ERROR_NOT_FOUND) {
            throw MonitorGoneError(std::format("no graphics adapter lists display {} as an output", ToUtf8(display)));
        }
        ThrowIfFailed(adapterFound, "IDXGIFactory1::EnumAdapters1");
        for (std::uint32_t outputIndex = 0;; ++outputIndex) {
            ComPtr<IDXGIOutput> output;
            const HRESULT       outputFound = adapter->EnumOutputs(outputIndex, &output);
            if (outputFound == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            ThrowIfFailed(outputFound, "IDXGIAdapter::EnumOutputs");
            DXGI_OUTPUT_DESC description{};
            ThrowIfFailed(output->GetDesc(&description), "IDXGIOutput::GetDesc");
            if (description.Monitor == monitor) {
                return MonitorOutput{
                    .output    = output,
                    .adapter   = AdapterName(adapter.Get()),
                    .refreshHz = RefreshHz(display),
                };
            }
        }
    }
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

float PixelsToDips(float pixels, std::uint32_t dpi) {
    return pixels * USER_DEFAULT_SCREEN_DPI / static_cast<float>(dpi);
}

D2D1_POINT_2F ClientDips(POINT client, std::uint32_t dpi) {
    return D2D1::Point2F(PixelsToDips(static_cast<float>(client.x), dpi), PixelsToDips(static_cast<float>(client.y), dpi));
}

std::int32_t DipsToWholePixels(float dips, std::uint32_t dpi) {
    return static_cast<std::int32_t>(std::ceil(dips * static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI));
}

RECT DipsToScreen(const RECT& window, const D2D1_RECT_F& dips, std::uint32_t dpi) {
    return RECT{
        .left   = window.left + DipsToPixels(dips.left, dpi),
        .top    = window.top + DipsToPixels(dips.top, dpi),
        .right  = window.left + DipsToPixels(dips.right, dpi),
        .bottom = window.top + DipsToPixels(dips.bottom, dpi),
    };
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
