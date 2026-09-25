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

struct BarConfig {
    Edge  edge;
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
    Cpu,
    Memory,
    Custom
};

// What a module runs for each input, from the optional on_click, on_right_click, on_scroll_up and on_scroll_down keys.
// An input with a command replaces the module's own handling of it.
struct ModuleActions {
    std::optional<Command> click;
    std::optional<Command> rightClick;
    std::optional<Command> scrollUp;
    std::optional<Command> scrollDown;
};

// A module that shows an icon and one formatted value, redrawn every interval: clock, cpu, memory.
struct TextModuleConfig {
    std::string          format; // std::format spec for the module's value, such as "{:%H:%M}" or "{}%"
    std::wstring         icon;
    std::chrono::seconds interval;
    ModuleActions        actions;
};

struct WindowConfig {
    size_t        maxLength; // UTF-16 code units of title shown before it is cut with an ellipsis
    ModuleActions actions;
};

// A program run every interval, whose first line of output is shown through format.
struct CustomConfig {
    Command              command;
    std::chrono::seconds timeout; // the command and everything it started are killed after this long
    TextModuleConfig     text;
};

// The settings from a module's table: workspaces has none.
using ModuleSettings = std::variant<std::monostate, TextModuleConfig, WindowConfig, CustomConfig>;

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
