#pragma once

#include "audio/IAudio.hpp"
#include "config/Style.hpp"
#include "platform/AppBar.hpp"
#include "platform/Monitors.hpp"
#include "platform/Process.hpp"

#include <algorithm>
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

// The entries of bar.monitors, each naming monitors that get a bar.
struct AllMonitors {
    bool operator==(const AllMonitors& other) const = default;
};
struct PrimaryMonitor {
    bool operator==(const PrimaryMonitor& other) const = default;
};
struct InternalMonitor {
    bool operator==(const InternalMonitor& other) const = default;
};
struct MonitorWithId {
    std::string id; // MonitorId's form

    bool operator==(const MonitorWithId& other) const = default;
};
using MonitorMatch = std::variant<AllMonitors, PrimaryMonitor, InternalMonitor, MonitorWithId>;

struct BarConfig {
    Edge edge;
    Layer layer;
    Space space;
    float thicknessDips;
    std::vector<MonitorMatch> monitors;
};

enum class ModuleKind : std::uint8_t {
    Clock,
    Window,
    Workspaces,
    Windows,
    BindingMode,
    TilingDirection,
    Cpu,
    Memory,
    Audio,
    Network,
    Battery,
    Tray,
    Custom,
    Counter,
    Group
};

// The module type as the config's type key writes it.
std::string_view TypeName(ModuleKind kind);

enum class WindowManagerKind : std::uint8_t {
    Windows, // no window manager: the native backend reads windows and focus from Win32 events
    GlazeWm
};

// A command sent to the window manager as it is written, such as "focus --next-workspace" for GlazeWM.
struct WindowManagerCommand {
    std::string text;
};

// A popup menu of what command prints, killed after timeout like an interval custom command. Picking an entry starts
// onSelect with the entry's id as one more argument.
struct MenuAction {
    Command command;
    std::chrono::seconds timeout;
    Command onSelect;
};

// What an input does: start a program, send the window manager a command, or open a menu.
using ModuleAction = std::variant<Command, WindowManagerCommand, MenuAction>;

// What a module does for each input, from the optional on_click, on_right_click, on_middle_click, on_scroll_up and
// on_scroll_down keys. An input with an action replaces the module's own handling of it.
struct ModuleActions {
    std::optional<ModuleAction> click;
    std::optional<ModuleAction> rightClick;
    std::optional<ModuleAction> middleClick;
    std::optional<ModuleAction> scrollUp;
    std::optional<ModuleAction> scrollDown;
};

enum class ModuleInput : std::uint8_t {
    LeftClick,
    RightClick,
    MiddleClick,
    ScrollUp,
    ScrollDown
};

// The action an input runs, or nullopt when the config binds none to it.
const std::optional<ModuleAction>& ActionFor(const ModuleActions& actions, ModuleInput input);

// A module that shows an icon and one formatted value, redrawn every interval: clock, cpu, memory.
struct TextModuleConfig {
    std::string format;                 // the module's value by name, such as "{time:%H:%M}" or "{usage}%"
    std::optional<std::string> tooltip; // a second format for the same value, shown on hover, none when absent
    std::wstring icon;
    std::chrono::seconds interval;
    ModuleActions actions;
};

struct WindowConfig {
    size_t maxLength;                   // UTF-16 code units of title shown before it is cut with an ellipsis
    std::optional<std::string> tooltip; // a format of {title}, the full title, shown on hover, none when absent
    ModuleActions actions;
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

// run = "interval": the command runs every interval, and it and everything it started are killed after timeout.
struct IntervalRun {
    std::chrono::seconds timeout;
};

// run = "continuous": the command keeps running, each line it prints replaces the shown value, and it starts again
// interval after it exits.
struct ContinuousRun {};

// A program whose output is shown through format.
struct CustomConfig {
    Command command;
    std::variant<IntervalRun, ContinuousRun> run;
    CustomOutput output;
    // What the json output's "class" may name, each drawn in the module's .name colors. Output naming any other fails.
    std::vector<std::string> classes;
    TextModuleConfig text;
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
    std::wstring path; // English, local: "\GPU Engine(*engtype_3D)\Utilization Percentage"
    CounterCombine combine;
    TextModuleConfig text;
};

// The default output or input device's volume. The formats and tooltip can use {volume} (a whole percent) and {device}
// (its name).
struct AudioConfig {
    AudioFlow flow;
    std::string format;
    std::string formatMuted;
    std::optional<std::string> tooltip;
    std::wstring icon;
    std::wstring iconMuted;
    std::int32_t stepPercent; // the volume change per scroll notch
    ModuleActions actions;    // on_click replaces toggling mute, on_scroll_up and on_scroll_down the volume change
};

// The interface the default route leaves through. The formats and tooltip can use {ssid}, {signal} (a percent),
// {ip}, {interface}, {down} and {up} (rates such as "1.2 MB/s"). An empty format shows the icon alone.
struct NetworkConfig {
    std::string formatWifi;
    std::string formatEthernet; // every link that is not Wi-Fi
    std::string formatDisconnected;
    std::optional<std::string> tooltip;
    std::vector<std::wstring> iconsWifi; // weakest signal to strongest, the range split evenly between them
    std::wstring iconEthernet;
    std::wstring iconDisconnected;
    ModuleActions actions;
};

// The system's batteries taken together. The formats and tooltip can use {capacity} (the charge as a whole percent),
// {time} (hours and minutes left on battery, as "1:05", empty until Windows has an estimate) and {health} (full
// charge capacity against design capacity as a whole percent, empty when the batteries do not report it).
struct BatteryConfig {
    std::string formatDischarging;
    std::string formatCharging;
    std::string formatPlugged; // on external power and not charging: full, or held by a charge limit
    std::optional<std::string> tooltip;
    std::vector<std::wstring> icons; // emptiest to fullest while discharging, the range split evenly between them
    std::wstring iconCharging;
    std::wstring iconPlugged;
    // Discharging at or under these charges draws the module in warning, then critical, colors.
    std::int32_t warningAtPercent;
    std::int32_t criticalAtPercent; // under warningAtPercent
    ModuleActions actions;
};

// What the tray's toggle shows, such as a Nerd Font chevron. How it turns as the tray unfolds is the style's.
struct TrayConfig {
    std::wstring icon;
};

struct ModuleInstance;

// A toggle, such as a Nerd Font glyph, that folds its members away and unfolds them after it. Each member is a module
// no _modules list and no other group shows, and none is a group.
struct GroupConfig {
    std::wstring icon;
    std::vector<ModuleInstance> members; // in the order the group's modules key lists them
};

// The settings from a module's table: workspaces, windows and binding_mode have none.
using ModuleSettings = std::variant<std::monostate, TextModuleConfig, WindowConfig, CustomConfig, TilingDirectionConfig, AudioConfig, NetworkConfig, BatteryConfig, TrayConfig,
                                    CounterConfig, GroupConfig>;

// One entry of a _modules list: a name the user chose, defined in the config's modules table with its type.
struct ModuleInstance {
    std::string name;
    ModuleKind kind;
    ModuleSettings settings;
    ModuleStyle style;
};

// The three sections a bar lays out along its edge: left and center and right on a horizontal bar, top and center and
// bottom on a vertical one.
template <typename T>
struct Sections {
    std::vector<T> left;
    std::vector<T> center;
    std::vector<T> right;
};

template <typename T, typename Predicate>
bool AnyInSections(const Sections<T>& sections, Predicate predicate) {
    return std::ranges::any_of(sections.left, predicate) || std::ranges::any_of(sections.center, predicate) || std::ranges::any_of(sections.right, predicate);
}

template <typename T, typename Visit>
void ForEachInSections(const Sections<T>& sections, Visit visit) {
    std::ranges::for_each(sections.left, visit);
    std::ranges::for_each(sections.center, visit);
    std::ranges::for_each(sections.right, visit);
}

// The sections a bar shows in place of the config's own lists once its edge is at least minLengthDips long.
struct LayoutConfig {
    float minLengthDips{};
    Sections<ModuleInstance> modules;
};

struct Config {
    BarConfig bar;
    StyleConfig style;
    WindowManagerKind windowManager;
    Sections<ModuleInstance> modules;  // for a bar whose edge no layout's minimum reaches
    std::vector<LayoutConfig> layouts; // ascending by minLengthDips
};

// Whether any module the config shows matches: one of its own lists' or of a layout's, or a member of a group one of
// those shows.
template <typename Predicate>
bool AnyModuleInConfig(const Config& config, Predicate predicate) {
    const auto shows = [&predicate](const ModuleInstance& instance) {
        const auto* group = std::get_if<GroupConfig>(&instance.settings);
        return predicate(instance) || (group != nullptr && std::ranges::any_of(group->members, predicate));
    };
    return AnyInSections(config.modules, shows) || std::ranges::any_of(config.layouts, [&shows](const LayoutConfig& layout) { return AnyInSections(layout.modules, shows); });
}

// Every module of the config's own lists, then of each layout, each group followed by its members. A module several
// lists show is visited once per list.
template <typename Visit>
void ForEachModuleInConfig(const Config& config, Visit visit) {
    const auto visitShown = [&visit](const ModuleInstance& instance) {
        visit(instance);
        if (const auto* group = std::get_if<GroupConfig>(&instance.settings)) {
            for (const ModuleInstance& member : group->members) {
                visit(member);
            }
        }
    };
    ForEachInSections(config.modules, visitShown);
    for (const LayoutConfig& layout : config.layouts) {
        ForEachInSections(layout.modules, visitShown);
    }
}

class ConfigError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// %USERPROFILE%\.config\intarsia\config.lua
std::filesystem::path DefaultConfigPath();

// style.conf beside the config.
std::filesystem::path StylePath(const std::filesystem::path& configPath);

// Runs the file as Lua and reads the table it returns, then styles its modules from StylePath. Every config key is
// required, and unknown keys, blocks and properties are rejected.
Config LoadConfig(const std::filesystem::path& path);

// The sections of the last layout whose minimum edgeLengthDips reaches, or the config's own lists when none does.
const Sections<ModuleInstance>& ModulesForEdgeLength(const Config& config, float edgeLengthDips);

// Whether any of bar.monitors matches the monitor description describes.
bool ShowsBarOn(const BarConfig& bar, const MonitorDescription& description);

// Whether any bar can show a module of kind (AnyModuleInConfig).
bool ShowsKind(const Config& config, ModuleKind kind);

// Whether any bar can show an audio module of flow.
bool ShowsAudioFlow(const Config& config, AudioFlow flow);
