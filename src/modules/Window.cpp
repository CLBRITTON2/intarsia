#include "modules/Window.hpp"

#include "modules/Placeholders.hpp"
#include "platform/Text.hpp"

#include <utility>
#include <vector>

namespace {

    constexpr wchar_t kEllipsis = L'…';

    bool IsHighSurrogate(wchar_t unit) {
        return unit >= 0xD800 && unit <= 0xDBFF;
    }

    // The values a window tooltip can name: {title}, the whole title.
    std::vector<Placeholder> WindowPlaceholders(std::wstring_view title) {
        return {{.name = "title", .value = ToUtf8(title)}};
    }

} // namespace

Window::Window(WindowConfig config, const IWindowManager& windowManager) : IconTextModule(std::move(config)), _windowManager(windowManager) {
    RequireOptionalPlaceholders("window tooltip", Settings().tooltip, WindowPlaceholders(L""));
}

ModuleText Window::Content() {
    const std::optional<FocusedWindow> focused = _windowManager.Focused();
    if (!focused.has_value()) {
        return NormalText(L"", L"");
    }
    return NormalText(L"", TruncateTitle(focused->title, Settings().maxLength));
}

// The whole title, however long, since the bar may have cut it.
std::optional<std::wstring> Window::TooltipText() {
    const std::optional<FocusedWindow> focused = _windowManager.Focused();
    if (!Settings().tooltip.has_value() || !focused.has_value()) {
        return std::nullopt;
    }
    return FormatPlaceholders(*Settings().tooltip, WindowPlaceholders(focused->title));
}

std::wstring TruncateTitle(std::wstring_view title, size_t maxLength) {
    if (title.size() <= maxLength) {
        return std::wstring(title);
    }
    size_t kept = maxLength - 1;
    if (kept > 0 && IsHighSurrogate(title[kept - 1])) {
        --kept;
    }
    return std::wstring(title.substr(0, kept)) + kEllipsis;
}
