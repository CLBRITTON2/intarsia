#include "config/Config.hpp"

#include "platform/Errors.hpp"
#include "platform/Text.hpp"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    using LuaState = std::unique_ptr<lua_State, decltype(&lua_close)>;

    // Reads one Lua table with raw access only: a metamethod raising an error outside lua_pcall hits Lua's panic handler,
    // which aborts the process.
    class TableReader {
      public:
        TableReader(lua_State* lua, int index, std::string source, std::string path) : lua_(lua), index_(index), source_(std::move(source)), path_(std::move(path)) {}

        bool Has(std::string_view key) const {
            lua_pushlstring(lua_, key.data(), key.size());
            const int type = lua_rawget(lua_, index_);
            lua_pop(lua_, 1);
            return type != LUA_TNIL;
        }

        TableReader Table(std::string_view key) const {
            PushField(key, LUA_TTABLE, "a table");
            return TableReader(lua_, lua_gettop(lua_), source_, KeyPath(key));
        }

        std::string String(std::string_view key) const {
            PushField(key, LUA_TSTRING, "a string");
            std::string value = StringAt(-1);
            lua_pop(lua_, 1);
            return value;
        }

        float Number(std::string_view key) const {
            PushField(key, LUA_TNUMBER, "a number");
            const auto value = static_cast<float>(lua_tonumber(lua_, -1));
            lua_pop(lua_, 1);
            return value;
        }

        // A sequence of strings such as { "clock", "cpu" }.
        std::vector<std::string> StringList(std::string_view key) const {
            PushField(key, LUA_TTABLE, "a list of strings");
            const int                listIndex = lua_gettop(lua_);
            const auto               length    = static_cast<lua_Integer>(lua_rawlen(lua_, listIndex));
            std::vector<std::string> values;
            values.reserve(static_cast<size_t>(length));
            for (lua_Integer position = 1; position <= length; ++position) {
                if (lua_rawgeti(lua_, listIndex, position) != LUA_TSTRING) {
                    throw Error(std::format("{}[{}] must be a string, got {}", KeyPath(key), position, luaL_typename(lua_, -1)));
                }
                values.push_back(StringAt(-1));
                lua_pop(lua_, 1);
            }
            if (CountKeys(listIndex) != static_cast<size_t>(length)) {
                throw Error(std::format("{} must be a plain list, it has keys other than 1 to {}", KeyPath(key), length));
            }
            lua_pop(lua_, 1);
            return values;
        }

        // Every key of the table, which must all be strings.
        std::vector<std::string> Keys() const {
            std::vector<std::string> keys;
            lua_pushnil(lua_);
            while (lua_next(lua_, index_) != 0) {
                if (lua_type(lua_, -2) != LUA_TSTRING) {
                    throw Error(std::format("{} has a key that is not a string", path_.empty() ? std::string("the returned table") : path_));
                }
                keys.push_back(StringAt(-2));
                lua_pop(lua_, 1);
            }
            return keys;
        }

        void RejectUnknownKeys(const std::vector<std::string_view>& known) const {
            lua_pushnil(lua_);
            while (lua_next(lua_, index_) != 0) {
                if (lua_type(lua_, -2) != LUA_TSTRING) {
                    throw Error(std::format("{} has a key that is not a string", path_.empty() ? std::string("the returned table") : path_));
                }
                const std::string key = StringAt(-2);
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
        std::string StringAt(int index) const {
            size_t      length = 0;
            const char* data   = lua_tolstring(lua_, index, &length);
            return {data, length};
        }

        size_t CountKeys(int tableIndex) const {
            size_t count = 0;
            lua_pushnil(lua_);
            while (lua_next(lua_, tableIndex) != 0) {
                ++count;
                lua_pop(lua_, 1);
            }
            return count;
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

    long long PositiveWholeNumber(const TableReader& table, std::string_view key) {
        const float value = PositiveNumber(table, key);
        if (value != std::floor(value)) {
            throw table.Error(std::format("{} must be a whole number, got {}", table.KeyPath(key), value));
        }
        return static_cast<long long>(value);
    }

    WindowManagerKind ParseWindowManager(const TableReader& table, std::string_view key) {
        const std::string value = table.String(key);
        if (value == "windows")
            return WindowManagerKind::Windows;
        if (value == "glazewm")
            return WindowManagerKind::GlazeWm;
        throw table.Error(std::format("{} must be one of windows, glazewm, got '{}'", table.KeyPath(key), value));
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

    struct ModuleName {
        std::string_view name;
        ModuleKind       kind;
    };

    constexpr std::array<ModuleName, 5> kModuleNames{{
        {.name = "clock", .kind = ModuleKind::Clock},
        {.name = "window", .kind = ModuleKind::Window},
        {.name = "workspaces", .kind = ModuleKind::Workspaces},
        {.name = "cpu", .kind = ModuleKind::Cpu},
        {.name = "memory", .kind = ModuleKind::Memory},
    }};

    std::string                         KnownModuleNames() {
        std::string names;
        for (const ModuleName& module : kModuleNames) {
            names += names.empty() ? "" : ", ";
            names += module.name;
        }
        return names;
    }

    bool IsModuleNameCharacter(char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '_' || character == '-';
    }

    ModuleKind ParseModuleType(const TableReader& module) {
        const std::string type  = module.String("type");
        const auto        entry = std::ranges::find(kModuleNames, type, &ModuleName::name);
        if (entry == kModuleNames.end()) {
            throw module.Error(std::format("{} is '{}', known types are: {}", module.KeyPath("type"), type, KnownModuleNames()));
        }
        return entry->kind;
    }

    TextModuleConfig ReadTextModuleConfig(const TableReader& module);
    WindowConfig     ReadWindowConfig(const TableReader& window);

    ModuleSettings   ReadModuleSettings(const TableReader& module, ModuleKind kind) {
        switch (kind) {
            case ModuleKind::Clock:
            case ModuleKind::Cpu:
            case ModuleKind::Memory: return ReadTextModuleConfig(module);
            case ModuleKind::Window: return ReadWindowConfig(module);
            case ModuleKind::Workspaces: module.RejectUnknownKeys({"type"}); return std::monostate{};
        }
        throw std::invalid_argument(std::format("unknown module kind {}", static_cast<int>(kind)));
    }

    // Every name a modules_ list shows must be defined in the modules table, which holds one table per module.
    std::vector<ModuleInstance> ReadModuleList(const TableReader& root, const TableReader& definitions, std::string_view listKey) {
        std::vector<ModuleInstance> instances;
        for (const std::string& name : root.StringList(listKey)) {
            if (!definitions.Has(name)) {
                throw root.Error(std::format("{} shows '{}', which is not defined in modules", listKey, name));
            }
            const TableReader module = definitions.Table(name);
            const ModuleKind  kind   = ParseModuleType(module);
            instances.push_back(ModuleInstance{.name = name, .kind = kind, .settings = ReadModuleSettings(module, kind)});
        }
        return instances;
    }

    bool Shows(const Sections<ModuleInstance>& modules, std::string_view name) {
        const auto isNamed = [name](const ModuleInstance& instance) { return instance.name == name; };
        return std::ranges::any_of(modules.left, isNamed) || std::ranges::any_of(modules.center, isNamed) || std::ranges::any_of(modules.right, isNamed);
    }

    // A defined module no list shows is an error, so a misspelled name in a list or in modules is caught either way.
    void RejectUnshownModules(const TableReader& definitions, const Sections<ModuleInstance>& modules) {
        for (const std::string& name : definitions.Keys()) {
            if (name.empty() || !std::ranges::all_of(name, IsModuleNameCharacter)) {
                throw definitions.Error(std::format("{} is not a valid module name: use letters, digits, '_' or '-'", definitions.KeyPath(name)));
            }
            if (!Shows(modules, name)) {
                throw definitions.Error(std::format("{} is defined but no modules_ list shows it", definitions.KeyPath(name)));
            }
        }
    }

    bool ListsModule(const Sections<ModuleInstance>& modules, ModuleKind kind) {
        const auto isKind = [kind](const ModuleInstance& instance) { return instance.kind == kind; };
        return std::ranges::any_of(modules.left, isKind) || std::ranges::any_of(modules.center, isKind) || std::ranges::any_of(modules.right, isKind);
    }

    // An action is { program, arguments... }. The program must resolve to an .exe: a .bat or .cmd would run through
    // cmd.exe, which parses the arguments again as shell syntax.
    std::optional<Command> ReadCommand(const TableReader& module, std::string_view key) {
        if (!module.Has(key)) {
            return std::nullopt;
        }
        const std::vector<std::string> argv = module.StringList(key);
        if (argv.empty()) {
            throw module.Error(std::format("{} must name a program, as {{ \"taskmgr.exe\" }}", module.KeyPath(key)));
        }
        const std::optional<std::filesystem::path> program = ResolveProgram(ToWide(argv.front()));
        if (!program.has_value()) {
            throw module.Error(std::format("{} names '{}', which is not an existing file or a program on PATH", module.KeyPath(key), argv.front()));
        }
        std::wstring extension = program->extension().wstring();
        if (CompareStringOrdinal(extension.c_str(), static_cast<int>(extension.size()), L".exe", 4, TRUE) != CSTR_EQUAL) {
            throw module.Error(std::format("{} resolves to {}, which is not an .exe", module.KeyPath(key), ToUtf8(program->wstring())));
        }
        std::vector<std::wstring> arguments;
        arguments.reserve(argv.size() - 1);
        for (size_t index = 1; index < argv.size(); ++index) {
            arguments.push_back(ToWide(argv[index]));
        }
        return Command{.program = *program, .arguments = std::move(arguments)};
    }

    ModuleActions ReadActions(const TableReader& module) {
        return ModuleActions{.click      = ReadCommand(module, "on_click"),
                             .rightClick = ReadCommand(module, "on_right_click"),
                             .scrollUp   = ReadCommand(module, "on_scroll_up"),
                             .scrollDown = ReadCommand(module, "on_scroll_down")};
    }

    TextModuleConfig ReadTextModuleConfig(const TableReader& module) {
        module.RejectUnknownKeys({"type", "format", "icon", "interval", "on_click", "on_right_click", "on_scroll_up", "on_scroll_down"});
        return TextModuleConfig{.format   = module.String("format"),
                                .icon     = ToWide(module.String("icon")),
                                .interval = std::chrono::seconds(PositiveWholeNumber(module, "interval")),
                                .actions  = ReadActions(module)};
    }

    WindowConfig ReadWindowConfig(const TableReader& window) {
        window.RejectUnknownKeys({"type", "max_length", "on_click", "on_right_click", "on_scroll_up", "on_scroll_down"});
        return WindowConfig{.maxLength = static_cast<size_t>(PositiveWholeNumber(window, "max_length")), .actions = ReadActions(window)};
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
    root.RejectUnknownKeys({"bar", "font", "colors", "window_manager", "modules_left", "modules_center", "modules_right", "modules"});

    const TableReader              definitions = root.Table("modules");
    const Sections<ModuleInstance> modules{.left   = ReadModuleList(root, definitions, "modules_left"),
                                           .center = ReadModuleList(root, definitions, "modules_center"),
                                           .right  = ReadModuleList(root, definitions, "modules_right")};
    RejectUnshownModules(definitions, modules);

    const TableReader bar = root.Table("bar");
    bar.RejectUnknownKeys({"edge", "thickness", "border", "spacing"});
    const BarConfig   barConfig{.edge          = ParseEdge(bar, "edge"),
                                .thicknessDips = PositiveNumber(bar, "thickness"),
                                .borderDips    = NonNegativeNumber(bar, "border"),
                                .spacingDips   = NonNegativeNumber(bar, "spacing")};

    const TableReader font = root.Table("font");
    font.RejectUnknownKeys({"family", "text_size", "icon_size"});
    FontConfig        fontConfig{.family = ToWide(font.String("family")), .textSizeDips = PositiveNumber(font, "text_size"), .iconSizeDips = PositiveNumber(font, "icon_size")};

    const TableReader colors = root.Table("colors");
    colors.RejectUnknownKeys({"background", "border", "foreground", "accent", "alert"});
    const ColorConfig       colorConfig{.background = ReadColor(colors, "background"),
                                        .border     = ReadColor(colors, "border"),
                                        .foreground = ReadColor(colors, "foreground"),
                                        .accent     = ReadColor(colors, "accent"),
                                        .alert      = ReadColor(colors, "alert")};

    const WindowManagerKind windowManager = ParseWindowManager(root, "window_manager");

    if (windowManager == WindowManagerKind::Windows && ListsModule(modules, ModuleKind::Workspaces)) {
        throw root.Error("workspaces needs a window manager that has workspaces, window_manager 'windows' has none");
    }

    return Config{.bar = barConfig, .font = std::move(fontConfig), .colors = colorConfig, .windowManager = windowManager, .modules = modules};
}
