#include "modules/Window.hpp"

#include "platform/Text.hpp"

#include <cstdint>
#include <utility>

namespace {

    constexpr wchar_t kEllipsis = L'…';

    bool              IsHighSurrogate(wchar_t unit) {
        return unit >= 0xD800 && unit <= 0xDBFF;
    }

} // namespace

Window::Window(WindowConfig config, const IWindowManager& windowManager) : _config(std::move(config)), _windowManager(windowManager) {
    RequireOptionalPlaceholders("window tooltip", _config.tooltip, WindowPlaceholders(L""));
}

bool Window::Layout(const RenderContext& render) {
    const std::optional<FocusedWindow> focused = _windowManager.Focused();
    return _line.Update(render, ModuleText{.icon = {}, .text = focused.has_value() ? TruncateTitle(focused->title, _config.maxLength) : std::wstring(), .alert = false});
}

D2D1_SIZE_F Window::Size() const {
    return _line.Size();
}

void Window::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    _line.Draw(render, origin);
}

const ModuleActions& Window::Actions() const {
    return _config.actions;
}

// The whole title, however long, since the bar may have cut it.
std::optional<std::wstring> Window::TooltipText() {
    const std::optional<FocusedWindow> focused = _windowManager.Focused();
    if (!_config.tooltip.has_value() || !focused.has_value()) {
        return std::nullopt;
    }
    return ToWide(FormatPlaceholders(*_config.tooltip, WindowPlaceholders(focused->title)));
}

std::vector<Placeholder> WindowPlaceholders(std::wstring_view title) {
    return {{.name = "title", .value = ToUtf8(title)}};
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
