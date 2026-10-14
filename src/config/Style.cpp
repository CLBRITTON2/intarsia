#include "config/Style.hpp"

#include "config/StyleShapes.hpp"
#include "config/StyleTree.hpp"
#include "config/StyleValues.hpp"

#include <algorithm>
#include <format>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    // Blocks that set the same thing, the one that wins first, with no null entries.
    using Chain = std::vector<const StyleNode*>;

    Chain ChainOf(std::initializer_list<const StyleNode*> blocks) {
        Chain chain;
        std::ranges::copy_if(blocks, std::back_inserter(chain), [](const StyleNode* block) { return block != nullptr; });
        return chain;
    }

    // Each block's child block named key, in the chain's order.
    Chain Children(const Chain& chain, std::string_view key) {
        Chain children;
        for (const StyleNode* block : chain) {
            if (const StyleNode* child = Child(block, key); child != nullptr && IsBlock(*child)) {
                children.push_back(child);
            }
        }
        return children;
    }

    Chain Concat(const Chain& first, const Chain& second) {
        Chain joined = first;
        joined.insert(joined.end(), second.begin(), second.end());
        return joined;
    }

    // The first property named key in the chain.
    const StyleNode* FirstSet(const Chain& chain, std::string_view key) {
        for (const StyleNode* block : chain) {
            if (const StyleNode* property = Child(block, key); property != nullptr && !IsBlock(*property)) {
                return property;
            }
        }
        return nullptr;
    }

    template <typename Reader>
    auto Read(const Chain& chain, std::string_view key, Reader reader, decltype(reader(std::string_view())) inherited) -> decltype(reader(std::string_view())) {
        const StyleNode* property = FirstSet(chain, key);
        if (property == nullptr) {
            return inherited;
        }
        return reader(property->value);
    }

    // A reader of one property's transition, for Read.
    auto TransitionReader(std::string_view property) {
        return [property](std::string_view text) { return ParseTransition(text, property); };
    }

    D2D1_COLOR_F ResolveColor(const Chain& chain, std::string_view key, const D2D1_COLOR_F& inherited) {
        return Read(chain, key, ParseColor, inherited);
    }

    constexpr Spacing kNoSpace{.yDips = 0.0f, .xDips = 0.0f};

    Spacing           ResolveSpacing(const Chain& chain, std::string_view key) {
        return Read(chain, key, ParseSpacing, kNoSpace);
    }

    // A side's own border wins over its block's border, and a more specific block's border over either.
    BorderSide ResolveBorderSide(const Chain& chain, std::string_view side) {
        const std::string own = std::format("border-{}", side);
        for (const StyleNode* block : chain) {
            if (const StyleNode* sideBorder = Child(block, own)) {
                return ParseBorderSide(sideBorder->value);
            }
            if (const StyleNode* border = Child(block, "border")) {
                return ParseBorderSide(border->value);
            }
        }
        return kNoBorder;
    }

    BoxStyle ResolveBox(const Chain& chain) {
        return BoxStyle{
            .background = ResolveColor(chain, "background", kTransparent),
            .border =
                {
                    .top    = ResolveBorderSide(chain, "top"),
                    .right  = ResolveBorderSide(chain, "right"),
                    .bottom = ResolveBorderSide(chain, "bottom"),
                    .left   = ResolveBorderSide(chain, "left"),
                },
            .radiusDips = Read(chain, "radius", ParseLength, 0.0f),
            .padding    = ResolveSpacing(chain, "padding"),
            .shadows    = Read(chain, "shadow", ParseShadows, std::vector<InsetShadow>{}),
        };
    }

    constexpr Backdrop kNoBackdrop{
        .blur       = false,
        .brightness = 1.0f,
        .saturation = 1.0f,
    };

    Backdrop ResolveBackdrop(const Chain& chain) {
        if (!Read(chain, "blur", ParseSwitch, false)) {
            return kNoBackdrop;
        }
        return Backdrop{
            .blur       = true,
            .brightness = Read(chain, "brightness", ParseLength, 1.0f),
            .saturation = Read(chain, "saturate", ParseLength, 1.0f),
        };
    }

    // An item from its blocks, and its colors under the cursor from hover's.
    ItemStyle ResolveItem(const Chain& item, const Chain& hover, const D2D1_COLOR_F& color) {
        const D2D1_COLOR_F itemColor  = ResolveColor(item, "color", color);
        const D2D1_COLOR_F background = ResolveColor(item, "background", kTransparent);
        return ItemStyle{
            .color                = itemColor,
            .background           = background,
            .weight               = Read(item, "weight", ParseWeight, DWRITE_FONT_WEIGHT_NORMAL),
            .padding              = ResolveSpacing(item, "padding"),
            .margin               = ResolveSpacing(item, "margin"),
            .radiusDips           = Read(item, "radius", ParseLength, 0.0f),
            .shadows              = Read(item, "shadow", ParseShadows, std::vector<InsetShadow>{}),
            .hoverColor           = ResolveColor(hover, "color", itemColor),
            .hoverBackground      = ResolveColor(hover, "background", background),
            .backgroundTransition = Read(item, "transition", TransitionReader("background"), kNoTransition),
            .colorTransition      = Read(item, "transition", TransitionReader("color"), kNoTransition),
        };
    }

    // An item in one state: the state's blocks over the item's, and under the cursor the state's colors over the item's
    // hover, so a state such as focused keeps the colors it sets.
    ItemStyle ResolveItemState(const Chain& items, std::string_view state, const D2D1_COLOR_F& color) {
        const Chain states = Children(items, state);
        return ResolveItem(Concat(states, items), Concat(states, Children(items, "hover")), color);
    }

    // A module's text and icon colors in one state: the state's blocks win over the module's own, and the icon inherits
    // the state's text color.
    StateColors ResolveStateColors(const Chain& module, std::string_view state, const D2D1_COLOR_F& color) {
        const Chain        states = Children(module, state);
        const D2D1_COLOR_F text   = ResolveColor(states, "color", color);
        return StateColors{.text = text, .icon = ResolveColor(Concat(Children(states, "icon"), Children(module, "icon")), "color", text)};
    }

    const StyleNode& Required(const StyleSheet& sheet, const StyleNode& bar, std::string_view key, std::string_view example) {
        const StyleNode* property = Child(&bar, key);
        if (property == nullptr) {
            throw StyleError(sheet.source, bar.line, std::format("bar must set {}, as {}: {}", key, key, example));
        }
        return *property;
    }

} // namespace

StyleSheet ParseStyleSheet(std::string_view text, std::string source) {
    StyleSheet sheet{.source = std::move(source), .blocks = {}};
    sheet.blocks = ParseStyleTree(text, sheet.source);
    for (const StyleNode& block : sheet.blocks) {
        CheckTopLevelBlock(block, sheet.source);
    }
    return sheet;
}

StyleConfig ResolveBarStyle(const StyleSheet& sheet) {
    const StyleNode* bar = TopLevel(sheet, "bar");
    if (bar == nullptr) {
        throw ConfigError(std::format("{}: must have a bar block that sets font, size and color, as bar {{ font: Segoe UI; size: 13; color: #e0def4 }}", sheet.source));
    }
    const Chain        sectionChain = ChainOf({TopLevel(sheet, "section")});
    const Chain        tooltipChain = ChainOf({TopLevel(sheet, "tooltip")});
    const Chain        menuChain    = ChainOf({TopLevel(sheet, "menu")});
    const Chain        items        = Children(menuChain, "item");
    const Chain        active       = Children(items, "active");
    const D2D1_COLOR_F color        = ParseColor(Required(sheet, *bar, "color", "#e0def4").value);
    const D2D1_COLOR_F menuColor    = ResolveColor(menuChain, "color", color);
    const Chain        scrollbar    = Children(menuChain, "scrollbar");
    return StyleConfig{
        .font         = ParseFamily(Required(sheet, *bar, "font", "Segoe UI").value),
        .fontSizeDips = ParsePositiveLength(Required(sheet, *bar, "size", "13").value),
        .color        = color,
        .section =
            {
                .box      = ResolveBox(sectionChain),
                .margin   = ResolveSpacing(sectionChain, "margin"),
                .backdrop = ResolveBackdrop(sectionChain),
            },
        .tooltip =
            {
                .box      = ResolveBox(tooltipChain),
                .color    = ResolveColor(tooltipChain, "color", color),
                .backdrop = ResolveBackdrop(tooltipChain),
            },
        .menu =
            {
                .box           = ResolveBox(menuChain),
                .color         = menuColor,
                .backdrop      = ResolveBackdrop(menuChain),
                .maxWidthDips  = Read(menuChain, "max-width", ParseCap, std::nullopt),
                .maxHeightDips = Read(menuChain, "max-height", ParseCap, std::nullopt),
                .item          = ResolveItem(items, Children(items, "hover"), menuColor),
                .active        = ResolveItem(Concat(active, items), Concat(Children(active, "hover"), Concat(active, Children(items, "hover"))), menuColor),
                .scrollbar =
                    {
                        .widthDips  = Read(scrollbar, "width", ParsePositiveLength, 0.0f),
                        .background = ResolveColor(scrollbar, "background", kTransparent),
                        .radiusDips = Read(scrollbar, "radius", ParseLength, 0.0f),
                        .margin     = ResolveSpacing(scrollbar, "margin"),
                    },
            },
    };
}

ModuleStyle ResolveModuleStyle(const StyleSheet& sheet, const StyleConfig& bar, ModuleKind kind, std::string_view name, const std::vector<std::string>& classes) {
    const StyleNode* own = TopLevel(sheet, name);
    if (own != nullptr) {
        RequireDrawnBlocks(*own, kind, classes, sheet.source);
    }
    const Chain              module   = ChainOf({own, TopLevel(sheet, "module")});
    const BoxStyle           box      = ResolveBox(module);
    const D2D1_COLOR_F       color    = ResolveColor(module, "color", bar.color);
    const Chain              icon     = Children(module, "icon");
    const Chain              items    = Children(module, "item");
    const float              rotation = Read(icon, "rotate", ParseSignedNumber, 0.0f);
    std::vector<ClassColors> classColors;
    classColors.reserve(classes.size());
    for (const std::string& declared : classes) {
        classColors.push_back(ClassColors{.name = declared, .colors = ResolveStateColors(module, declared, color)});
    }
    return ModuleStyle{
        .box                  = box,
        .margin               = ResolveSpacing(module, "margin"),
        .hover                = ResolveColor(Children(module, "hover"), "background", box.background),
        .backgroundTransition = Read(module, "transition", TransitionReader("background"), kNoTransition),
        .color                = color,
        .alertColor           = ResolveColor(Children(module, "alert"), "color", color),
        .warning              = ResolveStateColors(module, "warning", color),
        .critical             = ResolveStateColors(module, "critical", color),
        .muted                = ResolveStateColors(module, "muted", color),
        .classes              = std::move(classColors),
        .icon =
            {
                .font            = Read(icon, "font", ParseFamily, bar.font),
                .sizeDips        = Read(icon, "size", ParsePositiveLength, bar.fontSizeDips),
                .color           = ResolveColor(icon, "color", color),
                .gapDips         = Read(icon, "margin-right", ParseLength, 0.0f),
                .rotationDegrees = rotation,
                .transition      = Read(icon, "transition", TransitionReader("rotate"), kNoTransition),
            },
        .item                        = ResolveItem(items, Children(items, "hover"), color),
        .focused                     = ResolveItemState(items, "focused", color),
        .occupied                    = ResolveItemState(items, "occupied", color),
        .empty                       = ResolveItemState(items, "empty", color),
        .toggle                      = ResolveItemState(items, "toggle", color),
        .transition                  = Read(module, "transition", TransitionReader("width"), kNoTransition),
        .expandedIconRotationDegrees = Read(Children(Children(module, "expanded"), "icon"), "rotate", ParseSignedNumber, rotation),
    };
}
