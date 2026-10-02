#include "render/Composition.hpp"

#include "platform/Errors.hpp"

#include <unknwn.h>

#include <d2d1effects_2.h>
#include <windows.graphics.effects.interop.h>
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Graphics.Effects.h>

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

    namespace effects    = winrt::Windows::Graphics::Effects;
    namespace foundation = winrt::Windows::Foundation;
    namespace abi        = ABI::Windows::Graphics::Effects;

    // One D2D effect for the compositor: its CLSID, its sources in order, and its properties by D2D property index.
    struct D2DEffect : winrt::implements<D2DEffect, effects::IGraphicsEffect, effects::IGraphicsEffectSource, abi::IGraphicsEffectD2D1Interop> {
        D2DEffect(const GUID& id, const std::vector<effects::IGraphicsEffectSource>& sources, const std::vector<foundation::IInspectable>& properties) :
            _id(id), _sources(sources), _properties(properties) {}

        winrt::hstring Name() const {
            return _name;
        }
        void Name(const winrt::hstring& name) {
            _name = name;
        }

        HRESULT __stdcall GetEffectId(GUID* id) noexcept override {
            *id = _id;
            return S_OK;
        }
        HRESULT __stdcall GetNamedPropertyMapping(LPCWSTR, UINT*, abi::GRAPHICS_EFFECT_PROPERTY_MAPPING*) noexcept override {
            return E_INVALIDARG;
        }
        HRESULT __stdcall GetPropertyCount(UINT* count) noexcept override {
            *count = static_cast<UINT>(_properties.size());
            return S_OK;
        }
        HRESULT __stdcall GetProperty(UINT index, ABI::Windows::Foundation::IPropertyValue** value) noexcept override {
            if (index >= _properties.size()) {
                return E_BOUNDS;
            }
            _properties[index].as<ABI::Windows::Foundation::IPropertyValue>().copy_to(value);
            return S_OK;
        }
        HRESULT __stdcall GetSource(UINT index, abi::IGraphicsEffectSource** source) noexcept override {
            if (index >= _sources.size()) {
                return E_BOUNDS;
            }
            _sources[index].as<abi::IGraphicsEffectSource>().copy_to(source);
            return S_OK;
        }
        HRESULT __stdcall GetSourceCount(UINT* count) noexcept override {
            *count = static_cast<UINT>(_sources.size());
            return S_OK;
        }

      private:
        GUID                                        _id;
        winrt::hstring                              _name;
        std::vector<effects::IGraphicsEffectSource> _sources;
        std::vector<foundation::IInspectable>       _properties;
    };

    // C++/WinRT throws rather than hand back a null factory, but its factory cache branches on one, and clang's analyzer
    // follows that branch into a call through null. A factory fetched and checked here leaves it no such path.
    template <typename Class, typename Interface>
    Interface FactoryOf(std::string_view call) {
        Interface factory = winrt::get_activation_factory<Class, Interface>();
        if (!factory) {
            throw Win32Error(call, E_POINTER);
        }
        return factory;
    }

} // namespace

CompositionDevice CreateCompositionDevice(ID2D1Device* d2d) {
    return Composing("CreateCompositionDevice", [d2d] {
        CompositionDevice device;
        device.compositor = winrt::Windows::UI::Composition::Compositor();
        ThrowIfDeviceFailed(device.compositor.as<ABI::Windows::UI::Composition::ICompositorInterop>()->CreateGraphicsDevice(
                                d2d, reinterpret_cast<ABI::Windows::UI::Composition::ICompositionGraphicsDevice**>(winrt::put_abi(device.graphics))),
                            "ICompositorInterop::CreateGraphicsDevice");
        return device;
    });
}

// CSS's saturate() matrix (Filter Effects, feColorMatrix type saturate), every channel then scaled by brightness.
std::array<float, 20> BackdropColorMatrix(const Backdrop& backdrop) {
    const float s = backdrop.saturation;
    const float b = backdrop.brightness;
    // clang-format off
    return {
        b * (0.213f + (0.787f * s)), b * (0.213f - (0.213f * s)), b * (0.213f - (0.213f * s)), 0.0f,
        b * (0.715f - (0.715f * s)), b * (0.715f + (0.285f * s)), b * (0.715f - (0.715f * s)), 0.0f,
        b * (0.072f - (0.072f * s)), b * (0.072f - (0.072f * s)), b * (0.072f + (0.928f * s)), 0.0f,
        0.0f,                        0.0f,                        0.0f,                        1.0f,
        0.0f,                        0.0f,                        0.0f,                        0.0f,
    };
    // clang-format on
}

winrt::Windows::UI::Composition::CompositionEffectFactory CreateBackdropFactory(const winrt::Windows::UI::Composition::Compositor& compositor, const Backdrop& backdrop) {
    namespace composition = winrt::Windows::UI::Composition;
    return Composing("CreateBackdropFactory", [&compositor, &backdrop] {
        const auto values  = FactoryOf<foundation::PropertyValue, foundation::IPropertyValueStatics>("RoGetActivationFactory(PropertyValue)");
        const auto sources = FactoryOf<composition::CompositionEffectSourceParameter, composition::ICompositionEffectSourceParameterFactory>(
            "RoGetActivationFactory(CompositionEffectSourceParameter)");
        const std::array<float, 20> matrix = BackdropColorMatrix(backdrop);
        const auto                  toned  = winrt::make<D2DEffect>(CLSID_D2D1ColorMatrix, std::vector<effects::IGraphicsEffectSource>{sources.Create(L"backdrop")},
                                                                    std::vector<foundation::IInspectable>{
                                                      values.CreateSingleArray(matrix),
                                                      values.CreateUInt32(static_cast<std::uint32_t>(D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED)),
                                                      values.CreateBoolean(true),
                                                  });
        const auto                  masked =
            winrt::make<D2DEffect>(CLSID_D2D1AlphaMask, std::vector<effects::IGraphicsEffectSource>{toned, sources.Create(L"mask")}, std::vector<foundation::IInspectable>{});
        return compositor.CreateEffectFactory(masked);
    });
}

winrt::Windows::UI::Composition::CompositionEffectFactory BackdropFactories::For(const winrt::Windows::UI::Composition::Compositor& compositor, const Backdrop& backdrop) {
    const auto found = std::ranges::find(_factories, backdrop, &std::pair<Backdrop, winrt::Windows::UI::Composition::CompositionEffectFactory>::first);
    if (found != _factories.end()) {
        return found->second;
    }
    _factories.emplace_back(backdrop, CreateBackdropFactory(compositor, backdrop));
    return _factories.back().second;
}
