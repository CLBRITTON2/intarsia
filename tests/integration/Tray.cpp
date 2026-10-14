#include "Harness.hpp"
#include "TrayApp.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <string_view>
#include <vector>

using testing::ElementsAre;

namespace {

    constexpr std::string_view kConfig = R"(
return {
    bar = { edge = "top", layer = "top", space = "share", thickness = 40, monitors = { "*" } },
    window_manager = "glazewm",
    modules_left = { "tray" },
    modules_center = {},
    modules_right = {},
    layouts = {},
    modules = { tray = { type = "tray", icon = "T" } },
}
)";

    constexpr std::uint32_t    kToggleColor = 0x8E44AD;

    // Icons at 16 DIPs, the size the test icons are made at, and no transitions, so a fold is done on the next frame.
    constexpr std::string_view kStyle = R"(
bar {
  font: Segoe UI
  size: 13
  color: #e0def4
}
section {
  margin: 4
  padding: 3
  background: #191724
}
tray {
  icon {
    size: 16
  }
  item {
    padding: 4
    margin: 0 1
    toggle {
      background: #8e44ad
    }
  }
}
)";

    constexpr std::uint32_t    kGreen  = 0x27AE60;
    constexpr std::uint32_t    kOrange = 0xF39C12;
    constexpr auto             kDrawn  = std::chrono::seconds(5);
    // The tray drops the icons of closed owners every 5 s.
    constexpr auto kSwept = std::chrono::seconds(7);

    // Clicks the middle of the pixels drawn in rgb. False when none are drawn within kDrawn.
    bool ClickColor(const integration::RunningBar& bar, std::uint32_t rgb) {
        const std::optional<std::int32_t> column = bar.DrawnColumn(rgb, kDrawn);
        if (!column.has_value()) {
            return false;
        }
        bar.ClickAt(*column);
        return true;
    }

    std::vector<LPARAM> LParams(const std::vector<TrayInputMessage>& messages, std::uint32_t id) {
        std::vector<LPARAM> lParams;
        for (const TrayInputMessage& message : messages) {
            integration::Log("tray_received", std::format("id={} message={:#x} wparam={:#x} lparam={:#x}", id, message.message, message.wParam, message.lParam));
            lParams.push_back(message.lParam);
        }
        return lParams;
    }

    std::vector<WPARAM> WParams(const std::vector<TrayInputMessage>& messages) {
        std::vector<WPARAM> wParams;
        std::ranges::transform(messages, std::back_inserter(wParams), &TrayInputMessage::wParam);
        return wParams;
    }

    LPARAM Version4Event(std::uint32_t event, std::uint32_t id) {
        return MAKELPARAM(static_cast<WORD>(event), static_cast<WORD>(id));
    }

} // namespace

TEST(Tray, PostsAClickOnAnIconToItsCallbackInTheFormOfItsVersion) {
    const integration::RunningBar bar("tray-click", kConfig, kStyle);
    const integration::TrayApp    current(1, NOTIFYICON_VERSION_4);
    const integration::TrayApp    legacy(2, 0);
    ASSERT_TRUE(current.Add(kGreen));
    ASSERT_TRUE(legacy.Add(kOrange));
    ASSERT_TRUE(ClickColor(bar, kToggleColor));

    ASSERT_TRUE(ClickColor(bar, kGreen)) << "the version 4 icon never drew";
    ASSERT_TRUE(ClickColor(bar, kOrange)) << "the version 0 icon never drew";
    integration::Eventually([&] { return current.Received().size() >= 3 && legacy.Received().size() >= 2; }, kDrawn);

    EXPECT_THAT(LParams(current.Received(), 1), ElementsAre(Version4Event(WM_LBUTTONDOWN, 1), Version4Event(WM_LBUTTONUP, 1), Version4Event(NIN_SELECT, 1)));
    EXPECT_THAT(LParams(legacy.Received(), 2), ElementsAre(WM_LBUTTONDOWN, WM_LBUTTONUP));
    EXPECT_THAT(WParams(legacy.Received()), ElementsAre(WPARAM{2}, WPARAM{2}));
}

TEST(Tray, PostsARightClickOnAnIconToItsCallbackInTheFormOfItsVersion) {
    const integration::RunningBar bar("tray-right-click", kConfig, kStyle);
    const integration::TrayApp    current(1, NOTIFYICON_VERSION_4);
    const integration::TrayApp    legacy(2, 0);
    ASSERT_TRUE(current.Add(kGreen));
    ASSERT_TRUE(legacy.Add(kOrange));
    ASSERT_TRUE(ClickColor(bar, kToggleColor));

    const std::optional<std::int32_t> green = bar.DrawnColumn(kGreen, kDrawn);
    ASSERT_TRUE(green.has_value()) << "the version 4 icon never drew";
    bar.ButtonUpAt(WM_RBUTTONUP, green.value_or(0));
    const std::optional<std::int32_t> orange = bar.DrawnColumn(kOrange, kDrawn);
    ASSERT_TRUE(orange.has_value()) << "the version 0 icon never drew";
    bar.ButtonUpAt(WM_RBUTTONUP, orange.value_or(0));
    integration::Eventually([&] { return current.Received().size() >= 3 && legacy.Received().size() >= 2; }, kDrawn);

    EXPECT_THAT(LParams(current.Received(), 1), ElementsAre(Version4Event(WM_RBUTTONDOWN, 1), Version4Event(WM_RBUTTONUP, 1), Version4Event(WM_CONTEXTMENU, 1)));
    EXPECT_THAT(LParams(legacy.Received(), 2), ElementsAre(WM_RBUTTONDOWN, WM_RBUTTONUP));
}

TEST(Tray, AnswersTheRectOfAnIconOnTheBar) {
    const integration::RunningBar bar("tray-rect", kConfig, kStyle);
    const integration::TrayApp    app(1, NOTIFYICON_VERSION_4);
    ASSERT_TRUE(app.Add(kGreen));
    EXPECT_FALSE(app.IconRect().has_value()) << "the tray answered a rect for an icon no bar shows";

    ASSERT_TRUE(ClickColor(bar, kToggleColor));
    std::optional<double> column;
    ASSERT_TRUE(integration::Eventually(
        [&] {
            column = bar.MeanColumnOf(kGreen);
            return column.has_value() && app.IconRect().has_value();
        },
        kDrawn))
        << "the icon never drew with a rect";
    const RECT rect    = app.IconRect().value_or(RECT{});
    const RECT barRect = integration::ScreenRect(bar.PrimaryBar());
    integration::Log("icon_rect",
                     std::format("left={} top={} right={} bottom={} bar_left={} bar_top={} bar_bottom={} icon_column={:.1f}", rect.left, rect.top, rect.right, rect.bottom,
                                 barRect.left, barRect.top, barRect.bottom, column.value_or(-1)));
    EXPECT_LE(rect.left - barRect.left, column.value_or(-1));
    EXPECT_GE(rect.right - barRect.left, column.value_or(-1));
    EXPECT_GE(rect.top, barRect.top);
    EXPECT_LE(rect.bottom, barRect.bottom);
    EXPECT_GT(rect.bottom, rect.top);
}

TEST(Tray, DrawsTheNewPixelsOfAModifiedIcon) {
    const integration::RunningBar bar("tray-modify", kConfig, kStyle);
    const integration::TrayApp    app(1, NOTIFYICON_VERSION_4);
    ASSERT_TRUE(app.Add(kGreen));
    ASSERT_TRUE(ClickColor(bar, kToggleColor));
    ASSERT_TRUE(bar.Draws(kGreen, kDrawn));

    ASSERT_TRUE(app.Modify(kOrange));
    EXPECT_TRUE(bar.Draws(kOrange, kDrawn)) << "the modified icon's new color never drew";
    EXPECT_TRUE(bar.StopsDrawing(kGreen, kDrawn)) << "the icon's old color still drew";
}

TEST(Tray, RemovesADeletedIcon) {
    const integration::RunningBar bar("tray-delete", kConfig, kStyle);
    const integration::TrayApp    app(1, NOTIFYICON_VERSION_4);
    ASSERT_TRUE(app.Add(kGreen));
    ASSERT_TRUE(ClickColor(bar, kToggleColor));
    ASSERT_TRUE(bar.Draws(kGreen, kDrawn));

    ASSERT_TRUE(app.Delete());
    EXPECT_TRUE(bar.StopsDrawing(kGreen, kDrawn)) << "the deleted icon still drew";
}

TEST(Tray, RemovesTheIconOfAClosedOwner) {
    const integration::RunningBar bar("tray-owner", kConfig, kStyle);
    integration::TrayApp          app(1, NOTIFYICON_VERSION_4);
    ASSERT_TRUE(app.Add(kGreen));
    ASSERT_TRUE(ClickColor(bar, kToggleColor));
    ASSERT_TRUE(bar.Draws(kGreen, kDrawn));

    app.CloseOwner();
    EXPECT_TRUE(bar.StopsDrawing(kGreen, kSwept)) << "the icon of a closed owner still drew";
}
