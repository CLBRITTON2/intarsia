#include "render/Surface.hpp"

#include "render/Composition.hpp"

#include <dwmapi.h>
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.UI.Composition.Desktop.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace composition = winrt::Windows::UI::Composition;
namespace interop = ABI::Windows::UI::Composition;

namespace {

    // A backdrop's sprite, the mask surface that rounds it, and the box both were last placed for, none before the first.
    struct BackdropVisual {
        composition::SpriteVisual sprite;
        composition::CompositionDrawingSurface mask;
        std::optional<D2D1_ROUNDED_RECT> box;
    };

    bool SameBox(const D2D1_ROUNDED_RECT& first, const D2D1_ROUNDED_RECT& second) {
        return first.rect.left == second.rect.left && first.rect.top == second.rect.top && first.rect.right == second.rect.right && first.rect.bottom == second.rect.bottom &&
            first.radiusX == second.radiusX && first.radiusY == second.radiusY;
    }

    winrt::Windows::Graphics::SizeInt32 PixelSize(const D2D1_RECT_F& rect, std::uint32_t dpi) {
        return {.Width = DipsToPixels(rect.right, dpi) - DipsToPixels(rect.left, dpi), .Height = DipsToPixels(rect.bottom, dpi) - DipsToPixels(rect.top, dpi)};
    }

    // Opaque inside box and clear outside it, drawn as DrawBox fills a box so the two edges antialias alike.
    void DrawMask(const composition::CompositionDrawingSurface& mask, const D2D1_ROUNDED_RECT& box, std::uint32_t dpi) {
        const auto surface = mask.as<interop::ICompositionDrawingSurfaceInterop>();
        ComPtr<ID2D1DeviceContext> context;
        POINT offset{};
        ThrowIfDeviceFailed(surface->BeginDraw(nullptr, IID_PPV_ARGS(&context), &offset), "ICompositionDrawingSurfaceInterop::BeginDraw");
        const auto pixelsPerInch = static_cast<float>(dpi);
        context->SetDpi(pixelsPerInch, pixelsPerInch);
        // The surface may be one part of a larger one, which offset (in pixels) places it in.
        context->SetTransform(
            D2D1::Matrix3x2F::Translation(PixelsToDips(static_cast<float>(offset.x), dpi) - box.rect.left, PixelsToDips(static_cast<float>(offset.y), dpi) - box.rect.top));
        context->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        context->FillRoundedRectangle(box, CreateBrush(context.Get(), D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f)).Get());
        ThrowIfDeviceFailed(surface->EndDraw(), "ICompositionDrawingSurfaceInterop::EndDraw");
    }

    BackdropVisual CreateBackdrop(CompositionDevice& device, const Backdrop& tone) {
        BackdropVisual backdrop{
            .sprite = device.compositor.CreateSpriteVisual(),
            .mask = device.graphics.CreateDrawingSurface2({.Width = 0, .Height = 0}, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                                          winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied),
            .box = std::nullopt,
        };
        const composition::CompositionEffectBrush brush = device.backdrops.For(device.compositor, tone).CreateBrush();
        brush.SetSourceParameter(L"backdrop", device.compositor.CreateHostBackdropBrush());
        brush.SetSourceParameter(L"mask", device.compositor.CreateSurfaceBrush(backdrop.mask));
        backdrop.sprite.Brush(brush);
        return backdrop;
    }

    void PlaceBackdrop(BackdropVisual& backdrop, const D2D1_ROUNDED_RECT& box, std::uint32_t dpi) {
        const winrt::Windows::Graphics::SizeInt32 size = PixelSize(box.rect, dpi);
        backdrop.box = box;
        backdrop.sprite.Offset({static_cast<float>(DipsToPixels(box.rect.left, dpi)), static_cast<float>(DipsToPixels(box.rect.top, dpi)), 0.0f});
        backdrop.sprite.Size({static_cast<float>(std::max(size.Width, 0)), static_cast<float>(std::max(size.Height, 0))});
        if (size.Width <= 0 || size.Height <= 0) {
            return;
        }
        backdrop.mask.Resize(size);
        DrawMask(backdrop.mask, box, dpi);
    }

    std::vector<D2D1_ROUNDED_RECT> SystemBlurBoxes(const std::vector<D2D1_ROUNDED_RECT>& boxes, const Backdrop& backdrop) {
        if (backdrop.source != BackdropSource::System) {
            return {};
        }
        return boxes;
    }

} // namespace

// The window's visuals from the bottom: the backdrops, then the swap chain's content. The backdrops' brushes are made
// for tone, none before the first.
struct SurfaceVisuals {
    composition::Desktop::DesktopWindowTarget target{nullptr};
    composition::ContainerVisual backdropLayer{nullptr};
    composition::SpriteVisual content{nullptr};
    std::vector<BackdropVisual> backdrops{};
    std::optional<Backdrop> tone{};
};

Surface::Surface(const GraphicsDevice& graphics, HWND window, std::uint32_t width, std::uint32_t height, std::uint32_t dpi) :
    _composition(graphics.composition), _dpi(dpi), _visuals(std::make_unique<SurfaceVisuals>()) {
    ThrowIfDeviceFailed(graphics.d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &_context), "ID2D1Device::CreateDeviceContext");

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = width;
    description.Height = height;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    ThrowIfDeviceFailed(graphics.dxgiFactory->CreateSwapChainForComposition(graphics.d3d.Get(), &description, nullptr, &_swapChain),
                        "IDXGIFactory2::CreateSwapChainForComposition");

    // A host backdrop brush draws nothing in a window that has not asked for it.
    const BOOL hostBackdrop = TRUE;
    ThrowIfFailed(DwmSetWindowAttribute(window, DWMWA_USE_HOSTBACKDROPBRUSH, &hostBackdrop, sizeof(hostBackdrop)), "DwmSetWindowAttribute(DWMWA_USE_HOSTBACKDROPBRUSH)");

    Composing("Surface", [this, window, width, height] {
        const composition::Compositor& compositor = _composition->compositor;
        ThrowIfDeviceFailed(compositor.as<interop::Desktop::ICompositorDesktopInterop>()->CreateDesktopWindowTarget(
                                window, TRUE, reinterpret_cast<interop::Desktop::IDesktopWindowTarget**>(winrt::put_abi(_visuals->target))),
                            "ICompositorDesktopInterop::CreateDesktopWindowTarget");
        composition::ICompositionSurface swapChainSurface{nullptr};
        ThrowIfDeviceFailed(compositor.as<interop::ICompositorInterop>()->CreateCompositionSurfaceForSwapChain(
                                _swapChain.Get(), reinterpret_cast<interop::ICompositionSurface**>(winrt::put_abi(swapChainSurface))),
                            "ICompositorInterop::CreateCompositionSurfaceForSwapChain");
        const composition::CompositionSurfaceBrush contentBrush = compositor.CreateSurfaceBrush(swapChainSurface);
        // Drawn at its own size from the top left, never stretched while a resize settles.
        contentBrush.Stretch(composition::CompositionStretch::None);
        contentBrush.HorizontalAlignmentRatio(0.0f);
        contentBrush.VerticalAlignmentRatio(0.0f);
        _visuals->content = compositor.CreateSpriteVisual();
        _visuals->content.Brush(contentBrush);
        _visuals->content.Size({static_cast<float>(width), static_cast<float>(height)});
        _visuals->backdropLayer = compositor.CreateContainerVisual();
        const composition::ContainerVisual root = compositor.CreateContainerVisual();
        root.Children().InsertAtTop(_visuals->backdropLayer);
        root.Children().InsertAtTop(_visuals->content);
        _visuals->target.Root(root);
    });
    BindRenderTarget();
}

Surface::~Surface() = default;

ID2D1DeviceContext* Surface::Context() const {
    return _context.Get();
}

void Surface::Resize(std::uint32_t width, std::uint32_t height, std::uint32_t dpi) {
    // ResizeBuffers fails while D2D still holds a bitmap over the old back buffer.
    _context->SetTarget(nullptr);
    ThrowIfDeviceFailed(_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "IDXGISwapChain1::ResizeBuffers");
    Composing("Surface::Resize", [this, width, height] { _visuals->content.Size({static_cast<float>(width), static_cast<float>(height)}); });
    if (dpi != _dpi) {
        // The same DIPs land on other pixels, so the next ShowBackdrops places every backdrop again.
        for (BackdropVisual& backdrop : _visuals->backdrops) {
            backdrop.box = std::nullopt;
        }
    }
    _dpi = dpi;
    BindRenderTarget();
}

void Surface::EndDrawAndPresent() {
    ThrowIfDeviceFailed(_context->EndDraw(), "ID2D1DeviceContext::EndDraw");
    // Frames are paced by the bar's frame clock. A present that waited for the refresh here would hold the UI thread
    // past the clock's next tick, and the frame it would have drawn is dropped.
    ThrowIfDeviceFailed(_swapChain->Present(0, 0), "IDXGISwapChain1::Present");
}

std::uint32_t Surface::LastPresentCount() const {
    UINT count = 0;
    ThrowIfDeviceFailed(_swapChain->GetLastPresentCount(&count), "IDXGISwapChain1::GetLastPresentCount");
    return count;
}

std::optional<ShownPresent> Surface::LatestShown() const {
    DXGI_FRAME_STATISTICS statistics{};
    const HRESULT read = _swapChain->GetFrameStatistics(&statistics);
    if (read == DXGI_ERROR_FRAME_STATISTICS_DISJOINT) {
        return std::nullopt;
    }
    ThrowIfDeviceFailed(read, "IDXGISwapChain1::GetFrameStatistics");
    return ShownPresent{.present = statistics.PresentCount, .refresh = statistics.PresentRefreshCount};
}

void Surface::ShowBackdrops(const std::vector<D2D1_ROUNDED_RECT>& outlines, const Backdrop& backdrop) {
    const std::vector<D2D1_ROUNDED_RECT> boxes = SystemBlurBoxes(outlines, backdrop);
    Composing("Surface::ShowBackdrops", [this, &boxes, &backdrop] {
        std::vector<BackdropVisual>& backdrops = _visuals->backdrops;
        const bool retoned = _visuals->tone != backdrop;
        // A new tone takes new brushes, so every backdrop is made again.
        while (!backdrops.empty() && (retoned || backdrops.size() > boxes.size())) {
            _visuals->backdropLayer.Children().Remove(backdrops.back().sprite);
            backdrops.pop_back();
        }
        _visuals->tone = backdrop;
        while (backdrops.size() < boxes.size()) {
            backdrops.push_back(CreateBackdrop(*_composition, backdrop));
            _visuals->backdropLayer.Children().InsertAtTop(backdrops.back().sprite);
        }
        // Only a box that moved or changed size redraws its mask, so a bar at rest makes no composition change.
        for (size_t index = 0; index < boxes.size(); ++index) {
            const std::optional<D2D1_ROUNDED_RECT>& placed = backdrops[index].box;
            if (!placed.has_value() || !SameBox(*placed, boxes[index])) {
                PlaceBackdrop(backdrops[index], boxes[index], _dpi);
            }
        }
    });
}

void Surface::BindRenderTarget() {
    ComPtr<IDXGISurface> surface;
    ThrowIfDeviceFailed(_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface)), "IDXGISwapChain1::GetBuffer");

    const auto pixelsPerInch = static_cast<float>(_dpi);
    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                                                       D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), pixelsPerInch, pixelsPerInch);
    ComPtr<ID2D1Bitmap1> target;
    ThrowIfDeviceFailed(_context->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &target), "ID2D1DeviceContext::CreateBitmapFromDxgiSurface");
    _context->SetTarget(target.Get());
    _context->SetDpi(pixelsPerInch, pixelsPerInch);
}
