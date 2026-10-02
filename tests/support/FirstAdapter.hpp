#pragma once

#include "platform/Errors.hpp"

#include <dxgi1_3.h>
#include <wrl/client.h>

// The adapter D3D11CreateDevice would pick if given none.
inline Microsoft::WRL::ComPtr<IDXGIAdapter1> FirstAdapter() {
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    ThrowIfFailed(factory->EnumAdapters1(0, &adapter), "IDXGIFactory1::EnumAdapters1");
    return adapter;
}
