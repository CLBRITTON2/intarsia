#include "config/Config.hpp"

#include "config/Style.hpp"
#include "platform/Errors.hpp"
#include "platform/Text.hpp"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <shlobj.h>

#include <cstdint>
#include <algorithm>
#include <array>
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
        TableReader(lua_State* lua, std::int32_t index, std::string source, std::string path) : _lua(lua), _index(index), _source(std::move(source)), _path(std::move(path)) {}

        bool Has(std::string_view key) const {
            Reserve(1);
            lua_pushlstring(_lua, key.data(), key.size());
            const std::int32_t type = lua_rawget(_lua, _index);
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
            const std::int32_t       listIndex = lua_gettop(_lua);
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
        std::string StringAt(std::int32_t index) const {
            size_t      length = 0;
            const char* data   = lua_tolstring(_lua, index, &length);
            return {data, length};
        }

        size_t CountKeys(std::int32_t tableIndex) const {
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
        void Reserve(std::int32_t slots) const {
            if (lua_checkstack(_lua, slots) == 0) {
                throw Error(std::format("Lua cannot grow its stack by {} slots to read {}", slots, _path.empty() ? std::string("the returned table") : _path));
            }
        }

        // Leaves room for StringList's element on top of the field.
        void PushField(std::string_view key, std::int32_t expectedType, std::string_view expectedName) const {
            Reserve(2);
            lua_pushlstring(_lua, key.data(), key.size());
            const std::int32_t type = lua_rawget(_lua, _index);
            if (type == LUA_TNIL) {
                throw Error(std::format("{} is missing", KeyPath(key)));
            }
            if (type != expectedType) {
                throw Error(std::format("{} must be {}, got {}", KeyPath(key), expectedName, lua_typename(_lua, type)));
            }
        }

        lua_State*   _lua;
        std::int32_t _index;
        std::string  _source;
        std::string  _path;
    };

    float PositiveNumber(const TableReader& table, std::string_view key) {
        const float value = table.Number(key);
        if (!(value > 0.0f)) {
            throw table.Error(std::format("{} must be greater than 0, got {}", table.KeyPath(key), value));
        }
        return value;
    }

    std::int64_t PositiveWholeNumber(const TableReader& table, std::string_view key) {
        const float value = PositiveNumber(table, key);
        if (value != std::floor(value)) {
            throw table.Error(std::format("{} must be a whole number, got {}", table.KeyPath(key), value));
        }
        return static_cast<std::int64_t>(value);
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

    std::string KnownModuleNames() {
        std::string names;
        for (const ModuleTypeName& module : kModuleTypes) {
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
        const auto        entry = std::ranges::find(kModuleTypes, type, &ModuleTypeName::name);
        if (entry == kModuleTypes.end()) {
            throw module.Error(std::format("{} is '{}', known types are: {}", module.KeyPath("type"), type, KnownModuleNames()));
        }
        return entry->kind;
    }

    TextModuleConfig ReadTextModuleConfig(const TableReader& module);
    WindowConfig     ReadWindowConfig(const TableReader& window);
    CustomConfig     ReadCustomConfig(const TableReader& module);
    AudioConfig      ReadAudioConfig(const TableReader& module);

    ModuleSettings   ReadModuleSettings(const TableReader& module, ModuleKind kind) {
        switch (kind) {
            case ModuleKind::Clock:
            case ModuleKind::Cpu:
            case ModuleKind::Memory: return ReadTextModuleConfig(module);
            case ModuleKind::Window: return ReadWindowConfig(module);
            case ModuleKind::Custom: return ReadCustomConfig(module);
            case ModuleKind::Audio: return ReadAudioConfig(module);
            case ModuleKind::Workspaces:
            case ModuleKind::Windows:
            case ModuleKind::BindingMode:
            case ModuleKind::Divider: module.RejectUnknownKeys({"type"}); return std::monostate{};
            case ModuleKind::TilingDirection:
                module.RejectUnknownKeys({"type", "horizontal", "vertical"});
                return TilingDirectionConfig{.horizontal = ToWide(module.String("horizontal")), .vertical = ToWide(module.String("vertical"))};
        }
        throw std::invalid_argument(std::format("unknown module kind {}", static_cast<std::int32_t>(kind)));
    }

    // Every name a _modules list shows must be defined in the modules table, which holds one table per module.
    std::vector<ModuleInstance> ReadModuleList(const TableReader& root, const TableReader& definitions, std::string_view listKey, const StyleSheet& sheet,
                                               const StyleConfig& style) {
        std::vector<ModuleInstance> instances;
        for (const std::string& name : root.StringList(listKey)) {
            if (!definitions.Has(name)) {
                throw root.Error(std::format("{} shows '{}', which is not defined in modules", listKey, name));
            }
            const TableReader module = definitions.Table(name);
            const ModuleKind  kind   = ParseModuleType(module);
            instances.push_back(ModuleInstance{.name = name, .kind = kind, .settings = ReadModuleSettings(module, kind), .style = ResolveModuleStyle(sheet, style, kind, name)});
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
        if (CompareStringOrdinal(extension.c_str(), static_cast<std::int32_t>(extension.size()), L".exe", 4, TRUE) != CSTR_EQUAL) {
            throw module.Error(std::format("{} resolves to {}, which is not an .exe", module.KeyPath(key), ToUtf8(program->wstring())));
        }
        std::vector<std::wstring> arguments;
        arguments.reserve(argv.size() - 1);
        for (size_t index = 1; index < argv.size(); ++index) {
            arguments.push_back(ToWide(argv[index]));
        }
        return Command{.program = *program, .arguments = std::move(arguments)};
    }

    // One line of text: a line break or other control character would reach the window manager's command parser.
    WindowManagerCommand ReadWindowManagerCommand(const TableReader& action) {
        action.RejectUnknownKeys({"window_manager"});
        std::string text    = action.String("window_manager");
        const bool  control = std::ranges::any_of(text, [](char character) {
            const auto byte = static_cast<std::uint8_t>(character);
            return byte < 0x20u || byte == 0x7Fu;
        });
        if (text.empty() || control) {
            throw action.Error(std::format("{} must be a command on one line, as \"focus --next-workspace\"", action.KeyPath("window_manager")));
        }
        return WindowManagerCommand{.text = std::move(text)};
    }

    // { window_manager = "..." } sends the window manager a command, and a list starts a program.
    std::optional<ModuleAction> ReadAction(const TableReader& module, std::string_view key) {
        if (!module.Has(key)) {
            return std::nullopt;
        }
        const TableReader action = module.Table(key);
        if (action.Has("window_manager")) {
            return ModuleAction(ReadWindowManagerCommand(action));
        }
        return ModuleAction(ReadCommand(module, key));
    }

    bool SendsWindowManagerCommands(const ModuleActions& actions) {
        return std::ranges::any_of(std::array{&actions.click, &actions.rightClick, &actions.scrollUp, &actions.scrollDown},
                                   [](const std::optional<ModuleAction>* action) { return action->has_value() && std::holds_alternative<WindowManagerCommand>(**action); });
    }

    // The actions in a module's settings, or null for a module that takes none.
    const ModuleActions* ActionsOf(const ModuleSettings& settings) {
        if (const auto* text = std::get_if<TextModuleConfig>(&settings)) {
            return &text->actions;
        }
        if (const auto* window = std::get_if<WindowConfig>(&settings)) {
            return &window->actions;
        }
        if (const auto* audio = std::get_if<AudioConfig>(&settings)) {
            return &audio->actions;
        }
        if (const auto* custom = std::get_if<CustomConfig>(&settings)) {
            return &custom->text.actions;
        }
        return nullptr;
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

    AudioConfig ReadAudioConfig(const TableReader& module) {
        module.RejectUnknownKeys(WithOptionalKeys({"type", "format", "format_muted", "icon", "icon_muted", "step"}));
        const std::int64_t step = PositiveWholeNumber(module, "step");
        if (step > 100) {
            throw module.Error(std::format("{} is a percent of full volume, from 1 to 100, got {}", module.KeyPath("step"), step));
        }
        return AudioConfig{.format      = module.String("format"),
                           .formatMuted = module.String("format_muted"),
                           .tooltip     = ReadTooltip(module),
                           .icon        = ToWide(module.String("icon")),
                           .iconMuted   = ToWide(module.String("icon_muted")),
                           .stepPercent = static_cast<std::int32_t>(step),
                           .actions     = ReadActions(module)};
    }

    WindowConfig ReadWindowConfig(const TableReader& window) {
        window.RejectUnknownKeys(WithOptionalKeys({"type", "max_length"}));
        return WindowConfig{.maxLength = static_cast<size_t>(PositiveWholeNumber(window, "max_length")), .tooltip = ReadTooltip(window), .actions = ReadActions(window)};
    }

    // what names the file in the error, as "config" or "style".
    std::string ReadFile(const std::filesystem::path& path, std::string_view what) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw ConfigError(std::format("cannot open {} file {}", what, ToUtf8(path.wstring())));
        }
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }

    // Far more than any real config needs (a few thousand instructions), and still under a second to reach.
    constexpr std::int32_t kMaxConfigInstructions = 50'000'000;

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

std::filesystem::path StylePath(const std::filesystem::path& configPath) {
    return configPath.parent_path() / L"style.css";
}

Config LoadConfig(const std::filesystem::path& path) {
    const std::string source = ToUtf8(path.wstring());
    const std::string chunk  = ReadFile(path, "config");

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
    root.RejectUnknownKeys({"bar", "window_manager", "modules_left", "modules_center", "modules_right", "modules"});

    const std::filesystem::path    stylePath   = StylePath(path);
    const StyleSheet               sheet       = ParseStyleSheet(ReadFile(stylePath, "style"), ToUtf8(stylePath.wstring()));
    StyleConfig                    style       = ResolveBarStyle(sheet);
    const TableReader              definitions = root.Table("modules");
    const Sections<ModuleInstance> modules{.left   = ReadModuleList(root, definitions, "modules_left", sheet, style),
                                           .center = ReadModuleList(root, definitions, "modules_center", sheet, style),
                                           .right  = ReadModuleList(root, definitions, "modules_right", sheet, style)};
    RejectUnshownModules(definitions, modules);
    RequireKnownIds(sheet, definitions.Keys());

    const TableReader bar = root.Table("bar");
    bar.RejectUnknownKeys({"edge", "layer", "thickness"});
    const BarConfig         barConfig{.edge = ParseEdge(bar, "edge"), .layer = ParseLayer(bar, "layer"), .thicknessDips = PositiveNumber(bar, "thickness")};

    const WindowManagerKind windowManager = ParseWindowManager(root, "window_manager");

    for (const auto [kind, name] : {std::pair{ModuleKind::Workspaces, "workspaces"}, std::pair{ModuleKind::Windows, "windows"}, std::pair{ModuleKind::BindingMode, "binding_mode"},
                                    std::pair{ModuleKind::TilingDirection, "tiling_direction"}}) {
        if (windowManager == WindowManagerKind::Windows && ListsModule(modules, kind)) {
            throw root.Error(std::format("{} needs a tiling window manager, window_manager 'windows' is none", name));
        }
    }
    const bool sendsCommands = AnyInSections(modules, [](const ModuleInstance& instance) {
        const ModuleActions* actions = ActionsOf(instance.settings);
        return actions != nullptr && SendsWindowManagerCommands(*actions);
    });
    if (windowManager == WindowManagerKind::Windows && sendsCommands) {
        throw root.Error("a window_manager action needs a window manager that takes commands, window_manager 'windows' takes none");
    }

    return Config{.bar = barConfig, .style = std::move(style), .windowManager = windowManager, .modules = modules};
}
