#pragma once

#include "bar/Sections.hpp"
#include "platform/AppBar.hpp"
#include "platform/Process.hpp"

#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Where the bar sits among other windows.
enum class Layer : std::uint8_t {
    Top,   // above other windows, lowered while a fullscreen app runs, as the taskbar does
    Bottom // below other windows, which can cover it
};

// Whether the bar takes its strip out of the monitor's work area.
enum class Space : std::uint8_t {
    Reserve, // maximized windows and the work area stop at the bar
    Share    // the work area keeps the strip, as for a plain window, so a window manager's gap has to make room
};

struct BarConfig {
    Edge  edge;
    Layer layer;
    Space space;
    float thicknessDips;
};

// Space inside or around a box on screen, as CSS's two-value padding and margin: y above and below, x left and right.
struct Spacing {
    float yDips;
    float xDips;
};

// A rounded box with an optional border: a section, a module, a tooltip.
struct BoxStyle {
    D2D1_COLOR_F background;
    D2D1_COLOR_F borderColor;
    float        borderWidthDips; // 0 for none
    float        radiusDips;      // at most half the box's shorter side
    Spacing      padding;
};

// The box behind each non-empty section (left, center, right).
struct SectionStyle {
    BoxStyle box;
    Spacing  margin; // between the bar's edges and the sections
};

struct TooltipStyle {
    BoxStyle     box;
    D2D1_COLOR_F color;
};

// What style.css sets for the whole bar. Modules inherit the font and color.
struct StyleConfig {
    std::wstring font;
    float        fontSizeDips;
    D2D1_COLOR_F color;
    SectionStyle section;
    TooltipStyle tooltip;
};

// CSS's timing functions, each a cubic Bézier from (0, 0) to (1, 1).
enum class TimingFunction : std::uint8_t {
    Linear,
    Ease,
    EaseIn,
    EaseOut,
    EaseInOut
};

// How a change of one property plays out, as CSS's transition. A zero duration (transition: none) changes at once.
struct Transition {
    std::chrono::milliseconds duration;
    TimingFunction            timing;
};

// A module's icon, drawn before its text in its own font, such as a Nerd Font's proportional variant.
struct IconStyle {
    std::wstring font;
    float        sizeDips; // also the size of a window's icon in the windows module
    D2D1_COLOR_F color;
    float        gapDips;         // between the icon and the text after it
    float        rotationDegrees; // transform: rotate(), clockwise about the icon's center, a tray's only
    Transition   transition;      // of the rotation
};

// One item's box inside a module: a workspace, a window's icon, a binding mode.
struct ItemStyle {
    D2D1_COLOR_F       color;
    D2D1_COLOR_F       background;
    DWRITE_FONT_WEIGHT weight;
    Spacing            padding;
    Spacing            margin; // x on each side of the box, so two items side by side are 2x apart
    float              radiusDips;
    // The item under the cursor, from .item:hover rules.
    D2D1_COLOR_F hoverColor;
    D2D1_COLOR_F hoverBackground;
};

// The text and icon colors of one state a module's value can be in.
struct StateColors {
    D2D1_COLOR_F text;
    D2D1_COLOR_F icon;
};

// A class a custom module declares, as its style colors it (#weather.sunny, #weather.sunny .icon).
struct ClassColors {
    std::string name;
    StateColors colors;
};

// One module's style, cascaded from every style.css rule that matches it.
struct ModuleStyle {
    BoxStyle     box;
    D2D1_COLOR_F hover; // the box's background under the cursor
    D2D1_COLOR_F color;
    D2D1_COLOR_F alertColor; // text in a state that needs attention, such as a failed command
    // A battery discharging at or under warning_at (.warning) and critical_at (.critical).
    StateColors warning;
    StateColors critical;
    StateColors muted; // an audio output that is muted (.muted)
    // A custom module's declared classes, in the order its classes key lists them.
    std::vector<ClassColors> classes;
    IconStyle                icon;
    // The box of a windows or binding_mode item, and of a focused window. Workspaces are focused, occupied (has
    // windows) or empty.
    ItemStyle item;
    ItemStyle focused;
    ItemStyle occupied;
    ItemStyle empty;
    ItemStyle toggle; // a tray's toggle, from .item.toggle rules over its .item rules
    // A divider's line, in its color.
    float lineWidthDips;
    float lineHeight; // a share of the box's height, 0 to 1
    // A tray's: the transition of its width as it unfolds and folds, and its icon's rotation while unfolded
    // (.expanded).
    Transition transition;
    float      expandedIconRotationDegrees;
};

enum class ModuleKind : std::uint8_t {
    Clock,
    Window,
    Workspaces,
    Windows,
    BindingMode,
    TilingDirection,
    Divider,
    Cpu,
    Memory,
    Audio,
    Network,
    Battery,
    Tray,
    Custom,
    Counter
};

// A module type as the config's type key and style.css's type selectors write it.
struct ModuleTypeName {
    std::string_view name;
    ModuleKind       kind;
};

inline constexpr std::array<ModuleTypeName, 15> kModuleTypes{{
    {.name = "clock", .kind = ModuleKind::Clock},
    {.name = "window", .kind = ModuleKind::Window},
    {.name = "workspaces", .kind = ModuleKind::Workspaces},
    {.name = "windows", .kind = ModuleKind::Windows},
    {.name = "binding_mode", .kind = ModuleKind::BindingMode},
    {.name = "tiling_direction", .kind = ModuleKind::TilingDirection},
    {.name = "divider", .kind = ModuleKind::Divider},
    {.name = "cpu", .kind = ModuleKind::Cpu},
    {.name = "memory", .kind = ModuleKind::Memory},
    {.name = "audio", .kind = ModuleKind::Audio},
    {.name = "network", .kind = ModuleKind::Network},
    {.name = "battery", .kind = ModuleKind::Battery},
    {.name = "tray", .kind = ModuleKind::Tray},
    {.name = "custom", .kind = ModuleKind::Custom},
    {.name = "counter", .kind = ModuleKind::Counter},
}};

enum class WindowManagerKind : std::uint8_t {
    Windows, // no window manager: the native backend reads windows and focus from Win32 events
    GlazeWm
};

// A command sent to the window manager as it is written, such as "focus --next-workspace" for GlazeWM.
struct WindowManagerCommand {
    std::string text;

    bool        operator==(const WindowManagerCommand&) const = default;
};

// What an input does: start a program, or send the window manager a command.
using ModuleAction = std::variant<Command, WindowManagerCommand>;

// What a module does for each input, from the optional on_click, on_right_click, on_middle_click, on_scroll_up and
// on_scroll_down keys. An input with an action replaces the module's own handling of it.
struct ModuleActions {
    std::optional<ModuleAction> click;
    std::optional<ModuleAction> rightClick;
    std::optional<ModuleAction> middleClick;
    std::optional<ModuleAction> scrollUp;
    std::optional<ModuleAction> scrollDown;
};

// A module that shows an icon and one formatted value, redrawn every interval: clock, cpu, memory.
struct TextModuleConfig {
    std::string                format;  // the module's value by name, such as "{time:%H:%M}" or "{usage}%"
    std::optional<std::string> tooltip; // a second format for the same value, shown on hover, none when absent
    std::wstring               icon;
    std::chrono::seconds       interval;
    ModuleActions              actions;
};

struct WindowConfig {
    size_t                     maxLength; // UTF-16 code units of title shown before it is cut with an ellipsis
    std::optional<std::string> tooltip;   // a format of {title}, the full title, shown on hover, none when absent
    ModuleActions              actions;
};

// What the tiling_direction module shows for each direction, such as a Nerd Font arrow.
struct TilingDirectionConfig {
    std::wstring horizontal;
    std::wstring vertical;
};

// How a custom module reads what its command printed.
enum class CustomOutput : std::uint8_t {
    Text, // the first line is the value
    Json  // one object: text, and optionally tooltip and alert
};

// A program run every interval, whose output is shown through format.
struct CustomConfig {
    Command              command;
    std::chrono::seconds timeout; // the command and everything it started are killed after this long
    CustomOutput         output;
    // What the json output's "class" may name, each drawn in the module's .name colors. Output naming any other fails.
    std::vector<std::string> classes;
    TextModuleConfig         text;
};

// How a counter's instances make one value.
enum class CounterCombine : std::uint8_t {
    Sum,
    Max,
    Average
};

// A performance counter sampled every interval, such as GPU use over every 3D engine. The format and tooltip can use
// {value} (the combined value, a number for a spec such as "{value:.0f}") and {rate} (the value read as bytes per
// second, as "3.4 MB/s").
struct CounterConfig {
    std::wstring     path; // English, local: "\GPU Engine(*engtype_3D)\Utilization Percentage"
    CounterCombine   combine;
    TextModuleConfig text;
};

// The default output device's volume. The formats and tooltip can use {volume} (a whole percent) and {device} (its
// name).
struct AudioConfig {
    std::string                format;
    std::string                formatMuted;
    std::optional<std::string> tooltip;
    std::wstring               icon;
    std::wstring               iconMuted;
    std::int32_t               stepPercent; // the volume change per scroll notch
    ModuleActions              actions;     // on_click replaces toggling mute, on_scroll_up and on_scroll_down the volume change
};

// The interface the default route leaves through. The formats and tooltip can use {ssid}, {signal} (a percent),
// {ip}, {interface}, {down} and {up} (rates such as "1.2 MB/s"). An empty format shows the icon alone.
struct NetworkConfig {
    std::string                formatWifi;
    std::string                formatEthernet; // every link that is not Wi-Fi
    std::string                formatDisconnected;
    std::optional<std::string> tooltip;
    std::vector<std::wstring>  iconsWifi; // weakest signal to strongest, the range split evenly between them
    std::wstring               iconEthernet;
    std::wstring               iconDisconnected;
    ModuleActions              actions;
};

// The system's batteries taken together. The formats and tooltip can use {capacity} (the charge as a whole percent),
// {time} (hours and minutes left on battery, as "1:05", empty until Windows has an estimate) and {health} (full
// charge capacity against design capacity as a whole percent, empty when the batteries do not report it).
struct BatteryConfig {
    std::string                formatDischarging;
    std::string                formatCharging;
    std::string                formatPlugged; // on external power and not charging: full, or held by a charge limit
    std::optional<std::string> tooltip;
    std::vector<std::wstring>  icons; // emptiest to fullest while discharging, the range split evenly between them
    std::wstring               iconCharging;
    std::wstring               iconPlugged;
    // Discharging at or under these charges draws the module in .warning, then .critical, colors.
    std::int32_t  warningAtPercent;
    std::int32_t  criticalAtPercent; // under warningAtPercent
    ModuleActions actions;
};

// What the tray's toggle shows, such as a Nerd Font chevron. How it turns as the tray unfolds is the style's.
struct TrayConfig {
    std::wstring icon;
};

// The settings from a module's table: workspaces, windows, binding_mode and divider have none.
using ModuleSettings =
    std::variant<std::monostate, TextModuleConfig, WindowConfig, CustomConfig, TilingDirectionConfig, AudioConfig, NetworkConfig, BatteryConfig, TrayConfig, CounterConfig>;

// One entry of a _modules list: a name the user chose, defined in the config's modules table with its type.
struct ModuleInstance {
    std::string    name;
    ModuleKind     kind;
    ModuleSettings settings;
    ModuleStyle    style;
};

// The sections a bar shows in place of the config's own lists once its edge is at least minLengthDips long.
struct LayoutConfig {
    float                    minLengthDips;
    Sections<ModuleInstance> modules;
};

struct Config {
    BarConfig                 bar;
    StyleConfig               style;
    WindowManagerKind         windowManager;
    Sections<ModuleInstance>  modules; // for a bar whose edge no layout's minimum reaches
    std::vector<LayoutConfig> layouts; // ascending by minLengthDips
};

// Whether any module of the config's own lists or of any layout matches.
template <typename Predicate>
bool AnyModuleInConfig(const Config& config, Predicate predicate) {
    return AnyInSections(config.modules, predicate) ||
        std::ranges::any_of(config.layouts, [&predicate](const LayoutConfig& layout) { return AnyInSections(layout.modules, predicate); });
}

// Every module of the config's own lists, then of each layout. A module several lists show is visited once per list.
template <typename Visit>
void ForEachModuleInConfig(const Config& config, Visit visit) {
    ForEachInSections(config.modules, visit);
    for (const LayoutConfig& layout : config.layouts) {
        ForEachInSections(layout.modules, visit);
    }
}

class ConfigError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// %USERPROFILE%\.config\intarsia\config.lua
std::filesystem::path DefaultConfigPath();

// style.css beside the config.
std::filesystem::path StylePath(const std::filesystem::path& configPath);

// Runs the file as Lua and reads the table it returns, then styles its modules from StylePath. Every config key is
// required, and unknown keys, selectors and properties are rejected.
Config LoadConfig(const std::filesystem::path& path);

// The sections of the last layout whose minimum edgeLengthDips reaches, or the config's own lists when none does.
const Sections<ModuleInstance>& ModulesForEdgeLength(const Config& config, float edgeLengthDips);
