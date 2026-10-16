#include "config/Style.hpp"

#include "config/Config.hpp"
#include "support/ThrowsWith.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

    constexpr std::string_view kBar = "bar {\n  font: Some Font\n  size: 15\n  color: #102030\n}\n";

    StyleSheet                 Parse(std::string_view text) {
        return ParseStyleSheet(text, "style.conf");
    }

    // The bar block, then text.
    StyleSheet WithBar(std::string_view text) {
        return Parse(std::string(kBar) + std::string(text));
    }

    ModuleStyle StyleOf(std::string_view text, ModuleKind kind, std::string_view name) {
        const StyleSheet sheet = WithBar(text);
        return ResolveModuleStyle(sheet, ResolveBarStyle(sheet), kind, name, {});
    }

    // Channels as 0 to 255 so a failure reads like the style that caused it.
    std::array<std::int32_t, 4> Rgba(const D2D1_COLOR_F& color) {
        return {static_cast<std::int32_t>(std::lround(color.r * 255.0f)), static_cast<std::int32_t>(std::lround(color.g * 255.0f)),
                static_cast<std::int32_t>(std::lround(color.b * 255.0f)), static_cast<std::int32_t>(std::lround(color.a * 255.0f))};
    }

    constexpr std::array<std::int32_t, 4> kBarColor{0x10, 0x20, 0x30, 255};
    constexpr std::array<std::int32_t, 4> kRed{255, 0, 0, 255};
    constexpr std::array<std::int32_t, 4> kGreen{0, 255, 0, 255};
    constexpr std::array<std::int32_t, 4> kNone{0, 0, 0, 0};

} // namespace

TEST(Style, TheBarBlockSetsTheFontAndColorEveryModuleInherits) {
    const StyleSheet  sheet = Parse(kBar);
    const StyleConfig bar   = ResolveBarStyle(sheet);
    EXPECT_EQ(bar.font, L"Some Font");
    EXPECT_FLOAT_EQ(bar.fontSizeDips, 15.0f);
    const ModuleStyle clock = ResolveModuleStyle(sheet, bar, ModuleKind::Clock, "time", {});
    EXPECT_EQ(Rgba(clock.color), kBarColor);
    EXPECT_EQ(Rgba(clock.icon.color), kBarColor);
    EXPECT_EQ(Rgba(clock.item.color), kBarColor);
    EXPECT_EQ(clock.icon.font, L"Some Font");
    EXPECT_EQ(Rgba(clock.box.background), kNone);
}

TEST(Style, TheBarMustSetFontSizeAndColor) {
    EXPECT_THAT([] { return ResolveBarStyle(Parse("bar {\n  size: 15\n  color: #fff\n}\n")); }, ThrowsWith<ConfigError>("style.conf:1: bar must set font, as font: Segoe UI"));
    EXPECT_THAT([] { return ResolveBarStyle(Parse("")); }, ThrowsWith<ConfigError>("must have a bar block"));
    EXPECT_THAT([] { return Parse("bar {\n  font: \"Some Font\"\n}\n"); }, ThrowsWith<ConfigError>("bar: font takes the family unquoted"));
    EXPECT_THAT([] { return Parse("bar {\n  font: A, B\n}\n"); }, ThrowsWith<ConfigError>("one font family, not a list"));
    EXPECT_THAT([] { return Parse("bar {\n  padding: 2\n}\n"); }, ThrowsWith<ConfigError>("bar: padding is not a property bar takes, which are: font, size, color"));
}

TEST(Style, ColorsTakeHexFormsAnOpacityAndTransparent) {
    const std::string blocks = "a {\n  color: #abc\n}\nb {\n  color: #11223380\n}\nc {\n  color: #ff0000 50%\n}\nd {\n  color: #ff000080 50%\n}\ne {\n  color: transparent\n}\n";
    EXPECT_EQ(Rgba(StyleOf(blocks, ModuleKind::Cpu, "a").color), (std::array<std::int32_t, 4>{0xaa, 0xbb, 0xcc, 255}));
    EXPECT_EQ(Rgba(StyleOf(blocks, ModuleKind::Cpu, "b").color), (std::array<std::int32_t, 4>{0x11, 0x22, 0x33, 0x80}));
    EXPECT_EQ(Rgba(StyleOf(blocks, ModuleKind::Cpu, "c").color), (std::array<std::int32_t, 4>{255, 0, 0, 128}));
    EXPECT_EQ(Rgba(StyleOf(blocks, ModuleKind::Cpu, "d").color), (std::array<std::int32_t, 4>{255, 0, 0, 64}));
    EXPECT_EQ(Rgba(StyleOf(blocks, ModuleKind::Cpu, "e").color), kNone);
    EXPECT_THAT([] { return Parse("module {\n  color: red\n}\n"); }, ThrowsWith<ConfigError>("module: color must be a color"));
    EXPECT_THAT([] { return Parse("module {\n  color: #12345\n}\n"); }, ThrowsWith<ConfigError>("must be a color"));
    EXPECT_THAT([] { return Parse("module {\n  color: #12345g\n}\n"); }, ThrowsWith<ConfigError>("not a hex digit"));
    EXPECT_THAT([] { return Parse("module {\n  color: #fff 120%\n}\n"); }, ThrowsWith<ConfigError>("percentage from 0% to 100%"));
}

TEST(Style, VarsAreReadThroughTheirNames) {
    const ModuleStyle style = StyleOf("vars {\n  accent: #ff0000\n  dim: 50%\n}\ncpu {\n  color: $accent\n  background: $accent $dim\n}\n", ModuleKind::Cpu, "cpu");
    EXPECT_EQ(Rgba(style.color), kRed);
    EXPECT_EQ(Rgba(style.box.background), (std::array<std::int32_t, 4>{255, 0, 0, 128}));
    EXPECT_THAT([] { return WithBar("cpu {\n  color: $nope\n}\n"); }, ThrowsWith<ConfigError>("style.conf:7: color: $nope names no var"));
    EXPECT_THAT([] { return WithBar("cpu {\n  color: $\n}\n"); }, ThrowsWith<ConfigError>("$ must be followed by the name of a var"));
    EXPECT_THAT([] { return Parse("vars {\n  a: #fff\n  b: $a\n}\n"); }, ThrowsWith<ConfigError>("vars > b: a var cannot use another var"));
    EXPECT_THAT([] { return Parse("vars {\n  a {\n    b: #fff\n  }\n}\n"); }, ThrowsWith<ConfigError>("vars > a: vars holds name: value lines"));
}

TEST(Style, AModulesOwnBlockWinsOverTheModuleBlock) {
    const std::string blocks = "module {\n  color: #ff0000\n  padding: 2\n}\ncpu {\n  color: #00ff00\n}\n";
    const ModuleStyle cpu    = StyleOf(blocks, ModuleKind::Cpu, "cpu");
    EXPECT_EQ(Rgba(cpu.color), kGreen);
    EXPECT_FLOAT_EQ(cpu.box.padding.xDips, 2.0f);
    EXPECT_EQ(Rgba(StyleOf(blocks, ModuleKind::Cpu, "other").color), kRed);
}

TEST(Style, IconsAndItemsInheritTheModulesColor) {
    const ModuleStyle style = StyleOf("ws {\n  color: #ff0000\n  item {\n    focused {\n      color: #00ff00\n    }\n  }\n}\n", ModuleKind::Workspaces, "ws");
    EXPECT_EQ(Rgba(style.icon.color), kRed);
    EXPECT_EQ(Rgba(style.empty.color), kRed);
    EXPECT_EQ(Rgba(style.focused.color), kGreen);
}

TEST(Style, ItemPropertiesApplyToEveryStateAndAStatesToOne) {
    const std::string blocks = R"(
module {
  item {
    padding: 2 6
    margin: 0 1
    radius: 10
    focused {
      background: #9ccfd8
      weight: bold
    }
  }
}
ws {
  item {
    empty {
      color: #908caa
    }
  }
}
)";
    const ModuleStyle style  = StyleOf(blocks, ModuleKind::Workspaces, "ws");
    EXPECT_FLOAT_EQ(style.occupied.padding.yDips, 2.0f);
    EXPECT_FLOAT_EQ(style.occupied.padding.xDips, 6.0f);
    EXPECT_FLOAT_EQ(style.focused.margin.xDips, 1.0f);
    EXPECT_FLOAT_EQ(style.empty.radiusDips, 10.0f);
    EXPECT_EQ(style.focused.weight, DWRITE_FONT_WEIGHT_BOLD);
    EXPECT_EQ(style.occupied.weight, DWRITE_FONT_WEIGHT_NORMAL);
    EXPECT_EQ(Rgba(style.focused.background), (std::array<std::int32_t, 4>{0x9c, 0xcf, 0xd8, 255}));
    EXPECT_EQ(Rgba(style.occupied.background), kNone);
    EXPECT_EQ(Rgba(style.empty.color), (std::array<std::int32_t, 4>{0x90, 0x8c, 0xaa, 255}));
    // Another module's blocks stay with that module.
    EXPECT_EQ(Rgba(StyleOf(blocks, ModuleKind::Workspaces, "other").empty.color), kBarColor);
}

TEST(Style, TheTraysToggleTakesItsItemPropertiesAndThenItsOwn) {
    const std::string blocks = R"(
tray {
  item {
    padding: 4
    margin: 0 2
    radius: 6
    toggle {
      padding: 0 9
      radius: 10
    }
  }
}
)";
    const ModuleStyle style  = StyleOf(blocks, ModuleKind::Tray, "tray");
    EXPECT_FLOAT_EQ(style.toggle.padding.yDips, 0.0f);
    EXPECT_FLOAT_EQ(style.toggle.padding.xDips, 9.0f);
    EXPECT_FLOAT_EQ(style.toggle.radiusDips, 10.0f);
    EXPECT_FLOAT_EQ(style.toggle.margin.xDips, 2.0f);
    EXPECT_FLOAT_EQ(style.item.padding.xDips, 4.0f);
    EXPECT_FLOAT_EQ(style.item.radiusDips, 6.0f);
    EXPECT_THAT([] { return Parse("module {\n  item {\n    toggle {\n      focused {\n        color: #fff\n      }\n    }\n  }\n}\n"); },
                ThrowsWith<ConfigError>("module > item > toggle: focused is not a block module > item > toggle holds, which are: none"));
}

TEST(Style, HoverSetsTheBoxUnderTheCursorAndAlertTheTextThatNeedsAttention) {
    const ModuleStyle style =
        StyleOf("module {\n  background: #111111\n  alert {\n    color: #ff0000\n  }\n}\ncpu {\n  hover {\n    background: #222222\n  }\n}\n", ModuleKind::Cpu, "cpu");
    EXPECT_EQ(Rgba(style.box.background), (std::array<std::int32_t, 4>{0x11, 0x11, 0x11, 255}));
    EXPECT_EQ(Rgba(style.hover), (std::array<std::int32_t, 4>{0x22, 0x22, 0x22, 255}));
    EXPECT_EQ(Rgba(style.alertColor), kRed);
    EXPECT_EQ(Rgba(style.color), kBarColor);
    // With no hover block the box keeps its background under the cursor.
    EXPECT_EQ(Rgba(StyleOf("module {\n  background: #111111\n}\n", ModuleKind::Cpu, "cpu").hover), (std::array<std::int32_t, 4>{0x11, 0x11, 0x11, 255}));
    EXPECT_THAT([] { return Parse("cpu {\n  hover {\n    color: #fff\n  }\n}\n"); },
                ThrowsWith<ConfigError>("cpu > hover: color is not a property cpu > hover takes, which are: background"));
}

TEST(Style, StatesColorTheTextAndTheIconOverTheModulesOwn) {
    const std::string blocks = R"(
power {
  icon {
    color: #00ffff
  }
  warning {
    icon {
      color: #ffff00
    }
  }
  critical {
    color: #ff0000
  }
}
)";
    const ModuleStyle style  = StyleOf(blocks, ModuleKind::Battery, "power");
    EXPECT_EQ(Rgba(style.warning.icon), (std::array<std::int32_t, 4>{255, 255, 0, 255}));
    EXPECT_EQ(Rgba(style.warning.text), Rgba(style.color));
    EXPECT_EQ(Rgba(style.critical.text), kRed);
    // Nothing colors the critical icon, so the module's own icon color still applies to it.
    EXPECT_EQ(Rgba(style.critical.icon), (std::array<std::int32_t, 4>{0, 255, 255, 255}));
    const ModuleStyle bare = StyleOf("power {\n  critical {\n    color: #ff0000\n  }\n}\n", ModuleKind::Battery, "power");
    EXPECT_EQ(Rgba(bare.critical.icon), kRed);
    EXPECT_EQ(Rgba(bare.warning.icon), Rgba(bare.icon.color));
    EXPECT_EQ(Rgba(StyleOf("volume {\n  muted {\n    color: #ff0000\n  }\n}\n", ModuleKind::Audio, "volume").muted.text), kRed);
}

TEST(Style, OwnedStatesTakeColorOnlyAndBelongToTheirModuleType) {
    EXPECT_THAT([] { return Parse("power {\n  warning {\n    padding: 2\n  }\n}\n"); },
                ThrowsWith<ConfigError>("power > warning: padding is not a property power > warning takes, which are: color"));
    EXPECT_THAT([] { return Parse("power {\n  warning {\n    icon {\n      size: 20\n    }\n  }\n}\n"); }, ThrowsWith<ConfigError>("which are: color"));
    EXPECT_THAT([] { return Parse("module {\n  warning {\n    color: #fff\n  }\n}\n"); }, ThrowsWith<ConfigError>("module: warning is not a block module holds"));
    EXPECT_THAT([] { return StyleOf("cpu {\n  warning {\n    color: #fff\n  }\n}\n", ModuleKind::Cpu, "cpu"); },
                ThrowsWith<ConfigError>("cpu > warning: warning colors only battery modules, and cpu is a cpu module"));
    EXPECT_THAT([] { return Parse("cpu {\n  alert {\n    icon {\n      color: #fff\n    }\n  }\n}\n"); },
                ThrowsWith<ConfigError>("cpu > alert: icon is not a block cpu > alert holds"));
}

TEST(Style, ItemHoverChangesColorsAndLosesToAStatesOwnColors) {
    const std::string blocks = R"(
module {
  item {
    background: #111111
    hover {
      background: #222222
      color: #ffffff
    }
  }
}
ws {
  item {
    focused {
      background: #9ccfd8
    }
  }
}
)";
    const ModuleStyle style  = StyleOf(blocks, ModuleKind::Workspaces, "ws");
    EXPECT_EQ(Rgba(style.occupied.hoverBackground), (std::array<std::int32_t, 4>{0x22, 0x22, 0x22, 255}));
    EXPECT_EQ(Rgba(style.occupied.hoverColor), (std::array<std::int32_t, 4>{255, 255, 255, 255}));
    EXPECT_EQ(Rgba(style.occupied.background), (std::array<std::int32_t, 4>{0x11, 0x11, 0x11, 255}));
    EXPECT_EQ(Rgba(style.focused.hoverBackground), (std::array<std::int32_t, 4>{0x9c, 0xcf, 0xd8, 255}));
    // With no hover block an item keeps its colors under the cursor.
    const ModuleStyle plain = StyleOf("module {\n  item {\n    color: #ff0000\n  }\n}\n", ModuleKind::Windows, "apps");
    EXPECT_EQ(Rgba(plain.item.hoverColor), kRed);
    EXPECT_THAT([] { return Parse("module {\n  item {\n    hover {\n      padding: 2\n    }\n  }\n}\n"); }, ThrowsWith<ConfigError>("which are: color, background"));
    EXPECT_THAT([] { return Parse("module {\n  icon {\n    hover {\n      color: #fff\n    }\n  }\n}\n"); },
                ThrowsWith<ConfigError>("module > icon: hover is not a block module > icon holds"));
}

TEST(Style, TheMenuBlockStylesTheMenuItsEntriesAndTheActiveEntry) {
    const std::string blocks = R"(
menu {
  background: #111111
  color: #eeeeee
  padding: 6
  item {
    padding: 2 8
    hover {
      background: #222222
    }
    active {
      weight: bold
      color: #00ff00
      hover {
        color: #ffffff
      }
    }
  }
}
)";
    const MenuStyle   menu   = ResolveBarStyle(WithBar(blocks)).menu;
    EXPECT_EQ(Rgba(menu.box.background), (std::array<std::int32_t, 4>{0x11, 0x11, 0x11, 255}));
    EXPECT_FLOAT_EQ(menu.box.padding.xDips, 6.0f);
    EXPECT_EQ(Rgba(menu.item.color), (std::array<std::int32_t, 4>{0xee, 0xee, 0xee, 255}));
    EXPECT_FLOAT_EQ(menu.item.padding.xDips, 8.0f);
    EXPECT_EQ(Rgba(menu.item.hoverBackground), (std::array<std::int32_t, 4>{0x22, 0x22, 0x22, 255}));
    EXPECT_EQ(menu.active.weight, DWRITE_FONT_WEIGHT_BOLD);
    EXPECT_EQ(Rgba(menu.active.color), kGreen);
    EXPECT_EQ(Rgba(menu.active.hoverColor), (std::array<std::int32_t, 4>{255, 255, 255, 255}));
    EXPECT_EQ(Rgba(menu.active.hoverBackground), (std::array<std::int32_t, 4>{0x22, 0x22, 0x22, 255}));
    EXPECT_FLOAT_EQ(menu.active.padding.xDips, 8.0f);
    // With no menu block the entries take the bar's color.
    EXPECT_EQ(Rgba(ResolveBarStyle(Parse(kBar)).menu.item.color), kBarColor);
    EXPECT_THAT([] { return Parse("menu {\n  icon {\n    color: #fff\n  }\n}\n"); }, ThrowsWith<ConfigError>("menu: icon is not a block menu holds, which are: item, scrollbar"));
    EXPECT_THAT([] { return Parse("menu {\n  item {\n    transition: color 1s\n  }\n}\n"); }, ThrowsWith<ConfigError>("menu > item: transition is not a property"));
}

TEST(Style, TheMenuBlockCapsTheMenuAndStylesItsScrollBar) {
    const std::string blocks = "menu {\n  max-width: 480\n  max-height: 360\n  scrollbar {\n    width: 4\n    background: #333333\n    radius: 2\n    margin: 6 3\n  }\n}\n";
    const MenuStyle   menu   = ResolveBarStyle(WithBar(blocks)).menu;
    EXPECT_EQ(menu.maxWidthDips, 480.0f);
    EXPECT_EQ(menu.maxHeightDips, 360.0f);
    EXPECT_FLOAT_EQ(menu.scrollbar.widthDips, 4.0f);
    EXPECT_EQ(Rgba(menu.scrollbar.background), (std::array<std::int32_t, 4>{0x33, 0x33, 0x33, 255}));
    EXPECT_FLOAT_EQ(menu.scrollbar.radiusDips, 2.0f);
    EXPECT_FLOAT_EQ(menu.scrollbar.margin.yDips, 6.0f);
    EXPECT_FLOAT_EQ(menu.scrollbar.margin.xDips, 3.0f);
    // Without caps the monitor bounds the menu, and without a width no thumb draws.
    const MenuStyle plain = ResolveBarStyle(Parse(kBar)).menu;
    EXPECT_EQ(plain.maxWidthDips, std::nullopt);
    EXPECT_EQ(plain.maxHeightDips, std::nullopt);
    EXPECT_FLOAT_EQ(plain.scrollbar.widthDips, 0.0f);
    EXPECT_THAT([] { return Parse("menu {\n  scrollbar {\n    color: #fff\n  }\n}\n"); }, ThrowsWith<ConfigError>("menu > scrollbar: color is not a property"));
    EXPECT_THAT([] { return Parse("tooltip {\n  max-width: 100\n}\n"); }, ThrowsWith<ConfigError>("tooltip: max-width is not a property"));
    EXPECT_THAT([] { return Parse("menu {\n  max-width: 0\n}\n"); }, ThrowsWith<ConfigError>("must be more than 0"));
}

TEST(Style, BorderSetsEverySideAWidthAndColor) {
    const StyleConfig bar = ResolveBarStyle(WithBar("section {\n  border: 1 #6e6a86 25%\n  radius: 14\n  margin: 5\n  padding: 4\n}\n"));
    for (const BorderSide& side : {bar.section.box.border.top, bar.section.box.border.right, bar.section.box.border.bottom, bar.section.box.border.left}) {
        EXPECT_FLOAT_EQ(side.widthDips, 1.0f);
        EXPECT_EQ(Rgba(side.color), (std::array<std::int32_t, 4>{110, 106, 134, 64}));
    }
    EXPECT_FLOAT_EQ(bar.section.margin.yDips, 5.0f);
    EXPECT_FLOAT_EQ(bar.section.box.padding.xDips, 4.0f);
    EXPECT_FLOAT_EQ(ResolveBarStyle(WithBar("section {\n  border: none\n}\n")).section.box.border.left.widthDips, 0.0f);
    EXPECT_THAT([] { return Parse("section {\n  border: 1\n}\n"); }, ThrowsWith<ConfigError>("must be none, or a width and a color"));
    EXPECT_THAT([] { return Parse("section {\n  border: 1 solid #fff\n}\n"); }, ThrowsWith<ConfigError>("must be a color"));
}

TEST(Style, EachSideTakesItsOwnBorderOverItsBlocksAndAMoreSpecificBlocksOverEither) {
    const Border border = StyleOf("cpu {\n  border: 2 #ffffff\n  border-right: 1 #ff0000\n  border-left: none\n}\n", ModuleKind::Cpu, "cpu").box.border;
    EXPECT_FLOAT_EQ(border.top.widthDips, 2.0f);
    EXPECT_EQ(Rgba(border.top.color), (std::array<std::int32_t, 4>{255, 255, 255, 255}));
    EXPECT_FLOAT_EQ(border.right.widthDips, 1.0f);
    EXPECT_EQ(Rgba(border.right.color), kRed);
    EXPECT_FLOAT_EQ(border.left.widthDips, 0.0f);
    EXPECT_FLOAT_EQ(border.bottom.widthDips, 2.0f);
    const Border layered = StyleOf("module {\n  border-top: 3 #ff0000\n}\ncpu {\n  border: 1 #00ff00\n}\n", ModuleKind::Cpu, "cpu").box.border;
    EXPECT_FLOAT_EQ(layered.top.widthDips, 1.0f);
    EXPECT_EQ(Rgba(layered.top.color), kGreen);
    // No border unless a block draws one.
    EXPECT_FLOAT_EQ(StyleOf("", ModuleKind::Cpu, "cpu").box.border.right.widthDips, 0.0f);
}

TEST(Style, ShadowsAreInsetAndKeepTheirOrder) {
    const StyleSheet sheet =
        WithBar("section {\n  shadow: 0 1 2 #ffffff 50%, -1 0 #000000\n}\nworkspaces {\n  item {\n    focused {\n      shadow: 0 -1 1 2 #ff0000\n    }\n  }\n}\n");
    const StyleConfig               bar     = ResolveBarStyle(sheet);
    const std::vector<InsetShadow>& shadows = bar.section.box.shadows;
    ASSERT_EQ(shadows.size(), 2u);
    EXPECT_FLOAT_EQ(shadows[0].xDips, 0.0f);
    EXPECT_FLOAT_EQ(shadows[0].yDips, 1.0f);
    EXPECT_FLOAT_EQ(shadows[0].blurDips, 2.0f);
    EXPECT_FLOAT_EQ(shadows[0].spreadDips, 0.0f);
    EXPECT_EQ(Rgba(shadows[0].color), (std::array<std::int32_t, 4>{255, 255, 255, 128}));
    EXPECT_FLOAT_EQ(shadows[1].xDips, -1.0f);
    EXPECT_FLOAT_EQ(shadows[1].blurDips, 0.0f);
    const ModuleStyle workspaces = ResolveModuleStyle(sheet, bar, ModuleKind::Workspaces, "workspaces", {});
    ASSERT_EQ(workspaces.focused.shadows.size(), 1u);
    EXPECT_FLOAT_EQ(workspaces.focused.shadows[0].yDips, -1.0f);
    EXPECT_FLOAT_EQ(workspaces.focused.shadows[0].spreadDips, 2.0f);
    EXPECT_TRUE(workspaces.item.shadows.empty());
    EXPECT_TRUE(ResolveBarStyle(WithBar("tooltip {\n  shadow: none\n}\n")).tooltip.box.shadows.empty());
    EXPECT_THAT([] { return Parse("section {\n  shadow: 0 1 -2 #000\n}\n"); }, ThrowsWith<ConfigError>("negative blur"));
    EXPECT_THAT([] { return Parse("section {\n  shadow: 1 #000\n}\n"); }, ThrowsWith<ConfigError>("must be none, or shadows"));
    EXPECT_THAT([] { return Parse("section {\n  shadow: 0 1 2\n}\n"); }, ThrowsWith<ConfigError>("must be none, or shadows"));
    EXPECT_THAT([] { return Parse("section {\n  shadow: 0 1 #000,\n}\n"); }, ThrowsWith<ConfigError>("must be none, or shadows"));
    EXPECT_THAT([] { return Parse("module {\n  item {\n    hover {\n      shadow: 0 1 #000\n    }\n  }\n}\n"); },
                ThrowsWith<ConfigError>("module > item > hover: shadow is not a property"));
}

TEST(Style, SectionsTooltipsAndMenusTakeABlurAndNothingElseDoes) {
    const StyleConfig bar = ResolveBarStyle(WithBar("section {\n  blur: on\n}\nmenu {\n  blur: off\n}\n"));
    const Backdrop    none{
           .source     = BackdropSource::None,
           .radiusDips = 0.0f,
           .brightness = 1.0f,
           .saturation = 1.0f,
    };
    EXPECT_EQ(bar.section.backdrop,
              (Backdrop{
                  .source     = BackdropSource::System,
                  .radiusDips = 0.0f,
                  .brightness = 1.0f,
                  .saturation = 1.0f,
              }));
    EXPECT_EQ(bar.tooltip.backdrop, none);
    EXPECT_EQ(bar.menu.backdrop, none);
    EXPECT_THAT([] { return Parse("tooltip {\n  blur: 20\n}\n"); }, ThrowsWith<ConfigError>("tooltip: blur must be on, off, or wallpaper and a radius"));
    EXPECT_THAT([] { return Parse("module {\n  blur: on\n}\n"); }, ThrowsWith<ConfigError>("module: blur is not a property"));
}

TEST(Style, AWallpaperBlurTakesARadiusOfAtMostAHundred) {
    const StyleConfig bar = ResolveBarStyle(WithBar("section {\n  blur: wallpaper 6\n  brightness: 0.8\n}\ntooltip {\n  blur: wallpaper 0\n}\n"));
    EXPECT_EQ(bar.section.backdrop,
              (Backdrop{
                  .source     = BackdropSource::Wallpaper,
                  .radiusDips = 6.0f,
                  .brightness = 0.8f,
                  .saturation = 1.0f,
              }));
    EXPECT_EQ(bar.tooltip.backdrop.source, BackdropSource::Wallpaper);
    EXPECT_FLOAT_EQ(bar.tooltip.backdrop.radiusDips, 0.0f);
    EXPECT_THAT([] { return Parse("section {\n  blur: wallpaper 101\n}\n"); }, ThrowsWith<ConfigError>("section: blur takes a wallpaper radius of at most 100"));
    EXPECT_THAT([] { return Parse("section {\n  blur: wallpaper -2\n}\n"); }, ThrowsWith<ConfigError>("section: blur must be 0 or more"));
    EXPECT_THAT([] { return Parse("section {\n  blur: wallpaper\n}\n"); }, ThrowsWith<ConfigError>("must be on, off, or wallpaper and a radius"));
    EXPECT_THAT([] { return Parse("section {\n  blur: desktop 6\n}\n"); }, ThrowsWith<ConfigError>("must be on, off, or wallpaper and a radius"));
}

TEST(Style, BrightnessAndSaturateToneTheBlur) {
    const StyleConfig bar = ResolveBarStyle(WithBar("section {\n  blur: on\n  saturate: 1.8\n  brightness: 0.4\n}\ntooltip {\n  brightness: 0.4\n  blur: on\n}\n"));
    EXPECT_EQ(bar.section.backdrop,
              (Backdrop{
                  .source     = BackdropSource::System,
                  .radiusDips = 0.0f,
                  .brightness = 0.4f,
                  .saturation = 1.8f,
              }));
    EXPECT_FLOAT_EQ(bar.tooltip.backdrop.brightness, 0.4f);
    EXPECT_FLOAT_EQ(bar.tooltip.backdrop.saturation, 1.0f);
    EXPECT_THAT([] { return Parse("section {\n  brightness: 0.4\n}\n"); },
                ThrowsWith<ConfigError>("section: brightness tones the blur, so it takes blur: on or blur: wallpaper beside it"));
    EXPECT_THAT([] { return Parse("section {\n  blur: off\n  saturate: 2\n}\n"); }, ThrowsWith<ConfigError>("takes blur: on or blur: wallpaper beside it"));
    EXPECT_THAT([] { return Parse("section {\n  blur: on\n  saturate: -1\n}\n"); }, ThrowsWith<ConfigError>("section: saturate must be 0 or more"));
}

TEST(Style, ModulesTakeAMargin) {
    const ModuleStyle style = StyleOf("module {\n  margin: 1\n}\nws {\n  margin: 6 2\n}\n", ModuleKind::Workspaces, "ws");
    EXPECT_FLOAT_EQ(style.margin.yDips, 6.0f);
    EXPECT_FLOAT_EQ(style.margin.xDips, 2.0f);
    EXPECT_FLOAT_EQ(StyleOf("module {\n  margin: 1\n}\n", ModuleKind::Clock, "date").margin.xDips, 1.0f);
}

TEST(Style, ATrayOrGroupTakesItsTransitionsAndItsUnfoldedIconsRotation) {
    const std::string blocks = R"(
icons {
  transition: width 0.4s ease-out
  icon {
    rotate: -90
    transition: rotate 300ms
  }
  expanded {
    icon {
      rotate: 90
    }
  }
}
)";
    const ModuleStyle tray   = StyleOf(blocks, ModuleKind::Tray, "icons");
    EXPECT_EQ(tray.transition.duration, std::chrono::milliseconds(400));
    EXPECT_EQ(tray.transition.timing, TimingFunction::EaseOut);
    EXPECT_FLOAT_EQ(tray.icon.rotationDegrees, -90.0f);
    EXPECT_EQ(tray.icon.transition.duration, std::chrono::milliseconds(300));
    // A transition with no timing function eases.
    EXPECT_EQ(tray.icon.transition.timing, TimingFunction::Ease);
    EXPECT_FLOAT_EQ(tray.expandedIconRotationDegrees, 90.0f);
    EXPECT_FLOAT_EQ(StyleOf(blocks, ModuleKind::Group, "icons").expandedIconRotationDegrees, 90.0f);
}

TEST(Style, ATrayWithoutTransitionsChangesAtOnce) {
    const ModuleStyle tray = StyleOf("icons {\n  icon {\n    rotate: 30\n    transition: none\n  }\n}\n", ModuleKind::Tray, "icons");
    EXPECT_EQ(tray.transition.duration, std::chrono::milliseconds(0));
    EXPECT_EQ(tray.icon.transition.duration, std::chrono::milliseconds(0));
    // With no expanded block the icon keeps its rotation unfolded.
    EXPECT_FLOAT_EQ(tray.expandedIconRotationDegrees, 30.0f);
}

TEST(Style, AnyModuleAndItemFadesItsColors) {
    const std::string blocks = R"(
module {
  transition: background 150ms
}
ws {
  item {
    transition: background 100ms linear, color 200ms ease-in
    focused {
      transition: color 50ms
    }
  }
}
)";
    const ModuleStyle style  = StyleOf(blocks, ModuleKind::Workspaces, "ws");
    EXPECT_EQ(style.backgroundTransition.duration, std::chrono::milliseconds(150));
    EXPECT_EQ(style.transition.duration, std::chrono::milliseconds(0));
    EXPECT_EQ(style.item.backgroundTransition.duration, std::chrono::milliseconds(100));
    EXPECT_EQ(style.item.backgroundTransition.timing, TimingFunction::Linear);
    EXPECT_EQ(style.item.colorTransition.duration, std::chrono::milliseconds(200));
    EXPECT_EQ(style.item.colorTransition.timing, TimingFunction::EaseIn);
    // A state's transition replaces the item's whole list, so the focused item fades no background.
    EXPECT_EQ(style.focused.colorTransition.duration, std::chrono::milliseconds(50));
    EXPECT_EQ(style.focused.backgroundTransition.duration, std::chrono::milliseconds(0));
    EXPECT_EQ(
        StyleOf("ws {\n  item {\n    transition: color 1s\n    focused {\n      transition: none\n    }\n  }\n}\n", ModuleKind::Workspaces, "ws").focused.colorTransition.duration,
        std::chrono::milliseconds(0));
}

TEST(Style, TransitionsAndRotationsAreATraysOrGroupsOnly) {
    EXPECT_THAT([] { return StyleOf("cpu {\n  transition: background 1s, width 1s\n}\n", ModuleKind::Cpu, "cpu"); },
                ThrowsWith<ConfigError>("cpu: transition: width animates a tray or group as it unfolds, and cpu is a cpu module"));
    EXPECT_THAT([] { return StyleOf("time {\n  icon {\n    rotate: 10\n  }\n}\n", ModuleKind::Clock, "time"); }, ThrowsWith<ConfigError>("time: rotate animates a tray or group"));
    EXPECT_THAT([] { return StyleOf("time {\n  expanded {\n    icon {\n      rotate: 10\n    }\n  }\n}\n", ModuleKind::Clock, "time"); },
                ThrowsWith<ConfigError>("time: expanded animates a tray or group"));
    EXPECT_THAT([] { return Parse("module {\n  icon {\n    rotate: 10\n  }\n}\n"); }, ThrowsWith<ConfigError>("module > icon: rotate is not a property"));
    EXPECT_THAT([] { return Parse("module {\n  expanded {\n    icon {\n      rotate: 10\n    }\n  }\n}\n"); },
                ThrowsWith<ConfigError>("module: expanded is not a block module holds"));
}

TEST(Style, TransitionsAreCheckedAsTheyParse) {
    EXPECT_THAT([] { return Parse("tray {\n  transition: color 1s\n}\n"); }, ThrowsWith<ConfigError>("tray: transition animates background, width here, not color"));
    EXPECT_THAT([] { return Parse("tray {\n  transition: width fast\n}\n"); }, ThrowsWith<ConfigError>("duration in ms or s"));
    EXPECT_THAT([] { return Parse("tray {\n  transition: width 1s bouncy\n}\n"); }, ThrowsWith<ConfigError>("timing function 'bouncy'"));
    EXPECT_THAT([] { return Parse("tray {\n  transition: width\n}\n"); }, ThrowsWith<ConfigError>("must be none, or transitions separated by commas"));
    EXPECT_THAT([] { return Parse("tray {\n  transition: width 1s,\n}\n"); }, ThrowsWith<ConfigError>("must be none, or transitions separated by commas"));
    EXPECT_THAT([] { return Parse("tray {\n  expanded {\n    background: #fff\n  }\n}\n"); }, ThrowsWith<ConfigError>("tray > expanded: background is not a property"));
    EXPECT_THAT([] { return Parse("tray {\n  expanded {\n    icon {\n      color: #fff\n    }\n  }\n}\n"); }, ThrowsWith<ConfigError>("which are: rotate"));
}

TEST(Style, NumbersMustBeFiniteUnitlessAndInRange) {
    EXPECT_THAT([] { return Parse("module {\n  padding: inf\n}\n"); }, ThrowsWith<ConfigError>("must be a number with no unit"));
    EXPECT_THAT([] { return Parse("module {\n  padding: 4px\n}\n"); }, ThrowsWith<ConfigError>("must be a number with no unit, as 4, not '4px'"));
    EXPECT_THAT([] { return Parse("module {\n  padding: 20000\n}\n"); }, ThrowsWith<ConfigError>("must be a number from -10000 to 10000"));
    EXPECT_THAT([] { return Parse("module {\n  padding: -1\n}\n"); }, ThrowsWith<ConfigError>("must be 0 or more"));
    EXPECT_THAT([] { return Parse("module {\n  item {\n    weight: nan\n  }\n}\n"); }, ThrowsWith<ConfigError>("a weight from 100 to 900"));
    EXPECT_THAT([] { return Parse("tray {\n  transition: width nanms\n}\n"); }, ThrowsWith<ConfigError>("duration in ms or s"));
    EXPECT_THAT([] { return Parse("tray {\n  transition: width 60000s\n}\n"); }, ThrowsWith<ConfigError>("from 0 to 60s"));
    EXPECT_THAT([] { return Parse("section {\n  blur: on\n  brightness: inf\n}\n"); }, ThrowsWith<ConfigError>("section: brightness must be a number"));
}

TEST(Style, CommentsAndBlankLinesAreIgnoredAndLinesStillCount) {
    const ModuleStyle style = StyleOf("// a note\n\ncpu { // the processor\n  color: #ff0000 // red\n}\n", ModuleKind::Cpu, "cpu");
    EXPECT_EQ(Rgba(style.color), kRed);
    EXPECT_THAT([] { return Parse("// a\n\ncpu {\n  colour: #fff\n}\n"); }, ThrowsWith<ConfigError>("style.conf:4: cpu: colour is not a property"));
    EXPECT_NO_THROW(Parse("cpu {\r\n  color: #fff\r\n}\r\n"));
}

TEST(Style, BlocksAndPropertiesShareLinesThroughSemicolonsAndBraces) {
    const ModuleStyle style = StyleOf("cpu { color: #ff0000; background: #00ff00 }\nws { item { focused { color: #ff0000 } } }\n", ModuleKind::Cpu, "cpu");
    EXPECT_EQ(Rgba(style.color), kRed);
    EXPECT_EQ(Rgba(style.box.background), kGreen);
    EXPECT_EQ(Rgba(StyleOf("ws { item { focused { color: #ff0000 } } }\n", ModuleKind::Workspaces, "ws").focused.color), kRed);
    EXPECT_EQ(Rgba(StyleOf("cpu\n{\n\tcolor: #ff0000;\n}", ModuleKind::Cpu, "cpu").color), kRed);
    EXPECT_EQ(Rgba(StyleOf("cpu { color: #ff0000 // red }\n}\n", ModuleKind::Cpu, "cpu").color), kRed);
}

TEST(Style, UnbalancedBracesAreErrorsNamingTheirLine) {
    EXPECT_THAT([] { return Parse("cpu {\n  icon {\n    color: #fff\n  }\n"); }, ThrowsWith<ConfigError>("style.conf:1: cpu: the block opened here is never closed"));
    EXPECT_THAT([] { return Parse("cpu {\n  color: #fff\n}\n}\n"); }, ThrowsWith<ConfigError>("style.conf:4: } closes no block"));
    EXPECT_THAT([] { return Parse("cpu {\n  color: #fff\n  icon\n"); }, ThrowsWith<ConfigError>("style.conf:3: icon: ends the file"));
}

TEST(Style, CssFormsAreErrorsNamingTheFormToUse) {
    EXPECT_THAT([] { return Parse(".cpu {\n  color: #fff\n}\n"); },
                ThrowsWith<ConfigError>("style.conf:1: '.cpu' is a selector, and style.conf has none: blocks are names nested inside each other"));
    EXPECT_THAT([] { return Parse("cpu {\n  item:hover {\n    color: #fff\n  }\n}\n"); },
                ThrowsWith<ConfigError>("style.conf:2: 'item:hover' is a selector, and style.conf has none"));
    EXPECT_THAT([] { return Parse("cpu {\n  item > hover {\n    color: #fff\n  }\n}\n"); }, ThrowsWith<ConfigError>("style.conf:2: 'item > hover' is a selector"));
    EXPECT_THAT([] { return Parse(":root {\n  --accent: #fff\n}\n"); }, ThrowsWith<ConfigError>(":root is CSS: define values in a vars block and write them as $name"));
    EXPECT_THAT([] { return Parse("vars {\n  --accent: #fff\n}\n"); }, ThrowsWith<ConfigError>("style.conf:2: --name is a CSS custom property"));
    EXPECT_THAT([] { return Parse("cpu {\n  color: var(--accent)\n}\n"); }, ThrowsWith<ConfigError>("color: var() is CSS"));
    EXPECT_THAT([] { return Parse("cpu {\n  color: #fff !important\n}\n"); }, ThrowsWith<ConfigError>("color: !important is CSS"));
    EXPECT_THAT([] { return Parse("@media (min-width: 100) {\n}\n"); }, ThrowsWith<ConfigError>("style.conf:1: @media is a CSS at-rule"));
    EXPECT_THAT([] { return Parse("/* note */\ncpu {\n  color: #fff\n}\n"); }, ThrowsWith<ConfigError>("style.conf:1: /* */ is not a comment here"));
    EXPECT_THAT([] { return Parse("cpu:\n  color: #fff\n"); }, ThrowsWith<ConfigError>("style.conf:1: cpu: has no value: a property is cpu: value, and a block opens with cpu {"));
}

TEST(Style, AnythingOutsideTheFormatIsAnErrorNamingTheLine) {
    EXPECT_THAT([] { return Parse("cpu {\n  color #fff\n}\n"); }, ThrowsWith<ConfigError>("style.conf:2: color: is followed by '#'"));
    EXPECT_THAT([] { return Parse("cpu {\n  ; color: #fff\n}\n"); }, ThrowsWith<ConfigError>("style.conf:2: ';' cannot start a key"));
    EXPECT_THAT([] { return Parse("cpu {\n  color: #fff\n  color: #000\n}\n"); }, ThrowsWith<ConfigError>("style.conf:3: color: is already set on line 2 of the same block"));
    EXPECT_THAT([] { return Parse("cpu {\n}\n"); }, ThrowsWith<ConfigError>("style.conf:1: cpu: is an empty block"));
    EXPECT_THAT([] { return Parse("cpu: #fff\n"); }, ThrowsWith<ConfigError>("cpu: the top level holds blocks"));
    EXPECT_THAT([] { return Parse("cpu {\n  icon: #fff\n}\n"); }, ThrowsWith<ConfigError>("cpu: icon is a block"));
    EXPECT_THAT([] { return Parse("cpu {\n  color {\n    background: #fff\n  }\n}\n"); }, ThrowsWith<ConfigError>("cpu: color is a property"));
    EXPECT_THAT([] { return Parse("section {\n  size: 12\n}\n"); }, ThrowsWith<ConfigError>("section: size is not a property section takes"));
    EXPECT_THAT([] { return Parse("module {\n  width: 3\n}\n"); }, ThrowsWith<ConfigError>("module: width is not a property"));
    EXPECT_THAT([] { return Parse("module {\n  padding: 1 2 3\n}\n"); }, ThrowsWith<ConfigError>("takes one number for every side, or two"));
    EXPECT_THAT([] { return Parse("module {\n  item {\n    weight: 450\n  }\n}\n"); }, ThrowsWith<ConfigError>("in steps of 100"));
}

TEST(Style, AnErrorQuotesAWholeCharacterSoItStaysUtf8) {
    EXPECT_THAT([] { return Parse("cpu {\n  \xC3\xBC: #fff\n}\n"); }, ThrowsWith<ConfigError>("style.conf:2: '\xC3\xBC' cannot start a key"));
    EXPECT_THAT([] { return Parse("cpu {\n  color\xC3\xBC: #fff\n}\n"); }, ThrowsWith<ConfigError>("style.conf:2: color: is followed by '\xC3\xBC'"));
}

TEST(Style, AKeyIsCheckedAgainstItsOwnBlockOnceANestedBlockCloses) {
    EXPECT_THAT([] { return Parse("cpu {\n  icon {\n    color: #fff\n  }\n  color: #fff\n  icon {\n    color: #000\n  }\n}\n"); },
                ThrowsWith<ConfigError>("style.conf:6: icon: is already set on line 2 of the same block"));
    EXPECT_NO_THROW(Parse("cpu {\n  icon {\n    color: #fff\n  }\n  color: #fff\n}\nmemory {\n  icon {\n    color: #000\n  }\n}\n"));
}

TEST(Style, NestingPastEightLevelsIsAnError) {
    std::string nested;
    for (size_t level = 0; level < 9; ++level) {
        nested += "block {\n";
    }
    nested += "color: #fff\n" + std::string(9, '}');
    EXPECT_THAT([&nested] { return Parse(nested); }, ThrowsWith<ConfigError>("style.conf:9: block: is nested 9 levels deep, and a style nests at most 8"));
}

TEST(Style, AValueLongerThan1024CharactersOnceItsVarsAreFilledInIsAnError) {
    const std::string vars = "vars {\n  long: " + std::string(600, 'x') + "\n}\n";
    EXPECT_THAT([&vars] { return Parse(vars + "cpu {\n  color: $long$long\n}\n"); },
                ThrowsWith<ConfigError>("style.conf:5: color: is longer than 1024 characters once its vars are filled in"));
    EXPECT_THAT([&vars] { return Parse(vars + "cpu {\n  color: $long\n}\n"); }, ThrowsWith<ConfigError>("color must be a color"));
}

TEST(Style, ATopLevelBlockMustNameAModuleTheConfigDefines) {
    EXPECT_NO_THROW(RequireKnownModules(WithBar("cpu {\n  color: #fff\n}\nmodule {\n  color: #fff\n}\n"), {"cpu"}));
    EXPECT_THAT([] { RequireKnownModules(WithBar("cpu {\n  color: #fff\n}\n"), {"clock", "memory"}); },
                ThrowsWith<ConfigError>(
                    "style.conf:6: cpu: names no module, and the top level holds vars, bar, section, module, tooltip, menu and the modules the config defines: clock, memory"));
    EXPECT_TRUE(IsReservedModuleName("vars"));
}

TEST(Style, ClassBlocksMustBeOnesACustomModuleDeclares) {
    // A class block under module parses, as a custom module may declare it, and fails once no module does.
    EXPECT_THAT([] { RequireDeclaredClasses(Parse("module {\n  active {\n    color: #fff\n  }\n}\n"), {}); },
                ThrowsWith<ConfigError>("style.conf:2: module: active is not a block module holds (icon, item, hover, alert), nor a class a custom module lists"));
    EXPECT_NO_THROW(RequireDeclaredClasses(Parse("module {\n  active {\n    color: #fff\n  }\n}\n"), {"active"}));
    const StyleSheet  sheet   = WithBar("weather {\n  icon {\n    color: #00ff00\n  }\n  sunny {\n    color: #ff0000\n  }\n}\n");
    const ModuleStyle weather = ResolveModuleStyle(sheet, ResolveBarStyle(sheet), ModuleKind::Custom, "weather", {"sunny", "rainy"});
    ASSERT_EQ(weather.classes.size(), 2u);
    EXPECT_EQ(weather.classes[0].name, "sunny");
    EXPECT_EQ(Rgba(weather.classes[0].colors.text), kRed);
    EXPECT_EQ(Rgba(weather.classes[0].colors.icon), kGreen);
    EXPECT_EQ(Rgba(weather.classes[1].colors.text), kBarColor);
    EXPECT_THAT([&sheet] { return ResolveModuleStyle(sheet, ResolveBarStyle(sheet), ModuleKind::Custom, "weather", {"rainy"}); },
                ThrowsWith<ConfigError>("weather: sunny is not a block a module holds"));
    EXPECT_TRUE(IsStyledClass("hover"));
    EXPECT_FALSE(IsStyledClass("sunny"));
}
