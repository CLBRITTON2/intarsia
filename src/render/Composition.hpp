#pragma once

#include "config/Config.hpp"
#include "render/Graphics.hpp"

#include <unknwn.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Composition.h>

#include <array>
#include <string_view>
#include <utility>

// The compositor every window's visuals belong to, drawing with the graphics device's D2D device. Its objects are
// created on the UI thread and used only there.
struct CompositionDevice {
    winrt::Windows::UI::Composition::Compositor                compositor{nullptr};
    winrt::Windows::UI::Composition::CompositionGraphicsDevice graphics{nullptr};
};

// Needs a CompositionThread on the calling thread.
CompositionDevice CreateCompositionDevice(ID2D1Device* d2d);

// D2D's 5x4 color matrix (rows red, green, blue, alpha and offset, columns the output's channels) that scales a
// backdrop by its brightness and saturation as CSS's brightness() and saturate() do.
std::array<float, 20> BackdropColorMatrix(const Backdrop& backdrop);

// The host backdrop (source "backdrop") toned by backdrop's color matrix and cut to a mask surface's alpha (source
// "mask"), which antialiases a rounded edge that a geometric clip leaves stepped. A mask brush refuses a host backdrop
// as its source, and a color matrix is no property a brush can set, so each tone takes a factory of its own.
winrt::Windows::UI::Composition::CompositionEffectFactory CreateBackdropFactory(const CompositionDevice& device, const Backdrop& backdrop);

// Runs a composition call, whose failures C++/WinRT throws as winrt::hresult_error, and throws them as
// ThrowIfDeviceFailed does.
template <typename Call>
auto Composing(std::string_view call, Call&& body) {
    try {
        return std::forward<Call>(body)();
    } catch (const winrt::hresult_error& error) {
        ThrowIfDeviceFailed(error.code(), call);
        throw;
    }
}
