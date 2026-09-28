#pragma once

#include "bar/Sections.hpp"
#include "platform/AppBar.hpp"
#include "platform/Process.hpp"

#include <d2d1.h>
#include <dwrite.h>

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

// Where the bar sits among other windows, as Waybar's layer option.
enum class Layer : std::uint8_t {
    Top,   // above other windows, lowered while a fullscreen app runs, as the taskbar does
    Bottom // below other windows, which can cover it
};

struct BarConfig {
    Edge  edge;
    Layer layer;
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

// A module's icon, drawn before its text in its own font, such as a Nerd Font's proportional variant.
struct IconStyle {
    std::wstring font;
    float        sizeDips; // also the size of a window's icon in the windows module
    D2D1_COLOR_F color;
    float        gapDips; // between the icon and the text after it
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

// One module's style, cascaded from every style.css rule that matches it.
struct ModuleStyle {
    BoxStyle     box;
    D2D1_COLOR_F hover; // the box's background under the cursor
    D2D1_COLOR_F color;
    D2D1_COLOR_F alertColor; // text in a state that needs attention, such as a failed command
    IconStyle    icon;
    // The box of a windows or binding_mode item, and of a focused window. Workspaces are focused, occupied (has
    // windows) or empty.
    ItemStyle item;
    ItemStyle focused;
    ItemStyle occupied;
    ItemStyle empty;
    // A divider's line, in its color.
    float lineWidthDips;
    float lineHeight; // a share of the box's height, 0 to 1
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
    Custom
};

// A module type as the config's type key and style.css's type selectors write it.
struct ModuleTypeName {
    std::string_view name;
    ModuleKind       kind;
};

inline constexpr std::array<ModuleTypeName, 13> kModuleTypes{{
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
    {.name = "custom", .kind = ModuleKind::Custom},
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

// What a module does for each input, from the optional on_click, on_right_click, on_scroll_up and on_scroll_down
// keys. An input with an action replaces the module's own handling of it.
struct ModuleActions {
    std::optional<ModuleAction> click;
    std::optional<ModuleAction> rightClick;
    std::optional<ModuleAction> scrollUp;
    std::optional<ModuleAction> scrollDown;
};

// A module that shows an icon and one formatted value, redrawn every interval: clock, cpu, memory.
struct TextModuleConfig {
    std::string                format;  // std::format spec for the module's value, such as "{:%H:%M}" or "{}%"
    std::optional<std::string> tooltip; // a second spec for the same value, shown on hover, none when absent
    std::wstring               icon;
    std::chrono::seconds       interval;
    ModuleActions              actions;
};

struct WindowConfig {
    size_t                     maxLength; // UTF-16 code units of title shown before it is cut with an ellipsis
    std::optional<std::string> tooltip;   // std::format spec for the full title, shown on hover, none when absent
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
    Json  // one object: text, and optionally tooltip and alert, as Waybar's return-type json
};

// A program run every interval, whose output is shown through format.
struct CustomConfig {
    Command              command;
    std::chrono::seconds timeout; // the command and everything it started are killed after this long
    CustomOutput         output;
    TextModuleConfig     text;
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
    std::int32_t               alertAtPercent; // discharging at or under this charge draws the text in .alert
    ModuleActions              actions;
};

// The settings from a module's table: workspaces, windows, binding_mode and divider have none.
using ModuleSettings = std::variant<std::monostate, TextModuleConfig, WindowConfig, CustomConfig, TilingDirectionConfig, AudioConfig, NetworkConfig, BatteryConfig>;

// One entry of a _modules list: a name the user chose, defined in the config's modules table with its type.
struct ModuleInstance {
    std::string    name;
    ModuleKind     kind;
    ModuleSettings settings;
    ModuleStyle    style;
};

struct Config {
    BarConfig                bar;
    StyleConfig              style;
    WindowManagerKind        windowManager;
    Sections<ModuleInstance> modules;
};

class ConfigError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// %USERPROFILE%\.config\intarsia\config.lua
std::filesystem::path DefaultConfigPath();

// style.css beside the config, as Waybar keeps its style.
std::filesystem::path StylePath(const std::filesystem::path& configPath);

// Runs the file as Lua and reads the table it returns, then styles its modules from StylePath. Every config key is
// required, and unknown keys, selectors and properties are rejected.
Config LoadConfig(const std::filesystem::path& path);
