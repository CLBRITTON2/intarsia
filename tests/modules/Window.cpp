#include "modules/Window.hpp"

#include "platform/Errors.hpp"

#include <dwrite.h>
#include <wrl/client.h>

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace {

    class FakeWindowManager final : public IWindowManager {
      public:
        std::optional<FocusedWindow> focused;

        std::optional<FocusedWindow> Focused() const override {
            return focused;
        }
        std::vector<Workspace> Workspaces() const override {
            return {};
        }
        std::vector<ManagedWindow> Windows() const override {
            return {};
        }
        WindowManagerStatus Status() const override {
            return WindowManagerStatus{.name = L"fake", .connection = Connection::Connected, .detail = ""};
        }
        void FocusWorkspace(const std::string&) override {}
        void FocusWindow(const std::string&) override {}
    };

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
            return RenderContext{
                .context = nullptr, .dwrite = dwrite.Get(), .textFormat = format.Get(), .foreground = nullptr, .accent = nullptr, .alert = nullptr, .iconSizeDips = 12.0f};
        }
    };

} // namespace

TEST(Window, LayoutReportsAChangeOnlyWhenTheTitleChanges) {
    FakeWindowManager    windowManager;
    const TextOnlyRender render;
    Window               window(WindowConfig{.maxLength = 50,
                                             .tooltip   = std::nullopt,
                                             .actions   = {.click = std::nullopt, .rightClick = std::nullopt, .scrollUp = std::nullopt, .scrollDown = std::nullopt}},
                                windowManager);

    windowManager.focused = FocusedWindow{.title = L"one"};
    EXPECT_TRUE(window.Layout(render.Context()));
    EXPECT_FALSE(window.Layout(render.Context()));
    windowManager.focused = FocusedWindow{.title = L"two"};
    EXPECT_TRUE(window.Layout(render.Context()));
    windowManager.focused = std::nullopt;
    EXPECT_TRUE(window.Layout(render.Context()));
    EXPECT_FALSE(window.Layout(render.Context()));
}

TEST(Window, TitleWithinTheLimitIsUnchanged) {
    EXPECT_EQ(TruncateTitle(L"intarsia", 8), L"intarsia");
}

TEST(Window, LongTitleEndsInAnEllipsisAtTheLimit) {
    const std::wstring title = TruncateTitle(L"intarsia - Visual Studio Code", 10);
    EXPECT_EQ(title, L"intarsia …");
    EXPECT_EQ(title.size(), 10u);
}

TEST(Window, TruncationNeverSplitsASurrogatePair) {
    // "ab" then U+F1441 as a surrogate pair then "cd": a cut after the high surrogate drops the whole pair.
    const std::wstring title = L"ab\U000F1441cd";
    EXPECT_EQ(TruncateTitle(title, 4), L"ab…");
}

TEST(Window, ZeroLimitGivesAnEmptyTitle) {
    EXPECT_TRUE(TruncateTitle(L"intarsia", 0).empty());
}
