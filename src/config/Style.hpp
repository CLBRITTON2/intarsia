#pragma once

#include <d2d1.h>
#include <dwrite.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Defined in config/Config.hpp, which includes this header for the style its modules hold.
enum class ModuleKind : std::uint8_t;

// Space inside or around a box on screen: y above and below, x left and right.
struct Spacing {
    float yDips;
    float xDips;
};

struct BorderSide {
    float widthDips; // 0 for none
    D2D1_COLOR_F color;
};

// Drawn inside the box, over its padding, so a border never moves what the box holds.
struct Border {
    BorderSide top;
    BorderSide right;
    BorderSide bottom;
    BorderSide left;
};

// One box-shadow, always inset: the inside edge of the box's border, moved by the offset and shrunk by spread, casts
// it onto the box blurred over blur.
struct InsetShadow {
    float xDips;
    float yDips;
    float blurDips;
    float spreadDips; // negative grows the edge
    D2D1_COLOR_F color;
};

// A rounded box with an optional border and inset shadows: a section, a module, a tooltip.
struct BoxStyle {
    D2D1_COLOR_F background;
    Border border;
    float radiusDips; // at most half the box's shorter side
    Spacing padding;
    std::vector<InsetShadow> shadows; // the first on top
};

// What a box's backdrop blurs.
enum class BackdropSource : std::uint8_t {
    None, // nothing shows behind the background
    // Whatever lies behind the window, blurred by the system at a radius it alone sets. It shows nothing while the
    // system's transparency effects are off, and goes for a few seconds after a window is dragged across the box.
    System,
    Wallpaper, // the monitor's desktop wallpaper, under any window, blurred at the backdrop's radius
};

// What shows through a box's background, toned by brightness and saturate.
struct Backdrop {
    BackdropSource source;
    float radiusDips; // the wallpaper's blur as CSS's blur() takes it, a standard deviation, 0 for the other sources
    float brightness; // 0 is black, 1 leaves the blur as it is
    float saturation; // 0 is grey, 1 leaves the blur as it is
    bool operator==(const Backdrop&) const = default;
};

// The box behind each non-empty section (left, center, right).
struct SectionStyle {
    BoxStyle box;
    Spacing margin; // between the bar's edges and the sections
    Backdrop backdrop;
};

struct TooltipStyle {
    BoxStyle box;
    D2D1_COLOR_F color;
    Backdrop backdrop;
};

// Timing functions, each a cubic Bézier from (0, 0) to (1, 1).
enum class TimingFunction : std::uint8_t {
    Linear,
    Ease,
    EaseIn,
    EaseOut,
    EaseInOut
};

// How a change of one property plays out. A zero duration (transition: none) changes at once.
struct Transition {
    std::chrono::milliseconds duration;
    TimingFunction timing;
};

// A module's icon, drawn before its text in its own font, such as a Nerd Font's proportional variant.
struct IconStyle {
    std::wstring font;
    float sizeDips; // also the size of a window's icon in the windows module
    D2D1_COLOR_F color;
    float gapDips;         // between the icon and the text after it
    float rotationDegrees; // clockwise about the icon's center, a tray's or group's only
    Transition transition; // of the rotation
};

// One item's box inside a module: a workspace, a window's icon, a binding mode.
struct ItemStyle {
    D2D1_COLOR_F color;
    D2D1_COLOR_F background;
    DWRITE_FONT_WEIGHT weight;
    Spacing padding;
    Spacing margin; // x on each side of the box, so two items side by side are 2x apart
    float radiusDips;
    std::vector<InsetShadow> shadows;
    // The item under the cursor, from item > hover blocks.
    D2D1_COLOR_F hoverColor;
    D2D1_COLOR_F hoverBackground;
    // How the item fades into this style's colors, from the hover or another state.
    Transition backgroundTransition;
    Transition colorTransition;
};

// The thumb a menu draws beside its entries while they overflow its box, from menu > scrollbar.
struct ScrollbarStyle {
    float widthDips; // 0 draws none, and the entries still scroll by wheel and arrow keys
    D2D1_COLOR_F background;
    float radiusDips;
    Spacing margin; // x on each side of the thumb, y at each end of its track
};

// A popup menu's box and its entries (menu > item), the entry the listing marks active from menu > item > active.
// The entry under the cursor or picked with the arrow keys takes its style's hover colors.
struct MenuStyle {
    BoxStyle box;
    D2D1_COLOR_F color;
    Backdrop backdrop;
    std::optional<float> maxWidthDips;  // of the whole box, which never outgrows its monitor either
    std::optional<float> maxHeightDips; // the entries scroll past it
    ItemStyle item;
    ItemStyle active;
    ScrollbarStyle scrollbar;
};

// What style.conf sets for the whole bar. Modules inherit the font and color.
struct StyleConfig {
    std::wstring font;
    float fontSizeDips;
    D2D1_COLOR_F color;
    SectionStyle section;
    TooltipStyle tooltip;
    MenuStyle menu;
};

// The text and icon colors of one state a module's value can be in.
struct StateColors {
    D2D1_COLOR_F text;
    D2D1_COLOR_F icon;
};

// A class a custom module declares, as its style colors it (weather > sunny, weather > sunny > icon).
struct ClassColors {
    std::string name;
    StateColors colors;
};

// One module's style, from its own style.conf block over the module block.
struct ModuleStyle {
    BoxStyle box;
    Spacing margin;                  // around the box, which it shrinks across the bar and spaces from its neighbours along it
    D2D1_COLOR_F hover;              // the box's background under the cursor
    Transition backgroundTransition; // between the background and the hover
    D2D1_COLOR_F color;
    D2D1_COLOR_F alertColor; // text in a state that needs attention, such as a failed command
    // A battery discharging at or under warning_at (warning) and critical_at (critical).
    StateColors warning;
    StateColors critical;
    StateColors muted; // an audio output that is muted
    // A custom module's declared classes, in the order its classes key lists them.
    std::vector<ClassColors> classes;
    IconStyle icon;
    // The box of a windows or binding_mode item, and of a focused window. Workspaces are focused, occupied (has
    // windows) or empty.
    ItemStyle item;
    ItemStyle focused;
    ItemStyle occupied;
    ItemStyle empty;
    ItemStyle toggle; // a tray's toggle, from item > toggle over item
    // A tray's or group's: the transition of its width as it unfolds and folds, and its icon's rotation while unfolded
    // (expanded).
    Transition transition;
    float expandedIconRotationDegrees;
};

// One entry of style.conf: a property (key: value), or a block (key { ... }) holding the entries between its braces.
struct StyleNode {
    std::string key;
    std::string value; // $names already replaced, empty for a block
    std::uint32_t line;
    std::vector<StyleNode> children;
};

struct StyleSheet {
    std::string source;            // the file, for error messages
    std::vector<StyleNode> blocks; // every top-level block but vars, in file order
};

// Blocks (key { ... }) of properties (key: value, ended by a line break, ; or }), with // comments and $names from the
// vars block. Every entry is checked against the block it is in, and anything outside the format is a ConfigError
// naming the line, never an entry that silently does nothing.
StyleSheet ParseStyleSheet(std::string_view text, std::string source);

// The bar block, which must set font, size and color, and the section, tooltip and menu blocks.
StyleConfig ResolveBarStyle(const StyleSheet& sheet);

// The block named for the module over the module block, each one's states over both one's base. Color, font and size
// that neither sets inherit from the bar, and the icon's and items' color from the module. classes are those a custom
// module declares, each a block of the module's own that colors it.
ModuleStyle ResolveModuleStyle(const StyleSheet& sheet, const StyleConfig& bar, ModuleKind kind, std::string_view name, const std::vector<std::string>& classes);

// Every top-level block that is not bar, section, module, tooltip or menu names a module the config defines, so a
// misspelled name is an error rather than a block that never applies.
void RequireKnownModules(const StyleSheet& sheet, const std::vector<std::string>& names);

// Whether name is a top-level block of the style's own (bar, section ...), which a module's name would collide with.
bool IsReservedModuleName(std::string_view name);

// Letters, digits, '_' and '-': what module names, class names and style keys are written in.
bool IsNameCharacter(char character);

// Whether name is a block every module may hold (icon, item, hover ...), which a custom module's class would collide
// with.
bool IsStyledClass(std::string_view name);

// Every class block under module must be one some custom module declares, so a misspelled class is an error rather
// than a block that never applies. declared holds every custom module's classes. A module's own block checks its
// classes as it resolves.
void RequireDeclaredClasses(const StyleSheet& sheet, const std::vector<std::string>& declared);
