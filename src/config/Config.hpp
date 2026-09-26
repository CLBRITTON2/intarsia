#pragma once

#include "bar/Sections.hpp"
#include "platform/AppBar.hpp"
#include "platform/Process.hpp"

#include <d2d1.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

// Where the bar sits among other windows, as Waybar's layer option.
enum class Layer {
    Top,   // above other windows, lowered while a fullscreen app runs, as the taskbar does
    Bottom // below other windows, which can cover it
};

struct BarConfig {
    Edge  edge;
    Layer layer;
    float thicknessDips;
    float borderDips;
    float spacingDips;
};

struct FontConfig {
    std::wstring family;
    float        textSizeDips;
    float        iconSizeDips;
};

struct ColorConfig {
    D2D1_COLOR_F background;
    D2D1_COLOR_F border;
    D2D1_COLOR_F foreground;
    D2D1_COLOR_F accent;
    D2D1_COLOR_F alert; // for states that need attention, such as a disconnected window manager
};

enum class WindowManagerKind {
    Windows, // no window manager: the native backend reads windows and focus from Win32 events
    GlazeWm
};

enum class ModuleKind {
    Clock,
    Window,
    Workspaces,
    Windows,
    BindingMode,
    TilingDirection,
    Cpu,
    Memory,
    Custom
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
enum class CustomOutput {
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

// The settings from a module's table: workspaces, windows and binding_mode have none.
using ModuleSettings = std::variant<std::monostate, TextModuleConfig, WindowConfig, CustomConfig, TilingDirectionConfig>;

// One entry of a _modules list: a name the user chose, defined in the config's modules table with its type.
struct ModuleInstance {
    std::string    name;
    ModuleKind     kind;
    ModuleSettings settings;
};

struct Config {
    BarConfig                bar;
    FontConfig               font;
    ColorConfig              colors;
    WindowManagerKind        windowManager;
    Sections<ModuleInstance> modules;
};

class ConfigError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// %USERPROFILE%\.config\intarsia\config.lua
std::filesystem::path DefaultConfigPath();

// Runs the file as Lua and reads the table it returns. Every key is required and unknown keys are rejected.
Config LoadConfig(const std::filesystem::path& path);
