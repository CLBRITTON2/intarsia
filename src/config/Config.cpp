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
        TableReader(lua_State* lua, int index, std::string source, std::string path) : _lua(lua), _index(index), _source(std::move(source)), _path(std::move(path)) {}

        bool Has(std::string_view key) const {
            Reserve(1);
            lua_pushlstring(_lua, key.data(), key.size());
            const int type = lua_rawget(_lua, _index);
            lua_pop(_lua, 1);
            return type != LUA_TNIL;
        }

        TableReader Table(std::string_view key) const {
            PushField(key, LUA_TTABLE, "a table");
            return TableReader(_lua, lua_gettop(_lua), _source, KeyPath(key));
        }

        std::string String(std::string_view key) const {
            PushField(key, LUA_TSTRING, "a string");
            std::string value = StringAt(-1);
            lua_pop(_lua, 1);
            return value;
        }

        float Number(std::string_view key) const {
            PushField(key, LUA_TNUMBER, "a number");
            const auto value = static_cast<float>(lua_tonumber(_lua, -1));
            lua_pop(_lua, 1);
            return value;
        }

        // A sequence of strings such as { "clock", "cpu" }.
        std::vector<std::string> StringList(std::string_view key) const {
            PushField(key, LUA_TTABLE, "a list of strings");
            const int                listIndex = lua_gettop(_lua);
            const auto               length    = static_cast<lua_Integer>(lua_rawlen(_lua, listIndex));
            std::vector<std::string> values;
            values.reserve(static_cast<size_t>(length));
            for (lua_Integer position = 1; position <= length; ++position) {
                if (lua_rawgeti(_lua, listIndex, position) != LUA_TSTRING) {
                    throw Error(std::format("{}[{}] must be a string, got {}", KeyPath(key), position, luaL_typename(_lua, -1)));
                }
                values.push_back(StringAt(-1));
                lua_pop(_lua, 1);
            }
            if (CountKeys(listIndex) != static_cast<size_t>(length)) {
                throw Error(std::format("{} must be a plain list, it has keys other than 1 to {}", KeyPath(key), length));
            }
            lua_pop(_lua, 1);
            return values;
        }

        // Every key of the table, which must all be strings.
        std::vector<std::string> Keys() const {
            std::vector<std::string> keys;
            Reserve(2);
            lua_pushnil(_lua);
            while (lua_next(_lua, _index) != 0) {
                if (lua_type(_lua, -2) != LUA_TSTRING) {
                    throw Error(std::format("{} has a key that is not a string", _path.empty() ? std::string("the returned table") : _path));
                }
                keys.push_back(StringAt(-2));
                lua_pop(_lua, 1);
            }
            return keys;
        }

        void RejectUnknownKeys(const std::vector<std::string_view>& known) const {
            for (const std::string& key : Keys()) {
                if (std::ranges::find(known, key) == known.end()) {
                    throw Error(std::format("{} is not a known key", KeyPath(key)));
                }
            }
        }

        std::string KeyPath(std::string_view key) const {
            return _path.empty() ? std::string(key) : std::format("{}.{}", _path, key);
        }

        ConfigError Error(std::string_view message) const {
            return ConfigError(std::format("{}: {}", _source, message));
        }

      private:
        std::string StringAt(int index) const {
            size_t      length = 0;
            const char* data   = lua_tolstring(_lua, index, &length);
            return {data, length};
        }

        size_t CountKeys(int tableIndex) const {
            size_t count = 0;
            Reserve(2);
            lua_pushnil(_lua);
            while (lua_next(_lua, tableIndex) != 0) {
                ++count;
                lua_pop(_lua, 1);
            }
            return count;
        }

        // Every table read stays on the stack while its TableReader lives, one slot per module, so the stack can grow past
        // the LUA_MINSTACK slots Lua guarantees. Pushing past the allocated stack is unchecked in a release build.
        void Reserve(int slots) const {
            if (lua_checkstack(_lua, slots) == 0) {
                throw Error(std::format("Lua cannot grow its stack by {} slots to read {}", slots, _path.empty() ? std::string("the returned table") : _path));
            }
        }

        // Leaves room for StringList's element on top of the field.
        void PushField(std::string_view key, int expectedType, std::string_view expectedName) const {
            Reserve(2);
            lua_pushlstring(_lua, key.data(), key.size());
            const int type = lua_rawget(_lua, _index);
            if (type == LUA_TNIL) {
                throw Error(std::format("{} is missing", KeyPath(key)));
            }
            if (type != expectedType) {
                throw Error(std::format("{} must be {}, got {}", KeyPath(key), expectedName, lua_typename(_lua, type)));
            }
        }

        lua_State*  _lua;
        int         _index;
        std::string _source;
        std::string _path;
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

    Layer ParseLayer(const TableReader& table, std::string_view key) {
        const std::string value = table.String(key);
        if (value == "top")
            return Layer::Top;
        if (value == "bottom")
            return Layer::Bottom;
        throw table.Error(std::format("{} must be one of top, bottom, got '{}'", table.KeyPath(key), value));
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

    constexpr std::array<ModuleName, 9> kModuleNames{{
        {.name = "clock", .kind = ModuleKind::Clock},
        {.name = "window", .kind = ModuleKind::Window},
        {.name = "workspaces", .kind = ModuleKind::Workspaces},
        {.name = "windows", .kind = ModuleKind::Windows},
        {.name = "binding_mode", .kind = ModuleKind::BindingMode},
        {.name = "tiling_direction", .kind = ModuleKind::TilingDirection},
        {.name = "cpu", .kind = ModuleKind::Cpu},
        {.name = "memory", .kind = ModuleKind::Memory},
        {.name = "custom", .kind = ModuleKind::Custom},
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
    CustomConfig     ReadCustomConfig(const TableReader& module);

    ModuleSettings   ReadModuleSettings(const TableReader& module, ModuleKind kind) {
        switch (kind) {
            case ModuleKind::Clock:
            case ModuleKind::Cpu:
            case ModuleKind::Memory: return ReadTextModuleConfig(module);
            case ModuleKind::Window: return ReadWindowConfig(module);
            case ModuleKind::Custom: return ReadCustomConfig(module);
            case ModuleKind::Workspaces:
            case ModuleKind::Windows:
            case ModuleKind::BindingMode: module.RejectUnknownKeys({"type"}); return std::monostate{};
            case ModuleKind::TilingDirection:
                module.RejectUnknownKeys({"type", "horizontal", "vertical"});
                return TilingDirectionConfig{.horizontal = ToWide(module.String("horizontal")), .vertical = ToWide(module.String("vertical"))};
        }
        throw std::invalid_argument(std::format("unknown module kind {}", static_cast<int>(kind)));
    }

    // Every name a _modules list shows must be defined in the modules table, which holds one table per module.
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
        return AnyInSections(modules, [name](const ModuleInstance& instance) { return instance.name == name; });
    }

    // A defined module no list shows is an error, so a misspelled name in a list or in modules is caught either way.
    void RejectUnshownModules(const TableReader& definitions, const Sections<ModuleInstance>& modules) {
        for (const std::string& name : definitions.Keys()) {
            if (name.empty() || !std::ranges::all_of(name, IsModuleNameCharacter)) {
                throw definitions.Error(std::format("{} is not a valid module name: use letters, digits, '_' or '-'", definitions.KeyPath(name)));
            }
            if (!Shows(modules, name)) {
                throw definitions.Error(std::format("{} is defined but no _modules list shows it", definitions.KeyPath(name)));
            }
        }
    }

    bool ListsModule(const Sections<ModuleInstance>& modules, ModuleKind kind) {
        return AnyInSections(modules, [kind](const ModuleInstance& instance) { return instance.kind == kind; });
    }

    // A command is { program, arguments... }. The program must resolve to an .exe: a .bat or .cmd would run through
    // cmd.exe, which parses the arguments again as shell syntax.
    Command ReadCommand(const TableReader& module, std::string_view key) {
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

    std::optional<Command> ReadAction(const TableReader& module, std::string_view key) {
        return module.Has(key) ? std::optional<Command>(ReadCommand(module, key)) : std::nullopt;
    }

    ModuleActions ReadActions(const TableReader& module) {
        return ModuleActions{.click      = ReadAction(module, "on_click"),
                             .rightClick = ReadAction(module, "on_right_click"),
                             .scrollUp   = ReadAction(module, "on_scroll_up"),
                             .scrollDown = ReadAction(module, "on_scroll_down")};
    }

    // A module's own keys plus the optional tooltip and input actions every module with settings takes.
    std::vector<std::string_view> WithOptionalKeys(std::initializer_list<std::string_view> keys) {
        std::vector<std::string_view> known(keys);
        known.insert(known.end(), {"tooltip", "on_click", "on_right_click", "on_scroll_up", "on_scroll_down"});
        return known;
    }

    std::optional<std::string> ReadTooltip(const TableReader& module) {
        return module.Has("tooltip") ? std::optional<std::string>(module.String("tooltip")) : std::nullopt;
    }

    TextModuleConfig ReadTextFields(const TableReader& module) {
        return TextModuleConfig{.format   = module.String("format"),
                                .tooltip  = ReadTooltip(module),
                                .icon     = ToWide(module.String("icon")),
                                .interval = std::chrono::seconds(PositiveWholeNumber(module, "interval")),
                                .actions  = ReadActions(module)};
    }

    TextModuleConfig ReadTextModuleConfig(const TableReader& module) {
        module.RejectUnknownKeys(WithOptionalKeys({"type", "format", "icon", "interval"}));
        return ReadTextFields(module);
    }

    CustomOutput ParseCustomOutput(const TableReader& module, std::string_view key) {
        const std::string value = module.String(key);
        if (value == "text")
            return CustomOutput::Text;
        if (value == "json")
            return CustomOutput::Json;
        throw module.Error(std::format("{} must be one of text, json, got '{}'", module.KeyPath(key), value));
    }

    CustomConfig ReadCustomConfig(const TableReader& module) {
        module.RejectUnknownKeys(WithOptionalKeys({"type", "command", "timeout", "output", "format", "icon", "interval"}));
        return CustomConfig{.command = ReadCommand(module, "command"),
                            .timeout = std::chrono::seconds(PositiveWholeNumber(module, "timeout")),
                            .output  = ParseCustomOutput(module, "output"),
                            .text    = ReadTextFields(module)};
    }

    WindowConfig ReadWindowConfig(const TableReader& window) {
        window.RejectUnknownKeys(WithOptionalKeys({"type", "max_length"}));
        return WindowConfig{.maxLength = static_cast<size_t>(PositiveWholeNumber(window, "max_length")), .tooltip = ReadTooltip(window), .actions = ReadActions(window)};
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

    // Far more than any real config needs (a few thousand instructions), and still under a second to reach.
    constexpr int kMaxConfigInstructions = 50'000'000;

    // Called once the count is reached. luaL_error raises inside the lua_pcall, so LoadConfig reports it as a ConfigError.
    void AbortRunawayConfig(lua_State* lua, lua_Debug*) {
        luaL_error(lua, "the config ran %d instructions without finishing, is there an endless loop?", kMaxConfigInstructions);
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
    // Only libraries that cannot reach the filesystem or other processes: base minus its loaders. load goes too, since it
    // accepts precompiled chunks and Lua does not verify bytecode.
    OpenLibrary(lua.get(), LUA_GNAME, luaopen_base);
    OpenLibrary(lua.get(), LUA_STRLIBNAME, luaopen_string);
    OpenLibrary(lua.get(), LUA_TABLIBNAME, luaopen_table);
    OpenLibrary(lua.get(), LUA_MATHLIBNAME, luaopen_math);
    for (const char* loader : {"dofile", "loadfile", "load"}) {
        lua_pushnil(lua.get());
        lua_setglobal(lua.get(), loader);
    }
    // Reload runs on the UI thread, so a config that never finishes would freeze the bar: stop it instead.
    lua_sethook(lua.get(), AbortRunawayConfig, LUA_MASKCOUNT, kMaxConfigInstructions);

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
    bar.RejectUnknownKeys({"edge", "layer", "thickness", "border", "spacing"});
    const BarConfig   barConfig{.edge          = ParseEdge(bar, "edge"),
                                .layer         = ParseLayer(bar, "layer"),
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

    for (const auto [kind, name] : {std::pair{ModuleKind::Workspaces, "workspaces"}, std::pair{ModuleKind::Windows, "windows"}, std::pair{ModuleKind::BindingMode, "binding_mode"},
                                    std::pair{ModuleKind::TilingDirection, "tiling_direction"}}) {
        if (windowManager == WindowManagerKind::Windows && ListsModule(modules, kind)) {
            throw root.Error(std::format("{} needs a tiling window manager, window_manager 'windows' is none", name));
        }
    }

    return Config{.bar = barConfig, .font = std::move(fontConfig), .colors = colorConfig, .windowManager = windowManager, .modules = modules};
}
