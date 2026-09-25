#include "modules/Window.hpp"

#include <utility>

namespace {

    constexpr wchar_t kEllipsis = L'…';

    bool              IsHighSurrogate(wchar_t unit) {
        return unit >= 0xD800 && unit <= 0xDBFF;
    }

} // namespace

Window::Window(WindowConfig config, const IWindowManager& windowManager) :
    _config(config), _windowManager(windowManager), _text{.layout = nullptr, .size = {.width = 0.0f, .height = 0.0f}} {}

void Window::Layout(const RenderContext& render) {
    const std::optional<FocusedWindow> focused = _windowManager.Focused();
    _text = LayOutIconText(render, L"", focused.has_value() ? TruncateTitle(focused->title, _config.maxLength) : std::wstring(), render.foreground);
}

D2D1_SIZE_F Window::Size() const {
    return _text.size;
}

void Window::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    render.context->DrawTextLayout(origin, _text.layout.Get(), render.foreground);
}

std::optional<UINT> Window::MillisecondsUntilChange() const {
    return std::nullopt;
}

void                 Window::Click(D2D1_POINT_2F) {}

const ModuleActions& Window::Actions() const {
    return _config.actions;
}

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
