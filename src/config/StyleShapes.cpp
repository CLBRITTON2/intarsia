#include "config/StyleShapes.hpp"

#include "config/StyleTree.hpp"
#include "config/StyleValues.hpp"
#include "platform/Text.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <initializer_list>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

    // What one block may hold: its properties, its blocks by key, and what its transition may animate. A module's block
    // also holds its custom module's classes, any other block, each of the shape classes points to.
    struct BlockShape;

    struct ChildBlock {
        std::string_view  key;
        const BlockShape* shape;
    };

    struct BlockShape {
        std::span<const std::string_view> properties;
        std::span<const ChildBlock>       blocks;
        std::span<const std::string_view> animated;
        const BlockShape*                 classes = nullptr;
    };

    constexpr BlockShape PropertiesOnly(std::span<const std::string_view> properties) {
        return BlockShape{
            .properties = properties,
            .blocks     = {},
            .animated   = {},
            .classes    = nullptr,
        };
    }

    constexpr BlockShape WithBlocks(std::span<const std::string_view> properties, std::span<const ChildBlock> blocks) {
        return BlockShape{
            .properties = properties,
            .blocks     = blocks,
            .animated   = {},
            .classes    = nullptr,
        };
    }

    constexpr std::array<std::string_view, 2>  kColors{"color", "background"};
    constexpr std::array<std::string_view, 1>  kColor{"color"};
    constexpr std::array<std::string_view, 1>  kBackground{"background"};
    constexpr std::array<std::string_view, 1>  kRotate{"rotate"};
    constexpr std::array<std::string_view, 2>  kBackgroundAndWidth{"background", "width"};
    constexpr std::array<std::string_view, 3>  kBarProperties{"font", "size", "color"};
    constexpr std::array<std::string_view, 13> kSectionProperties{"background", "border", "border-top", "border-right", "border-bottom", "border-left", "radius",
                                                                  "padding",    "shadow", "margin",     "blur",         "brightness",    "saturate"};
    constexpr std::array<std::string_view, 13> kTooltipProperties{"background", "border", "border-top", "border-right", "border-bottom", "border-left", "radius",
                                                                  "padding",    "shadow", "color",      "blur",         "brightness",    "saturate"};
    constexpr std::array<std::string_view, 15> kMenuProperties{"background", "border", "border-top", "border-right", "border-bottom", "border-left", "radius",    "padding",
                                                               "shadow",     "color",  "blur",       "brightness",   "saturate",      "max-width",   "max-height"};
    constexpr std::array<std::string_view, 12> kModuleProperties{"background", "border",  "border-top", "border-right", "border-bottom", "border-left",
                                                                 "radius",     "padding", "shadow",     "margin",       "color",         "transition"};
    constexpr std::array<std::string_view, 7>  kMenuItemProperties{"color", "background", "weight", "padding", "margin", "radius", "shadow"};
    constexpr std::array<std::string_view, 8>  kItemProperties{"color", "background", "weight", "padding", "margin", "radius", "shadow", "transition"};
    constexpr std::array<std::string_view, 4>  kScrollbarProperties{"width", "background", "radius", "margin"};
    constexpr std::array<std::string_view, 6>  kIconProperties{"font", "size", "color", "margin-right", "rotate", "transition"};
    constexpr std::array<std::string_view, 4>  kDefaultIconProperties{"font", "size", "color", "margin-right"};

    constexpr BlockShape                       kBar       = PropertiesOnly(kBarProperties);
    constexpr BlockShape                       kSection   = PropertiesOnly(kSectionProperties);
    constexpr BlockShape                       kTooltip   = PropertiesOnly(kTooltipProperties);
    constexpr BlockShape                       kScrollbar = PropertiesOnly(kScrollbarProperties);
    constexpr BlockShape                       kItemHover = PropertiesOnly(kColors);
    constexpr std::array<ChildBlock, 1>        kMenuActiveBlocks{{{.key = "hover", .shape = &kItemHover}}};
    constexpr BlockShape                       kMenuActive = WithBlocks(kMenuItemProperties, kMenuActiveBlocks);
    constexpr std::array<ChildBlock, 2>        kMenuItemBlocks{{{.key = "hover", .shape = &kItemHover}, {.key = "active", .shape = &kMenuActive}}};
    constexpr BlockShape                       kMenuItem = WithBlocks(kMenuItemProperties, kMenuItemBlocks);
    constexpr std::array<ChildBlock, 2>        kMenuBlocks{{{.key = "item", .shape = &kMenuItem}, {.key = "scrollbar", .shape = &kScrollbar}}};
    constexpr BlockShape                       kMenu = WithBlocks(kMenuProperties, kMenuBlocks);

    constexpr BlockShape                       kIcon{
                              .properties = kIconProperties,
                              .blocks     = {},
                              .animated   = kRotate,
                              .classes    = nullptr,
    };
    constexpr BlockShape kDefaultIcon = PropertiesOnly(kDefaultIconProperties);
    constexpr BlockShape kItemState{
        .properties = kItemProperties,
        .blocks     = {},
        .animated   = kColors,
        .classes    = nullptr,
    };
    constexpr std::array<ChildBlock, 5> kItemBlocks{{{.key = "hover", .shape = &kItemHover},
                                                     {.key = "focused", .shape = &kItemState},
                                                     {.key = "occupied", .shape = &kItemState},
                                                     {.key = "empty", .shape = &kItemState},
                                                     {.key = "toggle", .shape = &kItemState}}};
    constexpr BlockShape                kItem{
                       .properties = kItemProperties,
                       .blocks     = kItemBlocks,
                       .animated   = kColors,
                       .classes    = nullptr,
    };
    constexpr BlockShape                kHover     = PropertiesOnly(kBackground);
    constexpr BlockShape                kColorOnly = PropertiesOnly(kColor);
    constexpr std::array<ChildBlock, 1> kStateBlocks{{{.key = "icon", .shape = &kColorOnly}}};
    constexpr BlockShape                kState        = WithBlocks(kColor, kStateBlocks);
    constexpr BlockShape                kExpandedIcon = PropertiesOnly(kRotate);
    constexpr std::array<ChildBlock, 1> kExpandedBlocks{{{.key = "icon", .shape = &kExpandedIcon}}};
    constexpr BlockShape                kExpanded = WithBlocks({}, kExpandedBlocks);
    constexpr std::array<ChildBlock, 8> kModuleBlocks{{{.key = "icon", .shape = &kIcon},
                                                       {.key = "item", .shape = &kItem},
                                                       {.key = "hover", .shape = &kHover},
                                                       {.key = "alert", .shape = &kColorOnly},
                                                       {.key = "warning", .shape = &kState},
                                                       {.key = "critical", .shape = &kState},
                                                       {.key = "muted", .shape = &kState},
                                                       {.key = "expanded", .shape = &kExpanded}}};
    constexpr BlockShape                kModule{
                       .properties = kModuleProperties,
                       .blocks     = kModuleBlocks,
                       .animated   = kBackgroundAndWidth,
                       .classes    = &kState,
    };
    // The module block styles every module, so it holds nothing only a tray, a group, a battery or an audio module draws.
    constexpr std::array<ChildBlock, 4> kDefaultModuleBlocks{
        {{.key = "icon", .shape = &kDefaultIcon}, {.key = "item", .shape = &kItem}, {.key = "hover", .shape = &kHover}, {.key = "alert", .shape = &kColorOnly}}};
    constexpr BlockShape kDefaultModule{
        .properties = kModuleProperties,
        .blocks     = kDefaultModuleBlocks,
        .animated   = kBackground,
        .classes    = &kState,
    };

    constexpr std::array<ChildBlock, 5> kTopBlocks{{{.key = "bar", .shape = &kBar},
                                                    {.key = "section", .shape = &kSection},
                                                    {.key = "module", .shape = &kDefaultModule},
                                                    {.key = "tooltip", .shape = &kTooltip},
                                                    {.key = "menu", .shape = &kMenu}}};

    std::string                         KeysOf(std::span<const ChildBlock> blocks) {
        return JoinNames(blocks | std::views::transform(&ChildBlock::key));
    }

    // The names as an error lists what is allowed, which may be nothing.
    template <std::ranges::input_range Names>
    std::string NamesOrNone(const Names& names) {
        if (std::ranges::empty(names)) {
            return "none";
        }
        return JoinNames(names);
    }

    // The blocks shape holds, as an error lists them.
    std::string HeldBlockNames(const BlockShape& shape) {
        if (shape.blocks.empty()) {
            return "none";
        }
        if (shape.classes == nullptr) {
            return KeysOf(shape.blocks);
        }
        return KeysOf(shape.blocks) + ", or a custom module's class";
    }

    // A block as an error names it, as module > item > focused.
    std::string PathOf(std::string_view parent, std::string_view key) {
        if (parent.empty()) {
            return std::string(key);
        }
        return std::format("{} > {}", parent, key);
    }

    // brightness and saturate tone the system's blur, so either without blur: on would do nothing.
    void RequireBlurForTone(const StyleNode& block, std::string_view path, std::string_view source) {
        const auto blur   = std::ranges::find(block.children, std::string_view("blur"), &StyleNode::key);
        const bool blurOn = blur != block.children.end() && blur->value == "on";
        for (const StyleNode& child : block.children) {
            if ((child.key == "brightness" || child.key == "saturate") && !blurOn) {
                throw StyleError(source, child.line, std::format("{}: {} tones the system's blur, so it takes blur: on beside it", path, child.key));
            }
        }
    }

    void CheckBlock(const StyleNode& block, const BlockShape& shape, std::string_view path, std::string_view source) {
        for (const StyleNode& child : block.children) {
            const bool property   = std::ranges::find(shape.properties, child.key) != shape.properties.end();
            const auto nested     = std::ranges::find(shape.blocks, child.key, &ChildBlock::key);
            const bool classBlock = nested == shape.blocks.end() && shape.classes != nullptr && !IsStyledClass(child.key);
            const auto where      = PathOf(path, child.key);
            if (IsBlock(child)) {
                if (property) {
                    throw StyleError(source, child.line, std::format("{}: {} is a property, written as {}: value", path, child.key, child.key));
                }
                if (nested == shape.blocks.end() && !classBlock) {
                    throw StyleError(source, child.line, std::format("{}: {} is not a block {} holds, which are: {}", path, child.key, path, HeldBlockNames(shape)));
                }
                if (nested != shape.blocks.end()) {
                    CheckBlock(child, *nested->shape, where, source);
                } else {
                    CheckBlock(child, *shape.classes, where, source);
                }
                continue;
            }
            if (nested != shape.blocks.end()) {
                throw StyleError(source, child.line, std::format("{}: {} is a block, as {} {{ ... }}", path, child.key, child.key));
            }
            if (!property) {
                throw StyleError(source, child.line, std::format("{}: {} is not a property {} takes, which are: {}", path, child.key, path, NamesOrNone(shape.properties)));
            }
            try {
                CheckValue(child.key, child.value);
                if (child.key == "transition") {
                    for (const TransitionValue& transition : ParseTransitionValues(child.value)) {
                        if (std::ranges::find(shape.animated, transition.property) == shape.animated.end()) {
                            throw std::invalid_argument(std::format("animates {} here, not {}", JoinNames(shape.animated), transition.property));
                        }
                    }
                }
            } catch (const std::invalid_argument& error) { throw StyleError(source, child.line, std::format("{}: {} {}", path, child.key, error.what())); }
        }
        RequireBlurForTone(block, path, source);
    }

    // A state that colors a module's icon as well as its text, and the one module type that draws it.
    struct OwnedState {
        std::string_view name;
        ModuleKind       owner;
    };

    constexpr std::array<OwnedState, 3> kOwnedStates{
        {{.name = "warning", .owner = ModuleKind::Battery}, {.name = "critical", .owner = ModuleKind::Battery}, {.name = "muted", .owner = ModuleKind::Audio}}};

} // namespace

void CheckTopLevelBlock(const StyleNode& block, std::string_view source) {
    if (!IsBlock(block)) {
        throw StyleError(source, block.line, std::format("{}: the top level holds blocks, as {} {{ ... }}", block.key, block.key));
    }
    const auto top = std::ranges::find(kTopBlocks, block.key, &ChildBlock::key);
    if (top != kTopBlocks.end()) {
        CheckBlock(block, *top->shape, block.key, source);
    } else {
        CheckBlock(block, kModule, block.key, source);
    }
}

void RequireDrawnBlocks(const StyleNode& own, ModuleKind kind, const std::vector<std::string>& classes, std::string_view source) {
    const std::string_view type = TypeName(kind);
    for (const OwnedState& state : kOwnedStates) {
        if (const StyleNode* block = Child(&own, state.name); block != nullptr && kind != state.owner) {
            throw StyleError(source, block->line,
                             std::format("{} > {}: {} colors only {} modules, and {} is a {} module", own.key, state.name, state.name, TypeName(state.owner), own.key, type));
        }
    }
    if (kind != ModuleKind::Tray && kind != ModuleKind::Group) {
        const StyleNode* icon = Child(&own, "icon");
        for (const StyleNode* drawn : {Child(&own, "expanded"), Child(icon, "rotate"), Child(icon, "transition")}) {
            if (drawn != nullptr) {
                throw StyleError(source, drawn->line, std::format("{}: {} animates a tray or group as it unfolds, and {} is a {} module", own.key, drawn->key, own.key, type));
            }
        }
        const StyleNode* transition = Child(&own, "transition");
        if (transition != nullptr && std::ranges::any_of(ParseTransitionValues(transition->value), [](const TransitionValue& value) { return value.property == "width"; })) {
            throw StyleError(source, transition->line, std::format("{}: transition: width animates a tray or group as it unfolds, and {} is a {} module", own.key, own.key, type));
        }
    }
    for (const StyleNode& child : own.children) {
        if (IsBlock(child) && !IsStyledClass(child.key) && std::ranges::find(classes, child.key) == classes.end()) {
            throw StyleError(source, child.line,
                             std::format("{}: {} is not a block a module holds ({}), nor a class {} lists in its classes", own.key, child.key, KeysOf(kModuleBlocks), own.key));
        }
    }
}

void RequireKnownModules(const StyleSheet& sheet, const std::vector<std::string>& names) {
    for (const StyleNode& block : sheet.blocks) {
        if (!IsReservedModuleName(block.key) && std::ranges::find(names, block.key) == names.end()) {
            throw StyleError(sheet.source, block.line,
                             std::format("{}: names no module, and the top level holds vars, bar, section, module, tooltip, menu and the modules the config defines: {}", block.key,
                                         NamesOrNone(names)));
        }
    }
}

bool IsReservedModuleName(std::string_view name) {
    return name == "vars" || std::ranges::find(kTopBlocks, name, &ChildBlock::key) != kTopBlocks.end();
}

bool IsStyledClass(std::string_view name) {
    return std::ranges::find(kModuleBlocks, name, &ChildBlock::key) != kModuleBlocks.end();
}

void RequireDeclaredClasses(const StyleSheet& sheet, const std::vector<std::string>& declared) {
    const StyleNode* module = TopLevel(sheet, "module");
    if (module == nullptr) {
        return;
    }
    for (const StyleNode& child : module->children) {
        const bool known = std::ranges::find(kDefaultModuleBlocks, child.key, &ChildBlock::key) != kDefaultModuleBlocks.end();
        if (IsBlock(child) && !known && std::ranges::find(declared, child.key) == declared.end()) {
            throw StyleError(sheet.source, child.line,
                             std::format("module: {} is not a block module holds ({}), nor a class a custom module lists in its classes", child.key, KeysOf(kDefaultModuleBlocks)));
        }
    }
}
