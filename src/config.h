#pragma once

#include "appbar.h"

#include <d2d1.h>

#include <filesystem>
#include <stdexcept>
#include <string>

struct BarConfig {
    Edge  edge;
    float thicknessDips;
    float borderDips;
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

struct Config {
    BarConfig   bar;
    FontConfig  font;
    ColorConfig colors;
};

class ConfigError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// %USERPROFILE%\.config\intarsia\config.lua
std::filesystem::path DefaultConfigPath();

// Runs the file as Lua and reads the table it returns. Every key is required and unknown keys are rejected.
Config LoadConfig(const std::filesystem::path& path);
