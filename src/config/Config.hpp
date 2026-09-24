#pragma once

#include "bar/Sections.hpp"
#include "platform/AppBar.hpp"

#include <d2d1.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

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
};

enum class ModuleKind {
    Clock
};

struct ClockConfig {
    std::string          format; // std::format chrono spec, "{:%a %d %b %Y, %H:%M}"
    std::wstring         icon;
    std::chrono::seconds interval;
};

struct Config {
    BarConfig            bar;
    FontConfig           font;
    ColorConfig          colors;
    Sections<ModuleKind> modules;
    // Present whenever a section lists the module, the loader rejects a config that lists it without its table.
    std::optional<ClockConfig> clock;
};

class ConfigError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// %USERPROFILE%\.config\intarsia\config.lua
std::filesystem::path DefaultConfigPath();

// Runs the file as Lua and reads the table it returns. Every key is required and unknown keys are rejected.
Config LoadConfig(const std::filesystem::path& path);
