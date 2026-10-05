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
        return Along(Axis::Horizontal, 24.0f);
    }

    // For a bar along axis whose module boxes are boxCrossDips across it.
    RenderContext Along(Axis axis, float boxCrossDips) const {
        return RenderContext{
            .context      = nullptr,
            .dwrite       = dwrite.Get(),
            .textFormat   = format.Get(),
            .iconFont     = L"Segoe UI",
            .iconSizeDips = 12.0f,
            .iconGapDips  = 4.0f,
            .foreground   = nullptr,
            .accent       = nullptr,
            .alert        = nullptr,
            .warning      = {.text = nullptr, .icon = nullptr},
            .critical     = {.text = nullptr, .icon = nullptr},
            .muted        = {.text = nullptr, .icon = nullptr},
            .classes      = {},
            .brushes      = nullptr,
            .boxes        = nullptr,
            .axis         = axis,
            .boxCrossDips = boxCrossDips,
        };
    }
};
