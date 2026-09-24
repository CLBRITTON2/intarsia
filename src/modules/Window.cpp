#include "modules/Window.hpp"

#include "platform/Errors.hpp"

#include <limits>
#include <utility>

namespace {

    constexpr wchar_t kEllipsis = L'…';

    bool              IsHighSurrogate(wchar_t unit) {
        return unit >= 0xD800 && unit <= 0xDBFF;
    }

} // namespace

Window::Window(WindowConfig config, const IWindowManager& windowManager) : config_(config), windowManager_(windowManager), size_{.width = 0.0f, .height = 0.0f} {}

void Window::Layout(const RenderContext& render) {
    const std::optional<FocusedWindow> focused = windowManager_.Focused();
    const std::wstring                 text    = focused.has_value() ? TruncateTitle(focused->title, config_.maxLength) : std::wstring();

    layout_.Reset();
    ThrowIfFailed(render.dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), render.textFormat, std::numeric_limits<float>::max(),
                                                  std::numeric_limits<float>::max(), &layout_),
                  "IDWriteFactory::CreateTextLayout");
    ThrowIfFailed(layout_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP), "IDWriteTextLayout::SetWordWrapping");

    DWRITE_TEXT_METRICS metrics{};
    ThrowIfFailed(layout_->GetMetrics(&metrics), "IDWriteTextLayout::GetMetrics");
    size_ = D2D1_SIZE_F{.width = metrics.widthIncludingTrailingWhitespace, .height = metrics.height};
}

D2D1_SIZE_F Window::Size() const {
    return size_;
}

void Window::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    render.context->DrawTextLayout(origin, layout_.Get(), render.foreground);
}

std::optional<UINT> Window::MillisecondsUntilChange() const {
    return std::nullopt;
}

void         Window::Click(D2D1_POINT_2F) {}

std::wstring TruncateTitle(std::wstring_view title, size_t maxLength) {
    if (title.size() <= maxLength) {
        return std::wstring(title);
    }
    if (maxLength == 0) {
        return {};
    }
    size_t kept = maxLength - 1;
    if (kept > 0 && IsHighSurrogate(title[kept - 1])) {
        --kept;
    }
    return std::wstring(title.substr(0, kept)) + kEllipsis;
}
