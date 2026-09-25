#include "render/Surface.hpp"

using Microsoft::WRL::ComPtr;

Surface::Surface(const GraphicsDevice& graphics, HWND window, UINT width, UINT height, UINT dpi) {
    ThrowIfDeviceFailed(graphics.d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &_context), "ID2D1Device::CreateDeviceContext");
    ThrowIfDeviceFailed(graphics.composition->CreateTargetForHwnd(window, TRUE, &_compositionTarget), "IDCompositionDevice::CreateTargetForHwnd");
    ThrowIfDeviceFailed(graphics.composition->CreateVisual(&_visual), "IDCompositionDevice::CreateVisual");
    ThrowIfDeviceFailed(_compositionTarget->SetRoot(_visual.Get()), "IDCompositionTarget::SetRoot");

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width            = width;
    description.Height           = height;
    description.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount      = 2;
    description.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    description.AlphaMode        = DXGI_ALPHA_MODE_PREMULTIPLIED;
    ThrowIfDeviceFailed(graphics.dxgiFactory->CreateSwapChainForComposition(graphics.d3d.Get(), &description, nullptr, &_swapChain),
                        "IDXGIFactory2::CreateSwapChainForComposition");
    ThrowIfDeviceFailed(_visual->SetContent(_swapChain.Get()), "IDCompositionVisual::SetContent");
    ThrowIfDeviceFailed(graphics.composition->Commit(), "IDCompositionDevice::Commit");
    BindRenderTarget(dpi);
}

ID2D1DeviceContext* Surface::Context() const {
    return _context.Get();
}

void Surface::Resize(UINT width, UINT height, UINT dpi) {
    // ResizeBuffers fails while D2D still holds a bitmap over the old back buffer.
    _context->SetTarget(nullptr);
    ThrowIfDeviceFailed(_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "IDXGISwapChain1::ResizeBuffers");
    BindRenderTarget(dpi);
}

void Surface::EndDrawAndPresent() {
    ThrowIfDeviceFailed(_context->EndDraw(), "ID2D1DeviceContext::EndDraw");
    ThrowIfDeviceFailed(_swapChain->Present(1, 0), "IDXGISwapChain1::Present");
}

void Surface::BindRenderTarget(UINT dpi) {
    ComPtr<IDXGISurface> surface;
    ThrowIfDeviceFailed(_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface)), "IDXGISwapChain1::GetBuffer");

    const auto                    pixelsPerInch = static_cast<float>(dpi);
    const D2D1_BITMAP_PROPERTIES1 properties    = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                                                          D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), pixelsPerInch, pixelsPerInch);
    ComPtr<ID2D1Bitmap1>          target;
    ThrowIfDeviceFailed(_context->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &target), "ID2D1DeviceContext::CreateBitmapFromDxgiSurface");
    _context->SetTarget(target.Get());
    _context->SetDpi(pixelsPerInch, pixelsPerInch);
}
