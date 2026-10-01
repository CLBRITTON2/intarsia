#include "config/Style.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

    constexpr std::string_view kBar = R"css(* { font-family: "Some Font"; font-size: 15px; color: #102030; }
)css";

    StyleSheet                 Parse(std::string_view text) {
        return ParseStyleSheet(text, "style.css");
    }

    ModuleStyle StyleOf(std::string_view text, ModuleKind kind, std::string_view name) {
        const StyleSheet sheet = Parse(std::string(kBar) + std::string(text));
        return ResolveModuleStyle(sheet, ResolveBarStyle(sheet), kind, name, {});
    }

    auto ConfigErrorWith(std::string_view text) {
        return testing::ThrowsMessage<ConfigError>(testing::HasSubstr(std::string(text)));
    }

    // Channels as 0 to 255 so a failure reads like the CSS that caused it.
    std::array<std::int32_t, 4> Rgba(const D2D1_COLOR_F& color) {
        return {static_cast<std::int32_t>(std::lround(color.r * 255.0f)), static_cast<std::int32_t>(std::lround(color.g * 255.0f)),
                static_cast<std::int32_t>(std::lround(color.b * 255.0f)), static_cast<std::int32_t>(std::lround(color.a * 255.0f))};
    }

} // namespace

TEST(Style, BarRulesSetTheFontAndColorEveryModuleInherits) {
    const StyleSheet  sheet = Parse(kBar);
    const StyleConfig bar   = ResolveBarStyle(sheet);
    EXPECT_EQ(bar.font, L"Some Font");
    EXPECT_FLOAT_EQ(bar.fontSizeDips, 15.0f);
    const ModuleStyle clock = ResolveModuleStyle(sheet, bar, ModuleKind::Clock, "time", {});
    EXPECT_EQ(Rgba(clock.color), (std::array<std::int32_t, 4>{0x10, 0x20, 0x30, 255}));
    EXPECT_EQ(Rgba(clock.icon.color), Rgba(clock.color));
    EXPECT_EQ(Rgba(clock.item.color), Rgba(clock.color));
    EXPECT_EQ(clock.icon.font, L"Some Font");
    EXPECT_EQ(Rgba(clock.box.background), (std::array<std::int32_t, 4>{0, 0, 0, 0}));
}

TEST(Style, TheBarMustSetFontFamilyFontSizeAndColor) {
    EXPECT_THAT([] { return ResolveBarStyle(Parse("* { font-size: 15px; color: #fff; }")); }, ConfigErrorWith("* must set font-family"));
    EXPECT_THAT([] { return ResolveBarStyle(Parse("")); }, ConfigErrorWith("* must set color"));
}

TEST(Style, ColorsTakeEveryCssForm) {
    const std::string rules = R"css(
        #a { color: #abc; }
        #b { color: #11223380; }
        #c { color: rgb(1, 2, 3); }
        #d { color: rgba(255, 0, 0, 0.5); }
        #e { color: transparent; }
    )css";
    EXPECT_EQ(Rgba(StyleOf(rules, ModuleKind::Cpu, "a").color), (std::array<std::int32_t, 4>{0xaa, 0xbb, 0xcc, 255}));
    EXPECT_EQ(Rgba(StyleOf(rules, ModuleKind::Cpu, "b").color), (std::array<std::int32_t, 4>{0x11, 0x22, 0x33, 0x80}));
    EXPECT_EQ(Rgba(StyleOf(rules, ModuleKind::Cpu, "c").color), (std::array<std::int32_t, 4>{1, 2, 3, 255}));
    EXPECT_EQ(Rgba(StyleOf(rules, ModuleKind::Cpu, "d").color), (std::array<std::int32_t, 4>{255, 0, 0, 128}));
    EXPECT_EQ(Rgba(StyleOf(rules, ModuleKind::Cpu, "e").color), (std::array<std::int32_t, 4>{0, 0, 0, 0}));
    EXPECT_THAT([] { return Parse(".module { color: red; }"); }, ConfigErrorWith("must be a color"));
    EXPECT_THAT([] { return Parse(".module { color: #12345; }"); }, ConfigErrorWith("must be a color"));
    EXPECT_THAT([] { return Parse(".module { color: rgb(300, 0, 0); }"); }, ConfigErrorWith("outside 0 to 255"));
}

TEST(Style, CustomPropertiesAreReadThroughVar) {
    const ModuleStyle style = StyleOf(":root { --accent: #ff0000; } #cpu { color: var(--accent); }", ModuleKind::Cpu, "cpu");
    EXPECT_EQ(Rgba(style.color), (std::array<std::int32_t, 4>{255, 0, 0, 255}));
    EXPECT_THAT([] { return Parse("#cpu { color: var(--nope); }"); }, ConfigErrorWith("var(--nope) names no custom property"));
    EXPECT_THAT([] { return Parse(":root { color: #fff; }"); }, ConfigErrorWith("does not belong in :root"));
}

TEST(Style, MoreSpecificSelectorsWinAndLaterRulesWinATie) {
    const std::string rules = R"css(
        #cpu { color: #0000ff; }
        .module { color: #ff0000; }
        cpu { color: #00ff00; }
        .module { padding: 1px; }
        .module { padding: 2px; }
    )css";
    const ModuleStyle cpu   = StyleOf(rules, ModuleKind::Cpu, "cpu");
    EXPECT_EQ(Rgba(cpu.color), (std::array<std::int32_t, 4>{0, 0, 255, 255}));
    EXPECT_FLOAT_EQ(cpu.box.padding.xDips, 2.0f);
    // A class beats a type.
    EXPECT_EQ(Rgba(StyleOf(rules, ModuleKind::Cpu, "other").color), (std::array<std::int32_t, 4>{255, 0, 0, 255}));
}

TEST(Style, TypeSelectorsMatchEveryModuleOfThatType) {
    const std::string rules = "clock { color: #00ff00; }";
    EXPECT_EQ(Rgba(StyleOf(rules, ModuleKind::Clock, "anything").color), (std::array<std::int32_t, 4>{0, 255, 0, 255}));
    EXPECT_EQ(Rgba(StyleOf(rules, ModuleKind::Cpu, "anything").color), (std::array<std::int32_t, 4>{0x10, 0x20, 0x30, 255}));
}

TEST(Style, IconsAndItemsInheritTheModulesColor) {
    const ModuleStyle style = StyleOf("#ws { color: #ff0000; } #ws .item.focused { color: #00ff00; }", ModuleKind::Workspaces, "ws");
    EXPECT_EQ(Rgba(style.icon.color), (std::array<std::int32_t, 4>{255, 0, 0, 255}));
    EXPECT_EQ(Rgba(style.empty.color), (std::array<std::int32_t, 4>{255, 0, 0, 255}));
    EXPECT_EQ(Rgba(style.focused.color), (std::array<std::int32_t, 4>{0, 255, 0, 255}));
}

TEST(Style, ItemRulesApplyToEveryStateAndStateRulesToOne) {
    const std::string rules = R"css(
        .item { padding: 2px 6px; margin: 0 1px; border-radius: 10px; }
        .item.focused { background: #9ccfd8; font-weight: bold; }
        #ws .item.empty { color: #908caa; }
    )css";
    const ModuleStyle style = StyleOf(rules, ModuleKind::Workspaces, "ws");
    EXPECT_FLOAT_EQ(style.occupied.padding.yDips, 2.0f);
    EXPECT_FLOAT_EQ(style.occupied.padding.xDips, 6.0f);
    EXPECT_FLOAT_EQ(style.focused.margin.xDips, 1.0f);
    EXPECT_FLOAT_EQ(style.empty.radiusDips, 10.0f);
    EXPECT_EQ(style.focused.weight, DWRITE_FONT_WEIGHT_BOLD);
    EXPECT_EQ(style.occupied.weight, DWRITE_FONT_WEIGHT_NORMAL);
    EXPECT_EQ(Rgba(style.focused.background), (std::array<std::int32_t, 4>{0x9c, 0xcf, 0xd8, 255}));
    EXPECT_EQ(Rgba(style.occupied.background), (std::array<std::int32_t, 4>{0, 0, 0, 0}));
    EXPECT_EQ(Rgba(style.empty.color), (std::array<std::int32_t, 4>{0x90, 0x8c, 0xaa, 255}));
    // Another module's .item rules stay with that module.
    EXPECT_EQ(Rgba(StyleOf(rules, ModuleKind::Workspaces, "other").empty.color), (std::array<std::int32_t, 4>{0x10, 0x20, 0x30, 255}));
}

TEST(Style, TheTraysToggleTakesItsItemRulesAndThenItsOwn) {
    const std::string rules = R"css(
        #tray .item { padding: 4px; margin: 0 2px; border-radius: 6px; }
        #tray .item.toggle { padding: 0 9px; border-radius: 10px; }
        #tray .item.toggle:hover { background: #222222; }
    )css";
    const ModuleStyle style = StyleOf(rules, ModuleKind::Tray, "tray");
    EXPECT_FLOAT_EQ(style.toggle.padding.yDips, 0.0f);
    EXPECT_FLOAT_EQ(style.toggle.padding.xDips, 9.0f);
    EXPECT_FLOAT_EQ(style.toggle.radiusDips, 10.0f);
    EXPECT_FLOAT_EQ(style.toggle.margin.xDips, 2.0f);
    EXPECT_EQ(Rgba(style.toggle.hoverBackground), (std::array<std::int32_t, 4>{0x22, 0x22, 0x22, 255}));
    EXPECT_FLOAT_EQ(style.item.padding.xDips, 4.0f);
    EXPECT_FLOAT_EQ(style.item.radiusDips, 6.0f);
    EXPECT_EQ(Rgba(style.item.hoverBackground), (std::array<std::int32_t, 4>{0, 0, 0, 0}));
    EXPECT_THAT([] { return Parse(".item.toggle.focused { color: #fff; }"); }, ConfigErrorWith("matches nothing intarsia draws"));
}

TEST(Style, HoverSetsTheBoxUnderTheCursorAndAlertTheTextThatNeedsAttention) {
    const ModuleStyle style = StyleOf(".module { background: #111111; } cpu:hover { background: #222222; } .alert { color: #ff0000; }", ModuleKind::Cpu, "cpu");
    EXPECT_EQ(Rgba(style.box.background), (std::array<std::int32_t, 4>{0x11, 0x11, 0x11, 255}));
    EXPECT_EQ(Rgba(style.hover), (std::array<std::int32_t, 4>{0x22, 0x22, 0x22, 255}));
    EXPECT_EQ(Rgba(style.alertColor), (std::array<std::int32_t, 4>{255, 0, 0, 255}));
    EXPECT_EQ(Rgba(style.color), (std::array<std::int32_t, 4>{0x10, 0x20, 0x30, 255}));
    // With no :hover rule the box keeps its background under the cursor.
    EXPECT_EQ(Rgba(StyleOf(".module { background: #111111; }", ModuleKind::Cpu, "cpu").hover), (std::array<std::int32_t, 4>{0x11, 0x11, 0x11, 255}));
    EXPECT_THAT([] { return Parse("cpu:hover { color: #fff; }"); }, ConfigErrorWith("color does not apply to cpu:hover, which takes: background"));
}

TEST(Style, BatteryLevelsColorTheTextAndTheIconOverTheModulesOwnRules) {
    const std::string rules = R"css(
        #power .icon { color: #00ffff; }
        #power.warning .icon { color: #ffff00; }
        battery.critical { color: #ff0000; }
    )css";
    const ModuleStyle style = StyleOf(rules, ModuleKind::Battery, "power");
    EXPECT_EQ(Rgba(style.warning.icon), (std::array<std::int32_t, 4>{255, 255, 0, 255}));
    EXPECT_EQ(Rgba(style.warning.text), Rgba(style.color));
    EXPECT_EQ(Rgba(style.critical.text), (std::array<std::int32_t, 4>{255, 0, 0, 255}));
    // No rule targets the critical icon, so the id's .icon rule still applies to it.
    EXPECT_EQ(Rgba(style.critical.icon), (std::array<std::int32_t, 4>{0, 255, 255, 255}));
    const ModuleStyle bare = StyleOf("battery.critical { color: #ff0000; }", ModuleKind::Battery, "power");
    EXPECT_EQ(Rgba(bare.critical.icon), (std::array<std::int32_t, 4>{255, 0, 0, 255}));
    EXPECT_EQ(Rgba(bare.warning.icon), Rgba(bare.icon.color));
}

TEST(Style, MutedColorsTheAudioTextAndIcon) {
    const ModuleStyle style = StyleOf("#volume .icon { color: #00ffff; } audio.muted { color: #808080; } #volume.muted .icon { color: #ff0000; }", ModuleKind::Audio, "volume");
    EXPECT_EQ(Rgba(style.muted.text), (std::array<std::int32_t, 4>{0x80, 0x80, 0x80, 255}));
    EXPECT_EQ(Rgba(style.muted.icon), (std::array<std::int32_t, 4>{255, 0, 0, 255}));
    EXPECT_EQ(Rgba(style.icon.color), (std::array<std::int32_t, 4>{0, 255, 255, 255}));
}

TEST(Style, OwnedStateRulesTakeColorOnlyAndMatchOnlyTheirModuleType) {
    EXPECT_THAT([] { return Parse("battery.warning { padding: 2px; }"); }, ConfigErrorWith("padding does not apply to battery.warning, which takes: color"));
    EXPECT_THAT([] { return Parse("battery.warning .icon { font-size: 20px; }"); }, ConfigErrorWith("which takes: color"));
    EXPECT_THAT([] { return Parse("audio.muted .item { color: #fff; }"); }, ConfigErrorWith("style a module's text and .icon only"));
    EXPECT_THAT([] { return StyleOf(".warning { color: #fff; }", ModuleKind::Cpu, "cpu"); }, ConfigErrorWith(".warning colors only battery modules, and cpu is a cpu module"));
    EXPECT_THAT([] { return StyleOf(".muted { color: #fff; }", ModuleKind::Battery, "power"); },
                ConfigErrorWith(".muted colors only audio modules, and power is a battery module"));
    EXPECT_NO_THROW(StyleOf("battery.warning { color: #fff; } audio.muted { color: #fff; }", ModuleKind::Cpu, "cpu"));
}

TEST(Style, ItemHoverChangesColorsAndLosesToAMoreSpecificStateRule) {
    const std::string rules = R"css(
        .item { background: #111111; }
        .item:hover { background: #222222; color: #ffffff; }
        #ws .item.focused { background: #9ccfd8; }
    )css";
    const ModuleStyle style = StyleOf(rules, ModuleKind::Workspaces, "ws");
    EXPECT_EQ(Rgba(style.occupied.hoverBackground), (std::array<std::int32_t, 4>{0x22, 0x22, 0x22, 255}));
    EXPECT_EQ(Rgba(style.occupied.hoverColor), (std::array<std::int32_t, 4>{255, 255, 255, 255}));
    EXPECT_EQ(Rgba(style.occupied.background), (std::array<std::int32_t, 4>{0x11, 0x11, 0x11, 255}));
    EXPECT_EQ(Rgba(style.focused.hoverBackground), (std::array<std::int32_t, 4>{0x9c, 0xcf, 0xd8, 255}));
    // With no :hover rule an item keeps its colors under the cursor.
    const ModuleStyle plain = StyleOf(".item { color: #ff0000; }", ModuleKind::Windows, "apps");
    EXPECT_EQ(Rgba(plain.item.hoverColor), (std::array<std::int32_t, 4>{255, 0, 0, 255}));
    EXPECT_THAT([] { return Parse(".item:hover { padding: 2px; }"); }, ConfigErrorWith("padding does not apply to .item:hover, which takes: color, background"));
    EXPECT_THAT([] { return Parse(".icon:hover { color: #fff; }"); }, ConfigErrorWith("matches nothing intarsia draws"));
}

TEST(Style, MenuRulesStyleTheMenuItsEntriesAndTheActiveEntry) {
    const std::string rules = R"css(
        menu { background: #111111; color: #eeeeee; padding: 6px; }
        menu .item { padding: 2px 8px; }
        menu .item:hover { background: #222222; }
        menu .item.active { font-weight: bold; color: #00ff00; }
        menu .item.active:hover { color: #ffffff; }
    )css";
    const MenuStyle   menu  = ResolveBarStyle(Parse(std::string(kBar) + rules)).menu;
    EXPECT_EQ(Rgba(menu.box.background), (std::array<std::int32_t, 4>{0x11, 0x11, 0x11, 255}));
    EXPECT_FLOAT_EQ(menu.box.padding.xDips, 6.0f);
    EXPECT_EQ(Rgba(menu.item.color), (std::array<std::int32_t, 4>{0xee, 0xee, 0xee, 255}));
    EXPECT_FLOAT_EQ(menu.item.padding.xDips, 8.0f);
    EXPECT_EQ(Rgba(menu.item.hoverBackground), (std::array<std::int32_t, 4>{0x22, 0x22, 0x22, 255}));
    EXPECT_EQ(menu.active.weight, DWRITE_FONT_WEIGHT_BOLD);
    EXPECT_EQ(Rgba(menu.active.color), (std::array<std::int32_t, 4>{0, 255, 0, 255}));
    EXPECT_EQ(Rgba(menu.active.hoverColor), (std::array<std::int32_t, 4>{255, 255, 255, 255}));
    EXPECT_FLOAT_EQ(menu.active.padding.xDips, 8.0f);
    // With no menu rules the entries take the bar's color.
    EXPECT_EQ(Rgba(ResolveBarStyle(Parse(kBar)).menu.item.color), (std::array<std::int32_t, 4>{0x10, 0x20, 0x30, 255}));
    EXPECT_THAT([] { return Parse("menu .icon { color: #fff; }"); }, ConfigErrorWith("a menu's entries are menu .item"));
}

TEST(Style, MenuRulesCapTheMenuAndStyleItsScrollBar) {
    const std::string rules = R"css(
        menu { max-width: 480px; max-height: 360px; }
        menu .scrollbar { width: 4px; background: #333333; border-radius: 2px; margin: 6px 3px; }
    )css";
    const MenuStyle   menu  = ResolveBarStyle(Parse(std::string(kBar) + rules)).menu;
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
    EXPECT_THAT([] { return Parse("menu .scrollbar:hover { background: #fff; }"); }, ConfigErrorWith("its scroll bar is menu .scrollbar"));
    EXPECT_THAT([] { return Parse("menu .scrollbar { color: #fff; }"); }, ConfigErrorWith("color does not apply to menu .scrollbar"));
    EXPECT_THAT([] { return Parse("tooltip { max-width: 100px; }"); }, ConfigErrorWith("max-width does not apply to tooltip"));
}

TEST(Style, BorderShorthandSetsWidthAndColor) {
    const StyleSheet  sheet = Parse(std::string(kBar) + "section { border: 1px solid rgba(110, 106, 134, 0.25); border-radius: 14px; margin: 5px; padding: 4px; }");
    const StyleConfig bar   = ResolveBarStyle(sheet);
    for (const BorderSide& side : {bar.section.box.border.top, bar.section.box.border.right, bar.section.box.border.bottom, bar.section.box.border.left}) {
        EXPECT_FLOAT_EQ(side.widthDips, 1.0f);
        EXPECT_EQ(Rgba(side.color), (std::array<std::int32_t, 4>{110, 106, 134, 64}));
    }
    EXPECT_FLOAT_EQ(bar.section.margin.yDips, 5.0f);
    EXPECT_FLOAT_EQ(bar.section.box.padding.xDips, 4.0f);
    EXPECT_FLOAT_EQ(ResolveBarStyle(Parse(std::string(kBar) + "section { border: 1px solid #fff; border: none; }")).section.box.border.left.widthDips, 0.0f);
    EXPECT_THAT([] { return Parse("section { border: 1px dashed #fff; }"); }, ConfigErrorWith("must be none, or a width, solid and a color"));
}

TEST(Style, EachSideTakesItsOwnBorder) {
    const Border border = StyleOf("#cpu { border: 2px solid #ffffff; border-right: 1px solid #ff0000; border-left: none; }", ModuleKind::Cpu, "cpu").box.border;
    EXPECT_FLOAT_EQ(border.top.widthDips, 2.0f);
    EXPECT_EQ(Rgba(border.top.color), (std::array<std::int32_t, 4>{255, 255, 255, 255}));
    EXPECT_FLOAT_EQ(border.right.widthDips, 1.0f);
    EXPECT_EQ(Rgba(border.right.color), (std::array<std::int32_t, 4>{255, 0, 0, 255}));
    EXPECT_FLOAT_EQ(border.left.widthDips, 0.0f);
    EXPECT_FLOAT_EQ(border.bottom.widthDips, 2.0f);
    EXPECT_EQ(Rgba(border.bottom.color), (std::array<std::int32_t, 4>{255, 255, 255, 255}));
    // border-bottom-color is a longhand the bar reads, not one a style writes.
    EXPECT_THAT([] { return Parse("#cpu { border-bottom-color: #00ff00; }"); }, ConfigErrorWith("border-bottom-color does not apply to #cpu"));
    const Border widths = StyleOf("#cpu { border-width: 3px; border-color: #0000ff; border-top: none; }", ModuleKind::Cpu, "cpu").box.border;
    EXPECT_FLOAT_EQ(widths.top.widthDips, 0.0f);
    EXPECT_FLOAT_EQ(widths.bottom.widthDips, 3.0f);
    EXPECT_EQ(Rgba(widths.left.color), (std::array<std::int32_t, 4>{0, 0, 255, 255}));
    // No border unless a rule draws one.
    EXPECT_FLOAT_EQ(StyleOf("", ModuleKind::Cpu, "cpu").box.border.right.widthDips, 0.0f);
}

TEST(Style, BoxShadowsAreInsetAndKeepTheirOrder) {
    const StyleSheet                sheet   = Parse(std::string(kBar) +
                                                    "section { box-shadow: inset 0 1px 2px rgba(255, 255, 255, 0.5), inset -1px 0 #000000; } "
                                                                     "workspaces .item.focused { box-shadow: inset 0 -1px 1px 2px #ff0000; }");
    const StyleConfig               bar     = ResolveBarStyle(sheet);
    const std::vector<InsetShadow>& shadows = bar.section.box.shadows;
    if (shadows.size() != 2) {
        FAIL() << "section has " << shadows.size() << " shadows, not 2";
    }
    EXPECT_FLOAT_EQ(shadows[0].xDips, 0.0f);
    EXPECT_FLOAT_EQ(shadows[0].yDips, 1.0f);
    EXPECT_FLOAT_EQ(shadows[0].blurDips, 2.0f);
    EXPECT_FLOAT_EQ(shadows[0].spreadDips, 0.0f);
    EXPECT_EQ(Rgba(shadows[0].color), (std::array<std::int32_t, 4>{255, 255, 255, 128}));
    EXPECT_FLOAT_EQ(shadows[1].xDips, -1.0f);
    EXPECT_FLOAT_EQ(shadows[1].blurDips, 0.0f);
    const ModuleStyle workspaces = ResolveModuleStyle(sheet, bar, ModuleKind::Workspaces, "workspaces", {});
    if (workspaces.focused.shadows.size() != 1) {
        FAIL() << "the focused item has " << workspaces.focused.shadows.size() << " shadows, not 1";
    }
    EXPECT_FLOAT_EQ(workspaces.focused.shadows[0].yDips, -1.0f);
    EXPECT_FLOAT_EQ(workspaces.focused.shadows[0].spreadDips, 2.0f);
    EXPECT_TRUE(workspaces.item.shadows.empty());
    EXPECT_TRUE(ResolveBarStyle(Parse(std::string(kBar) + "tooltip { box-shadow: inset 0 1px #fff; box-shadow: none; }")).tooltip.box.shadows.empty());
    EXPECT_THAT([] { return Parse("section { box-shadow: 0 1px 2px #000; }"); }, ConfigErrorWith("every shadow starts with inset"));
    EXPECT_THAT([] { return Parse("section { box-shadow: inset 0 1px -2px #000; }"); }, ConfigErrorWith("negative blur"));
    EXPECT_THAT([] { return Parse("section { box-shadow: inset 1px #000; }"); }, ConfigErrorWith("must be none, or inset shadows"));
    EXPECT_THAT([] { return Parse(".item:hover { box-shadow: inset 0 1px #000; }"); }, ConfigErrorWith("box-shadow does not apply to .item:hover"));
}

TEST(Style, SectionsTooltipsAndMenusTakeTheSystemsBlurAndNothingElseDoes) {
    const StyleConfig bar = ResolveBarStyle(Parse(std::string(kBar) + "section { backdrop-filter: blur; } menu { backdrop-filter: blur; backdrop-filter: none; }"));
    const Backdrop    none{.blur = false, .brightness = 1.0f, .saturation = 1.0f};
    EXPECT_EQ(bar.section.backdrop, (Backdrop{.blur = true, .brightness = 1.0f, .saturation = 1.0f}));
    EXPECT_EQ(bar.tooltip.backdrop, none);
    EXPECT_EQ(bar.menu.backdrop, none);
    EXPECT_THAT([] { return Parse("tooltip { backdrop-filter: blur(20px); }"); }, ConfigErrorWith("must be none, or blur"));
    EXPECT_THAT([] { return Parse(".module { backdrop-filter: blur; }"); }, ConfigErrorWith("backdrop-filter does not apply to .module"));
}

TEST(Style, BrightnessAndSaturateToneTheBlurOnceEachInEitherOrder) {
    const StyleConfig bar =
        ResolveBarStyle(Parse(std::string(kBar) + "section { backdrop-filter: blur saturate(180%) brightness( 0.4 ); } tooltip { backdrop-filter: blur brightness(40%); }"));
    EXPECT_EQ(bar.section.backdrop, (Backdrop{.blur = true, .brightness = 0.4f, .saturation = 1.8f}));
    EXPECT_FLOAT_EQ(bar.tooltip.backdrop.brightness, 0.4f);
    EXPECT_FLOAT_EQ(bar.tooltip.backdrop.saturation, 1.0f);
    EXPECT_THAT([] { return Parse("section { backdrop-filter: brightness(0.4); }"); }, ConfigErrorWith("must be none, or blur"));
    EXPECT_THAT([] { return Parse("section { backdrop-filter: blur brightness(0.4) brightness(0.5); }"); }, ConfigErrorWith("once each"));
    EXPECT_THAT([] { return Parse("section { backdrop-filter: blur contrast(2); }"); }, ConfigErrorWith("once each"));
    EXPECT_THAT([] { return Parse("section { backdrop-filter: blur saturate(-1); }"); },
                ConfigErrorWith("has saturate(-1), whose amount must be a number or percentage of 0 or more"));
}

TEST(Style, ModulesTakeNoSize) {
    EXPECT_THAT([] { return Parse(".module { width: 3px; }"); }, ConfigErrorWith("width does not apply to .module"));
    EXPECT_THAT([] { return Parse(".module { height: 50%; }"); }, ConfigErrorWith("is not a property intarsia draws"));
}

TEST(Style, ModulesTakeAMarginButTheirHoverDoesNot) {
    const ModuleStyle style = StyleOf(".module { margin: 1px; } #ws { margin: 6px 2px; }", ModuleKind::Workspaces, "ws");
    EXPECT_FLOAT_EQ(style.margin.yDips, 6.0f);
    EXPECT_FLOAT_EQ(style.margin.xDips, 2.0f);
    EXPECT_FLOAT_EQ(StyleOf(".module { margin: 1px; }", ModuleKind::Clock, "date").margin.xDips, 1.0f);
    EXPECT_THAT([] { return Parse("#ws:hover { margin: 1px; }"); }, ConfigErrorWith("margin does not apply to #ws:hover"));
}

TEST(Style, ATrayTakesItsTransitionsAndItsUnfoldedIconsRotation) {
    const ModuleStyle tray = StyleOf("tray { transition: width 0.4s ease-out; } tray .icon { transform: rotate(-90deg); transition: transform 300ms; } "
                                     "tray.expanded .icon { transform: rotate(90deg); }",
                                     ModuleKind::Tray, "icons");
    EXPECT_EQ(tray.transition.duration, std::chrono::milliseconds(400));
    EXPECT_EQ(tray.transition.timing, TimingFunction::EaseOut);
    EXPECT_FLOAT_EQ(tray.icon.rotationDegrees, -90.0f);
    EXPECT_EQ(tray.icon.transition.duration, std::chrono::milliseconds(300));
    // CSS's default timing function.
    EXPECT_EQ(tray.icon.transition.timing, TimingFunction::Ease);
    EXPECT_FLOAT_EQ(tray.expandedIconRotationDegrees, 90.0f);
}

TEST(Style, ATrayWithoutTransitionsChangesAtOnce) {
    const ModuleStyle tray = StyleOf("tray .icon { transform: rotate(30deg); transition: none; }", ModuleKind::Tray, "icons");
    EXPECT_EQ(tray.transition.duration, std::chrono::milliseconds(0));
    EXPECT_EQ(tray.icon.transition.duration, std::chrono::milliseconds(0));
    // With no .expanded rule the icon keeps its rotation unfolded.
    EXPECT_FLOAT_EQ(tray.expandedIconRotationDegrees, 30.0f);
}

TEST(Style, AGroupTakesTheTraysTransitionsAndUnfoldedIconRotation) {
    const ModuleStyle group = StyleOf("group { transition: width 250ms linear; } #layout .icon { transform: rotate(-90deg); transition: transform 100ms; } "
                                      "#layout.expanded .icon { transform: rotate(90deg); }",
                                      ModuleKind::Group, "layout");
    EXPECT_EQ(group.transition.duration, std::chrono::milliseconds(250));
    EXPECT_EQ(group.transition.timing, TimingFunction::Linear);
    EXPECT_FLOAT_EQ(group.icon.rotationDegrees, -90.0f);
    EXPECT_EQ(group.icon.transition.duration, std::chrono::milliseconds(100));
    EXPECT_FLOAT_EQ(group.expandedIconRotationDegrees, 90.0f);
}

TEST(Style, AnyModuleAndItemFadesItsColors) {
    const ModuleStyle style = StyleOf(".module { transition: background 150ms; } #ws .item { transition: background 100ms linear, color 200ms ease-in; } "
                                      "#ws .item.focused { transition: color 50ms; }",
                                      ModuleKind::Workspaces, "ws");
    EXPECT_EQ(style.backgroundTransition.duration, std::chrono::milliseconds(150));
    EXPECT_EQ(style.transition.duration, std::chrono::milliseconds(0));
    EXPECT_EQ(style.item.backgroundTransition.duration, std::chrono::milliseconds(100));
    EXPECT_EQ(style.item.backgroundTransition.timing, TimingFunction::Linear);
    EXPECT_EQ(style.item.colorTransition.duration, std::chrono::milliseconds(200));
    EXPECT_EQ(style.item.colorTransition.timing, TimingFunction::EaseIn);
    // A later transition replaces the whole list, as in CSS, so the focused item fades no background.
    EXPECT_EQ(style.focused.colorTransition.duration, std::chrono::milliseconds(50));
    EXPECT_EQ(style.focused.backgroundTransition.duration, std::chrono::milliseconds(0));
}

TEST(Style, ATrayTakesItsWidthAndBackgroundTransitionsFromOneList) {
    const ModuleStyle tray = StyleOf("tray { transition: width 400ms ease-out, background 100ms; }", ModuleKind::Tray, "icons");
    EXPECT_EQ(tray.transition.duration, std::chrono::milliseconds(400));
    EXPECT_EQ(tray.transition.timing, TimingFunction::EaseOut);
    EXPECT_EQ(tray.backgroundTransition.duration, std::chrono::milliseconds(100));
}

TEST(Style, TransitionsAndRotationsAreATraysOrGroupsOnly) {
    EXPECT_THAT([] { return StyleOf("cpu { transition: background 1s, width 1s; }", ModuleKind::Cpu, "cpu"); },
                ConfigErrorWith("transition: width animates a tray or group as it unfolds, and cpu is a cpu module"));
    EXPECT_THAT([] { return StyleOf(".icon { transform: rotate(10deg); }", ModuleKind::Clock, "time"); }, ConfigErrorWith("transform animates a tray or group"));
    EXPECT_THAT([] { return StyleOf(".expanded .icon { transform: rotate(10deg); }", ModuleKind::Clock, "time"); }, ConfigErrorWith("transform animates a tray or group"));
}

TEST(Style, TransitionsAndRotationsAreCheckedAsTheyParse) {
    EXPECT_THAT([] { return Parse("tray { transition: color 1s; }"); }, ConfigErrorWith("animates background, width here, not color"));
    EXPECT_THAT([] { return Parse("tray .icon { transition: width 1s; }"); }, ConfigErrorWith("animates transform here, not width"));
    EXPECT_THAT([] { return Parse(".item { transition: background 1s, width 1s; }"); }, ConfigErrorWith("animates background, color here, not width"));
    EXPECT_THAT([] { return Parse("menu .item { transition: color 1s; }"); }, ConfigErrorWith("transition does not apply to menu .item"));
    EXPECT_THAT([] { return Parse(".module:hover { transition: background 1s; }"); }, ConfigErrorWith("transition does not apply to .module:hover"));
    EXPECT_THAT([] { return Parse("tray { transition: width fast; }"); }, ConfigErrorWith("duration in ms or s"));
    EXPECT_THAT([] { return Parse("tray { transition: width 1s bouncy; }"); }, ConfigErrorWith("timing function 'bouncy'"));
    EXPECT_THAT([] { return Parse("tray { transition: width; }"); }, ConfigErrorWith("must be none, or transitions separated by commas"));
    EXPECT_THAT([] { return Parse("tray { transition: width 1s,; }"); }, ConfigErrorWith("must be none, or transitions separated by commas"));
    EXPECT_THAT([] { return Parse("tray .icon { transform: scale(2); }"); }, ConfigErrorWith("rotate(180deg)"));
    EXPECT_THAT([] { return Parse("tray.expanded { background: #fff; }"); }, ConfigErrorWith(".expanded styles an unfolded tray's or group's .icon only"));
    EXPECT_THAT([] { return Parse("group.expanded .item { color: #fff; }"); }, ConfigErrorWith(".expanded styles an unfolded tray's or group's .icon only"));
    EXPECT_THAT([] { return Parse("tray.expanded .icon { color: #fff; }"); }, ConfigErrorWith("which takes: transform"));
}

TEST(Style, CommentsAreIgnoredAndLinesStillCount) {
    const ModuleStyle style = StyleOf("/* one\n two */ #cpu { /* inline */ color: #ff0000; }", ModuleKind::Cpu, "cpu");
    EXPECT_EQ(Rgba(style.color), (std::array<std::int32_t, 4>{255, 0, 0, 255}));
    EXPECT_THAT([] { return Parse("/* a\n b\n */ #cpu { colour: #fff; }"); }, ConfigErrorWith("style.css:3: #cpu: colour is not a property"));
    EXPECT_THAT([] { return Parse("#cpu { } /* never closed"); }, ConfigErrorWith("never closed"));
}

TEST(Style, SelectorListsApplyTheBlockToEachSelector) {
    const std::string rules = "cpu, memory { color: #ff0000; }";
    EXPECT_EQ(Rgba(StyleOf(rules, ModuleKind::Memory, "m").color), (std::array<std::int32_t, 4>{255, 0, 0, 255}));
    EXPECT_EQ(Rgba(StyleOf(rules, ModuleKind::Cpu, "c").color), (std::array<std::int32_t, 4>{255, 0, 0, 255}));
}

TEST(Style, AnythingOutsideTheSubsetIsAnErrorNamingTheLine) {
    EXPECT_THAT([] { return Parse("\n\n@import url(x.css);"); }, ConfigErrorWith("style.css:3: at-rules"));
    EXPECT_THAT([] { return Parse("#cpu > .icon { color: #fff; }"); }, ConfigErrorWith("combinator"));
    EXPECT_THAT([] { return Parse("button { color: #fff; }"); }, ConfigErrorWith("'button' is not a type intarsia styles"));
    // A class intarsia does not style parses, as a custom module may declare it, and fails once no module does.
    EXPECT_THAT([] { RequireDeclaredClasses(Parse(".active { color: #fff; }"), {}); }, ConfigErrorWith("style.css:1: '.active' is not a class intarsia styles"));
    EXPECT_NO_THROW(RequireDeclaredClasses(Parse(".active { color: #fff; }"), {DeclaredClasses{.module = "weather", .classes = {"active"}}}));
    EXPECT_THAT([] { return Parse("#cpu:focus { color: #fff; }"); }, ConfigErrorWith(":hover is the only state"));
    EXPECT_THAT([] { return Parse("#cpu { color: #fff !important; }"); }, ConfigErrorWith("!important is not supported"));
    EXPECT_THAT([] { return Parse("#cpu { color #fff; }"); }, ConfigErrorWith("must be property: value"));
    EXPECT_THAT([] { return Parse("#cpu { color: #fff; "); }, ConfigErrorWith("never closed"));
    EXPECT_THAT([] { return Parse("#cpu { .icon { color: #fff; } }"); }, ConfigErrorWith("rules cannot nest"));
    EXPECT_THAT([] { return Parse("section { font-size: 12px; }"); }, ConfigErrorWith("font-size does not apply to section"));
    EXPECT_THAT([] { return Parse(".module { padding: 1px 2px 3px; }"); }, ConfigErrorWith("takes one length for every side, or two"));
    EXPECT_THAT([] { return Parse(".module { padding: 1em; }"); }, ConfigErrorWith("must be a length in px"));
    EXPECT_THAT([] { return Parse(".icon { font-family: \"A\", monospace; }"); }, ConfigErrorWith("one font family, not a list"));
    EXPECT_THAT([] { return Parse("#ws.alert .item { color: #fff; }"); }, ConfigErrorWith("must start with a module"));
}
