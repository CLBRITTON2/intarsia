#pragma once

#include "config/Config.hpp"
#include "render/Graphics.hpp"

#include <unknwn.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Composition.h>

#include <array>
#include <string_view>
#include <utility>
#include <vector>

// D2D's 5x4 color matrix (rows red, green, blue, alpha and offset, columns the output's channels) that scales a
// backdrop by its brightness and saturation as CSS's brightness() and saturate() do.
std::array<float, 20> BackdropColorMatrix(const Backdrop& backdrop);

// One backdrop factory per tone for every surface on a compositor, since each compiles an effect graph and a tooltip or
// menu makes a surface per show. A config holds a few tones, and the compositor's lifetime bounds them.
class BackdropFactories {
  public:
    winrt::Windows::UI::Composition::CompositionEffectFactory For(const winrt::Windows::UI::Composition::Compositor& compositor, const Backdrop& backdrop);

  private:
    std::vector<std::pair<Backdrop, winrt::Windows::UI::Composition::CompositionEffectFactory>> _factories;
};

// The compositor every window's visuals belong to, drawing with the graphics device's D2D device. Its objects are
// created on the UI thread and used only there.
struct CompositionDevice {
    winrt::Windows::UI::Composition::Compositor                compositor{nullptr};
    winrt::Windows::UI::Composition::CompositionGraphicsDevice graphics{nullptr};
    BackdropFactories                                          backdrops;
};

// Needs a CompositionThread on the calling thread.
CompositionDevice CreateCompositionDevice(ID2D1Device* d2d);

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
