#include "config.h"

#include "errors.h"
#include "text.h"

#include <lua.hpp>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string_view>
#include <utility>

namespace {

    using LuaState = std::unique_ptr<lua_State, decltype(&lua_close)>;

    // Reads one Lua table with raw access only: metamethods could raise a Lua error, which longjmps past C++ frames.
    class TableReader {
      public:
        TableReader(lua_State* lua, int index, std::string source, std::string path) : lua_(lua), index_(index), source_(std::move(source)), path_(std::move(path)) {}

        TableReader Table(std::string_view key) const {
            PushField(key, LUA_TTABLE, "a table");
            return TableReader(lua_, lua_gettop(lua_), source_, KeyPath(key));
        }

        std::string String(std::string_view key) const {
            PushField(key, LUA_TSTRING, "a string");
            size_t      length = 0;
            const char* data   = lua_tolstring(lua_, -1, &length);
            std::string value(data, length);
            lua_pop(lua_, 1);
            return value;
        }

        float Number(std::string_view key) const {
            PushField(key, LUA_TNUMBER, "a number");
            const auto value = static_cast<float>(lua_tonumber(lua_, -1));
            lua_pop(lua_, 1);
            return value;
        }

        void RejectUnknownKeys(std::initializer_list<std::string_view> known) const {
            lua_pushnil(lua_);
            while (lua_next(lua_, index_) != 0) {
                if (lua_type(lua_, -2) != LUA_TSTRING) {
                    throw Error(std::format("{} has a key that is not a string", Describe()));
                }
                size_t                 length = 0;
                const char*            data   = lua_tolstring(lua_, -2, &length);
                const std::string_view key(data, length);
                if (std::ranges::find(known, key) == known.end()) {
                    throw Error(std::format("{} is not a known key", KeyPath(key)));
                }
                lua_pop(lua_, 1);
            }
        }

        std::string KeyPath(std::string_view key) const {
            return path_.empty() ? std::string(key) : std::format("{}.{}", path_, key);
        }

        ConfigError Error(std::string_view message) const {
            return ConfigError(std::format("{}: {}", source_, message));
        }

      private:
        std::string Describe() const {
            return path_.empty() ? std::string("the returned table") : path_;
        }

        void PushField(std::string_view key, int expectedType, std::string_view expectedName) const {
            lua_pushlstring(lua_, key.data(), key.size());
            const int type = lua_rawget(lua_, index_);
            if (type == LUA_TNIL) {
                throw Error(std::format("{} is missing", KeyPath(key)));
            }
            if (type != expectedType) {
                throw Error(std::format("{} must be {}, got {}", KeyPath(key), expectedName, lua_typename(lua_, type)));
            }
        }

        lua_State*  lua_;
        int         index_;
        std::string source_;
        std::string path_;
    };

    float PositiveNumber(const TableReader& table, std::string_view key) {
        const float value = table.Number(key);
        if (!(value > 0.0f)) {
            throw table.Error(std::format("{} must be greater than 0, got {}", table.KeyPath(key), value));
        }
        return value;
    }

    float NonNegativeNumber(const TableReader& table, std::string_view key) {
        const float value = table.Number(key);
        if (!(value >= 0.0f)) {
            throw table.Error(std::format("{} must be 0 or more, got {}", table.KeyPath(key), value));
        }
        return value;
    }

    Edge ParseEdge(const TableReader& table, std::string_view key) {
        const std::string value = table.String(key);
        if (value == "top")
            return Edge::Top;
        if (value == "bottom")
            return Edge::Bottom;
        if (value == "left")
            return Edge::Left;
        if (value == "right")
            return Edge::Right;
        throw table.Error(std::format("{} must be one of top, bottom, left, right, got '{}'", table.KeyPath(key), value));
    }

    // "#RRGGBB" or "#RRGGBBAA".
    D2D1_COLOR_F ReadColor(const TableReader& table, std::string_view key) {
        const std::string text = table.String(key);
        if ((text.size() != 7 && text.size() != 9) || text.front() != '#') {
            throw table.Error(std::format("{} must be '#RRGGBB' or '#RRGGBBAA', got '{}'", table.KeyPath(key), text));
        }

        std::array<float, 4> channels{0.0f, 0.0f, 0.0f, 1.0f};
        const size_t         count = (text.size() - 1) / 2;
        for (size_t channel = 0; channel < count; ++channel) {
            const std::string_view digits = std::string_view(text).substr(1 + (channel * 2), 2);
            unsigned int           value  = 0;
            const auto [end, error]       = std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
            if (error != std::errc() || end != digits.data() + digits.size()) {
                throw table.Error(std::format("{} has invalid hex digits '{}' in '{}'", table.KeyPath(key), digits, text));
            }
            channels.at(channel) = static_cast<float>(value) / 255.0f;
        }
        return D2D1_COLOR_F{.r = channels[0], .g = channels[1], .b = channels[2], .a = channels[3]};
    }

    std::string ReadFile(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw ConfigError(std::format("cannot open config file {}", ToUtf8(path.wstring())));
        }
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }

    void OpenLibrary(lua_State* lua, const char* name, lua_CFunction open) {
        luaL_requiref(lua, name, open, 1);
        lua_pop(lua, 1);
    }

    std::string PopErrorMessage(lua_State* lua) {
        const char* message = lua_tostring(lua, -1);
        std::string text    = message == nullptr ? std::string("non-string Lua error") : std::string(message);
        lua_pop(lua, 1);
        return text;
    }

} // namespace

std::filesystem::path DefaultConfigPath() {
    PWSTR profile = nullptr;
    ThrowIfFailed(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &profile), "SHGetKnownFolderPath(FOLDERID_Profile)");
    const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(profile, &CoTaskMemFree);
    return std::filesystem::path(owned.get()) / L".config" / L"intarsia" / L"config.lua";
}

Config LoadConfig(const std::filesystem::path& path) {
    const std::string source = ToUtf8(path.wstring());
    const std::string chunk  = ReadFile(path);

    const LuaState    lua(luaL_newstate(), &lua_close);
    if (lua == nullptr) {
        throw ConfigError("luaL_newstate failed to allocate a Lua state");
    }
    // Only libraries that cannot reach the filesystem or other processes.
    OpenLibrary(lua.get(), LUA_GNAME, luaopen_base);
    OpenLibrary(lua.get(), LUA_STRLIBNAME, luaopen_string);
    OpenLibrary(lua.get(), LUA_TABLIBNAME, luaopen_table);
    OpenLibrary(lua.get(), LUA_MATHLIBNAME, luaopen_math);

    const std::string chunkName = "@" + source;
    if (luaL_loadbufferx(lua.get(), chunk.data(), chunk.size(), chunkName.c_str(), "t") != LUA_OK) {
        throw ConfigError(PopErrorMessage(lua.get()));
    }
    if (lua_pcall(lua.get(), 0, 1, 0) != LUA_OK) {
        throw ConfigError(PopErrorMessage(lua.get()));
    }
    if (lua_type(lua.get(), -1) != LUA_TTABLE) {
        throw ConfigError(std::format("{}: must return a table, got {}", source, luaL_typename(lua.get(), -1)));
    }

    const TableReader root(lua.get(), lua_gettop(lua.get()), source, "");
    root.RejectUnknownKeys({"bar", "font", "colors"});

    const TableReader bar = root.Table("bar");
    bar.RejectUnknownKeys({"edge", "thickness", "border"});
    const BarConfig   barConfig{.edge = ParseEdge(bar, "edge"), .thicknessDips = PositiveNumber(bar, "thickness"), .borderDips = NonNegativeNumber(bar, "border")};

    const TableReader font = root.Table("font");
    font.RejectUnknownKeys({"family", "text_size", "icon_size"});
    FontConfig        fontConfig{.family = ToWide(font.String("family")), .textSizeDips = PositiveNumber(font, "text_size"), .iconSizeDips = PositiveNumber(font, "icon_size")};

    const TableReader colors = root.Table("colors");
    colors.RejectUnknownKeys({"background", "border", "foreground", "accent"});
    const ColorConfig colorConfig{
        .background = ReadColor(colors, "background"), .border = ReadColor(colors, "border"), .foreground = ReadColor(colors, "foreground"), .accent = ReadColor(colors, "accent")};

    return Config{.bar = barConfig, .font = std::move(fontConfig), .colors = colorConfig};
}
