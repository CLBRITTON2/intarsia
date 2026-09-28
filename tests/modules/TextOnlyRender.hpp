#pragma once

#include "platform/Errors.hpp"
#include "render/RenderContext.hpp"

#include <dwrite.h>
#include <wrl/client.h>

// Enough to lay text out: nothing here draws, so there is no device context or brush.
struct TextOnlyRender {
    Microsoft::WRL::ComPtr<IDWriteFactory>    dwrite;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;

    TextOnlyRender() {
        ThrowIfFailed(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &dwrite), "DWriteCreateFactory");
        ThrowIfFailed(dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &format),
                      "CreateTextFormat");
    }

    RenderContext Context() const {
        return RenderContext{.context       = nullptr,
                             .dwrite        = dwrite.Get(),
                             .textFormat    = format.Get(),
                             .iconFont      = L"Segoe UI",
                             .iconSizeDips  = 12.0f,
                             .iconGapDips   = 4.0f,
                             .foreground    = nullptr,
                             .accent        = nullptr,
                             .alert         = nullptr,
                             .warning       = {.text = nullptr, .icon = nullptr},
                             .critical      = {.text = nullptr, .icon = nullptr},
                             .brushes       = nullptr,
                             .boxHeightDips = 24.0f};
    }
};
