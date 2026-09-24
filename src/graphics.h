#pragma once

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

// Shared by every bar window: one D3D device feeds all swap chains and one DComp device commits all visuals.
struct GraphicsDevice {
    Microsoft::WRL::ComPtr<ID3D11Device>        d3d;
    Microsoft::WRL::ComPtr<IDXGIFactory2>       dxgiFactory;
    Microsoft::WRL::ComPtr<ID2D1Factory1>       d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1Device>         d2d;
    Microsoft::WRL::ComPtr<IDWriteFactory>      dwrite;
    Microsoft::WRL::ComPtr<IDCompositionDevice> composition;
};

GraphicsDevice CreateGraphicsDevice();

// DirectWrite silently substitutes a missing family, which would hide a broken font install.
void RequireFontFamily(IDWriteFactory* dwrite, const wchar_t* family);
