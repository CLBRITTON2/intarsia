#include "config/Config.hpp"

#include "config/Style.hpp"
#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "platform/KnownFolder.hpp"
#include "platform/PerformanceCounter.hpp"
#include "platform/Text.hpp"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <shlobj.h>

#include <cstdint>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <format>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <ranges>
#include <sstream>
#include <string_view>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace {

    using LuaState = UniqueReleased<lua_State*, &lua_close>;

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
            const std::int32_t listIndex = lua_gettop(_lua);
            const auto         length    = static_cast<lua_Integer>(lua_rawlen(_lua, listIndex));
            RequirePlainList(listIndex, key, length);
            std::vector<std::string> values;
            values.reserve(static_cast<size_t>(length));
            for (lua_Integer position = 1; position <= length; ++position) {
                if (lua_rawgeti(_lua, listIndex, position) != LUA_TSTRING) {
                    throw Error(std::format("{}[{}] must be a string, got {}", KeyPath(key), position, luaL_typename(_lua, -1)));
                }
                values.push_back(StringAt(-1));
                lua_pop(_lua, 1);
            }
            lua_pop(_lua, 1);
            return values;
        }

        // A sequence of tables such as { { min_length = 1921 } }, each read as layouts[1] and so on.
        std::vector<TableReader> TableList(std::string_view key) const {
            PushField(key, LUA_TTABLE, "a list of tables");
            const std::int32_t listIndex = lua_gettop(_lua);
            const auto         length    = static_cast<lua_Integer>(lua_rawlen(_lua, listIndex));
            RequirePlainList(listIndex, key, length);
            std::vector<TableReader> entries;
            entries.reserve(static_cast<size_t>(length));
            for (lua_Integer position = 1; position <= length; ++position) {
                Reserve(1);
                if (lua_rawgeti(_lua, listIndex, position) != LUA_TTABLE) {
                    throw Error(std::format("{}[{}] must be a table, got {}", KeyPath(key), position, luaL_typename(_lua, -1)));
                }
                entries.emplace_back(_lua, lua_gettop(_lua), _source, std::format("{}[{}]", KeyPath(key), position));
            }
            return entries;
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

        void RequirePlainList(std::int32_t listIndex, std::string_view key, lua_Integer length) const {
            if (CountKeys(listIndex) != static_cast<size_t>(length)) {
                throw Error(std::format("{} must be a plain list, it has keys other than 1 to {}", KeyPath(key), length));
            }
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

    // Whole numbers are read through a float, which holds every whole number up to 2^24 exactly.
    constexpr float kMaxWholeNumber = 16777216.0f;

    float           PositiveNumber(const TableReader& table, std::string_view key) {
        const float value = table.Number(key);
        if (!(value > 0.0f) || !std::isfinite(value)) {
            throw table.Error(std::format("{} must be greater than 0, got {}", table.KeyPath(key), value));
        }
        return value;
    }

    std::int64_t PositiveWholeNumber(const TableReader& table, std::string_view key) {
        const float value = PositiveNumber(table, key);
        if (value != std::floor(value) || value > kMaxWholeNumber) {
            throw table.Error(std::format("{} must be a whole number up to {}, got {}", table.KeyPath(key), kMaxWholeNumber, value));
        }
        return static_cast<std::int64_t>(value);
    }

    // Far inside the 32-bit millisecond waits these reach, which wrap past about 49.7 days.
    constexpr std::int64_t kLongestWaitSeconds = 86400;

    std::chrono::seconds   Seconds(const TableReader& table, std::string_view key) {
        const std::int64_t value = PositiveWholeNumber(table, key);
        if (value > kLongestWaitSeconds) {
            throw table.Error(std::format("{} must be at most {} seconds, got {}", table.KeyPath(key), kLongestWaitSeconds, value));
        }
        return std::chrono::seconds(value);
    }

    // Far past any monitor, and small enough that the bar's edges in device pixels fit 32-bit coordinates.
    constexpr float kThickestBarDips = 10000.0f;

    float           BarThickness(const TableReader& bar) {
        const float value = PositiveNumber(bar, "thickness");
        if (value > kThickestBarDips) {
            throw bar.Error(std::format("{} must be at most {}, got {}", bar.KeyPath("thickness"), kThickestBarDips, value));
        }
        return value;
    }

    // One word a config key accepts and the value it stands for.
    template <typename Value>
    struct Keyword {
        std::string_view name;
        Value            value;
    };

    template <typename Value, size_t Count>
    Value ParseKeyword(const TableReader& table, std::string_view key, const std::array<Keyword<Value>, Count>& keywords) {
        const std::string value = table.String(key);
        const auto        entry = std::ranges::find(keywords, value, &Keyword<Value>::name);
        if (entry == keywords.end()) {
            throw table.Error(std::format("{} must be one of {}, got '{}'", table.KeyPath(key), JoinNames(keywords | std::views::transform(&Keyword<Value>::name)), value));
        }
        return entry->value;
    }

    constexpr std::array<Keyword<WindowManagerKind>, 2> kWindowManagers{
        {{.name = "windows", .value = WindowManagerKind::Windows}, {.name = "glazewm", .value = WindowManagerKind::GlazeWm}}};
    constexpr std::array<Keyword<Edge>, 4> kEdges{
        {{.name = "top", .value = Edge::Top}, {.name = "bottom", .value = Edge::Bottom}, {.name = "left", .value = Edge::Left}, {.name = "right", .value = Edge::Right}}};
    constexpr std::array<Keyword<Layer>, 2>        kLayers{{{.name = "top", .value = Layer::Top}, {.name = "bottom", .value = Layer::Bottom}}};
    constexpr std::array<Keyword<Space>, 2>        kSpaces{{{.name = "reserve", .value = Space::Reserve}, {.name = "share", .value = Space::Share}}};
    constexpr std::array<Keyword<CustomOutput>, 2> kCustomOutputs{{{.name = "text", .value = CustomOutput::Text}, {.name = "json", .value = CustomOutput::Json}}};
    enum class CustomRun : std::uint8_t {
        Interval,
        Continuous
    };
    constexpr std::array<Keyword<CustomRun>, 2>      kCustomRuns{{{.name = "interval", .value = CustomRun::Interval}, {.name = "continuous", .value = CustomRun::Continuous}}};
    constexpr std::array<Keyword<CounterCombine>, 3> kCounterCombines{
        {{.name = "sum", .value = CounterCombine::Sum}, {.name = "max", .value = CounterCombine::Max}, {.name = "average", .value = CounterCombine::Average}}};
    constexpr std::array<Keyword<AudioFlow>, 2> kAudioFlows{{{.name = "output", .value = AudioFlow::Output}, {.name = "input", .value = AudioFlow::Input}}};

    ModuleKind                                  ParseModuleType(const TableReader& module) {
        const std::string type  = module.String("type");
        const auto        entry = std::ranges::find(kModuleTypes, type, &ModuleTypeName::name);
        if (entry == kModuleTypes.end()) {
            throw module.Error(
                std::format("{} is '{}', known types are: {}", module.KeyPath("type"), type, JoinNames(kModuleTypes | std::views::transform(&ModuleTypeName::name))));
        }
        return entry->kind;
    }

    TextModuleConfig ReadTextModuleConfig(const TableReader& module);
    WindowConfig     ReadWindowConfig(const TableReader& window);
    CustomConfig     ReadCustomConfig(const TableReader& module);
    CounterConfig    ReadCounterConfig(const TableReader& module);
    AudioConfig      ReadAudioConfig(const TableReader& module);
    NetworkConfig    ReadNetworkConfig(const TableReader& module);
    BatteryConfig    ReadBatteryConfig(const TableReader& module);
    GroupConfig      ReadGroupConfig(const TableReader& group, const TableReader& definitions, const StyleSheet& sheet, const StyleConfig& style);

    // definitions, sheet and style are for a group, whose members are modules of their own.
    ModuleSettings ReadModuleSettings(const TableReader& module, ModuleKind kind, const TableReader& definitions, const StyleSheet& sheet, const StyleConfig& style) {
        switch (kind) {
            case ModuleKind::Clock:
            case ModuleKind::Cpu:
            case ModuleKind::Memory: return ReadTextModuleConfig(module);
            case ModuleKind::Window: return ReadWindowConfig(module);
            case ModuleKind::Custom: return ReadCustomConfig(module);
            case ModuleKind::Counter: return ReadCounterConfig(module);
            case ModuleKind::Audio: return ReadAudioConfig(module);
            case ModuleKind::Network: return ReadNetworkConfig(module);
            case ModuleKind::Battery: return ReadBatteryConfig(module);
            case ModuleKind::Workspaces:
            case ModuleKind::Windows:
            case ModuleKind::BindingMode: module.RejectUnknownKeys({"type"}); return std::monostate{};
            case ModuleKind::TilingDirection:
                module.RejectUnknownKeys({"type", "horizontal", "vertical"});
                return TilingDirectionConfig{.horizontal = ToWide(module.String("horizontal")), .vertical = ToWide(module.String("vertical"))};
            case ModuleKind::Tray: module.RejectUnknownKeys({"type", "icon"}); return TrayConfig{.icon = ToWide(module.String("icon"))};
            case ModuleKind::Group: return ReadGroupConfig(module, definitions, sheet, style);
        }
        throw std::invalid_argument(std::format("unknown module kind {}", static_cast<std::int32_t>(kind)));
    }

    ModuleInstance ReadModuleInstance(const TableReader& definitions, const std::string& name, const TableReader& module, ModuleKind kind, const StyleSheet& sheet,
                                      const StyleConfig& style) {
        ModuleSettings      settings = ReadModuleSettings(module, kind, definitions, sheet, style);
        const CustomConfig* custom   = std::get_if<CustomConfig>(&settings);
        ModuleStyle         styled   = ResolveModuleStyle(sheet, style, kind, name, custom != nullptr ? custom->classes : std::vector<std::string>{});
        return ModuleInstance{.name = name, .kind = kind, .settings = std::move(settings), .style = std::move(styled)};
    }

    // The table of a module a list shows, which must be defined in the modules table, one table per module.
    TableReader DefinedModule(const TableReader& lists, std::string_view listKey, const TableReader& definitions, const std::string& name) {
        if (!definitions.Has(name)) {
            throw lists.Error(std::format("{} shows '{}', which is not defined in modules", lists.KeyPath(listKey), name));
        }
        return definitions.Table(name);
    }

    std::vector<ModuleInstance> ReadModuleList(const TableReader& lists, const TableReader& definitions, std::string_view listKey, const StyleSheet& sheet,
                                               const StyleConfig& style) {
        std::vector<ModuleInstance> instances;
        for (const std::string& name : lists.StringList(listKey)) {
            const TableReader module = DefinedModule(lists, listKey, definitions, name);
            instances.push_back(ReadModuleInstance(definitions, name, module, ParseModuleType(module), sheet, style));
        }
        return instances;
    }

    // A group's members are read before any check that they are shown nowhere else, which needs the whole config.
    GroupConfig ReadGroupConfig(const TableReader& group, const TableReader& definitions, const StyleSheet& sheet, const StyleConfig& style) {
        group.RejectUnknownKeys({"type", "icon", "modules"});
        std::wstring icon = ToWide(group.String("icon"));
        if (icon.empty()) {
            throw group.Error(std::format("{} must be a glyph to click, as \"+\": a group with an empty icon could never unfold", group.KeyPath("icon")));
        }
        const std::vector<std::string> names = group.StringList("modules");
        if (names.empty()) {
            throw group.Error(std::format("{} must name at least one module", group.KeyPath("modules")));
        }
        std::vector<ModuleInstance> members;
        members.reserve(names.size());
        for (const std::string& name : names) {
            if (std::ranges::any_of(members, [&name](const ModuleInstance& member) { return member.name == name; })) {
                throw group.Error(std::format("{} lists '{}' twice", group.KeyPath("modules"), name));
            }
            const TableReader module = DefinedModule(group, "modules", definitions, name);
            const ModuleKind  kind   = ParseModuleType(module);
            if (kind == ModuleKind::Group) {
                throw group.Error(std::format("{} lists '{}', which is a group: groups do not nest", group.KeyPath("modules"), name));
            }
            members.push_back(ReadModuleInstance(definitions, name, module, kind, sheet, style));
        }
        return GroupConfig{.icon = std::move(icon), .members = std::move(members)};
    }

    Sections<ModuleInstance> ReadSections(const TableReader& lists, const TableReader& definitions, const StyleSheet& sheet, const StyleConfig& style) {
        return Sections<ModuleInstance>{.left   = ReadModuleList(lists, definitions, "modules_left", sheet, style),
                                        .center = ReadModuleList(lists, definitions, "modules_center", sheet, style),
                                        .right  = ReadModuleList(lists, definitions, "modules_right", sheet, style)};
    }

    // ModulesForEdgeLength takes the last layout an edge reaches, so each must start past the one before.
    std::vector<LayoutConfig> ReadLayouts(const TableReader& root, const TableReader& definitions, const StyleSheet& sheet, const StyleConfig& style) {
        std::vector<LayoutConfig> layouts;
        for (const TableReader& layout : root.TableList("layouts")) {
            layout.RejectUnknownKeys({"min_length", "modules_left", "modules_center", "modules_right"});
            const float minLength = PositiveNumber(layout, "min_length");
            if (!layouts.empty() && minLength <= layouts.back().minLengthDips) {
                throw layout.Error(
                    std::format("{} is {}, it must be greater than the layout before it ({})", layout.KeyPath("min_length"), minLength, layouts.back().minLengthDips));
            }
            layouts.push_back(LayoutConfig{.minLengthDips = minLength, .modules = ReadSections(layout, definitions, sheet, style)});
        }
        return layouts;
    }

    // A defined module no list shows is an error, so a misspelled name in a list or in modules is caught either way.
    void RejectUnshownModules(const TableReader& definitions, const Config& config) {
        for (const std::string& name : definitions.Keys()) {
            if (name.empty() || !std::ranges::all_of(name, IsIdentCharacter)) {
                throw definitions.Error(std::format("{} is not a valid module name: use letters, digits, '_' or '-'", definitions.KeyPath(name)));
            }
            if (!AnyModuleInConfig(config, [&name](const ModuleInstance& instance) { return instance.name == name; })) {
                throw definitions.Error(std::format("{} is defined but no _modules list or group shows it", definitions.KeyPath(name)));
            }
        }
    }

    // A member shows only through its group: listed in a _modules list or in a second group as well, it would show twice.
    void RequireMembersInOneGroup(const TableReader& definitions, const Config& config) {
        std::vector<const Sections<ModuleInstance>*> lists{&config.modules};
        for (const LayoutConfig& layout : config.layouts) {
            lists.push_back(&layout.modules);
        }
        std::map<std::string, std::string, std::less<>> groupOf;
        for (const Sections<ModuleInstance>* sections : lists) {
            ForEachInSections(*sections, [&](const ModuleInstance& instance) {
                const auto* group = std::get_if<GroupConfig>(&instance.settings);
                if (group == nullptr) {
                    return;
                }
                for (const ModuleInstance& member : group->members) {
                    const auto listed = [&member](const ModuleInstance& shown) { return shown.name == member.name; };
                    if (std::ranges::any_of(lists, [&listed](const Sections<ModuleInstance>* other) { return AnyInSections(*other, listed); })) {
                        throw definitions.Error(
                            std::format("{} is a member of group '{}', so no _modules list may show it as well", definitions.KeyPath(member.name), instance.name));
                    }
                    const auto [entry, added] = groupOf.try_emplace(member.name, instance.name);
                    if (!added && entry->second != instance.name) {
                        throw definitions.Error(std::format("{} is a member of both group '{}' and group '{}', and one group shows it", definitions.KeyPath(member.name),
                                                            entry->second, instance.name));
                    }
                }
            });
        }
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
        const std::wstring extension = program->extension().wstring();
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

    // on_select gets an id from command output as its last argument. cmd.exe parses its whole command line as shell
    // syntax under rules no quoting of that id can satisfy, so an id such as "a&calc" would run calc.
    MenuAction ReadMenuAction(const TableReader& action) {
        action.RejectUnknownKeys({"menu"});
        const TableReader menu = action.Table("menu");
        menu.RejectUnknownKeys({"command", "timeout", "on_select"});
        MenuAction read{.command = ReadCommand(menu, "command"), .timeout = Seconds(menu, "timeout"), .onSelect = ReadCommand(menu, "on_select")};
        if (SameFileName(read.onSelect.program.filename().wstring(), L"cmd.exe")) {
            throw menu.Error(
                std::format("{} may not run cmd.exe, which would read the picked id as shell syntax: name an .exe that takes it as an argument", menu.KeyPath("on_select")));
        }
        return read;
    }

    // { window_manager = "..." } sends the window manager a command, { menu = {...} } opens a menu, and a list starts a
    // program.
    std::optional<ModuleAction> ReadAction(const TableReader& module, std::string_view key) {
        if (!module.Has(key)) {
            return std::nullopt;
        }
        const TableReader action = module.Table(key);
        if (action.Has("window_manager")) {
            return ModuleAction(ReadWindowManagerCommand(action));
        }
        if (action.Has("menu")) {
            return ModuleAction(ReadMenuAction(action));
        }
        return ModuleAction(ReadCommand(module, key));
    }

    bool SendsWindowManagerCommands(const ModuleActions& actions) {
        return std::ranges::any_of(std::array{&actions.click, &actions.rightClick, &actions.middleClick, &actions.scrollUp, &actions.scrollDown},
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
        if (const auto* network = std::get_if<NetworkConfig>(&settings)) {
            return &network->actions;
        }
        if (const auto* battery = std::get_if<BatteryConfig>(&settings)) {
            return &battery->actions;
        }
        if (const auto* custom = std::get_if<CustomConfig>(&settings)) {
            return &custom->text.actions;
        }
        if (const auto* counter = std::get_if<CounterConfig>(&settings)) {
            return &counter->text.actions;
        }
        return nullptr;
    }

    ModuleActions ReadActions(const TableReader& module) {
        return ModuleActions{.click       = ReadAction(module, "on_click"),
                             .rightClick  = ReadAction(module, "on_right_click"),
                             .middleClick = ReadAction(module, "on_middle_click"),
                             .scrollUp    = ReadAction(module, "on_scroll_up"),
                             .scrollDown  = ReadAction(module, "on_scroll_down")};
    }

    // A module's own keys plus the optional tooltip and input actions every module with settings takes.
    std::vector<std::string_view> WithOptionalKeys(std::initializer_list<std::string_view> keys) {
        std::vector<std::string_view> known(keys);
        known.insert(known.end(), {"tooltip", "on_click", "on_right_click", "on_middle_click", "on_scroll_up", "on_scroll_down"});
        return known;
    }

    std::optional<std::string> ReadTooltip(const TableReader& module) {
        return module.Has("tooltip") ? std::optional<std::string>(module.String("tooltip")) : std::nullopt;
    }

    TextModuleConfig ReadTextFields(const TableReader& module) {
        return TextModuleConfig{.format   = module.String("format"),
                                .tooltip  = ReadTooltip(module),
                                .icon     = ToWide(module.String("icon")),
                                .interval = Seconds(module, "interval"),
                                .actions  = ReadActions(module)};
    }

    TextModuleConfig ReadTextModuleConfig(const TableReader& module) {
        module.RejectUnknownKeys(WithOptionalKeys({"type", "format", "icon", "interval"}));
        return ReadTextFields(module);
    }

    // Each a CSS class name that style.css writes as .name, none of intarsia's own, none twice.
    std::vector<std::string> ReadCustomClasses(const TableReader& module, std::string_view key) {
        std::vector<std::string> classes = module.StringList(key);
        for (size_t index = 0; index < classes.size(); ++index) {
            const std::string& name = classes[index];
            const bool ident = !name.empty() && (std::isalpha(static_cast<unsigned char>(name.front())) != 0 || name.front() == '_') && std::ranges::all_of(name, IsIdentCharacter);
            if (!ident) {
                throw module.Error(
                    std::format("{} lists '{}', which is not a class name: use letters, digits, '_' or '-', starting with a letter or '_'", module.KeyPath(key), name));
            }
            if (IsStyledClass(name)) {
                throw module.Error(std::format("{} lists '{}', a class intarsia styles itself", module.KeyPath(key), name));
            }
            if (std::ranges::find(classes.begin(), classes.begin() + static_cast<std::ptrdiff_t>(index), name) != classes.begin() + static_cast<std::ptrdiff_t>(index)) {
                throw module.Error(std::format("{} lists '{}' twice", module.KeyPath(key), name));
            }
        }
        return classes;
    }

    std::variant<IntervalRun, ContinuousRun> ReadCustomRun(const TableReader& module) {
        switch (ParseKeyword(module, "run", kCustomRuns)) {
            case CustomRun::Interval: return IntervalRun{.timeout = Seconds(module, "timeout")};
            case CustomRun::Continuous:
                if (module.Has("timeout")) {
                    throw module.Error(std::format("{} applies only to run = \"interval\": a continuous command runs until it exits", module.KeyPath("timeout")));
                }
                return ContinuousRun{};
        }
        throw std::invalid_argument(std::format("unknown custom run in {}", module.KeyPath("run")));
    }

    CustomConfig ReadCustomConfig(const TableReader& module) {
        module.RejectUnknownKeys(WithOptionalKeys({"type", "command", "run", "timeout", "output", "classes", "format", "icon", "interval"}));
        return CustomConfig{.command = ReadCommand(module, "command"),
                            .run     = ReadCustomRun(module),
                            .output  = ParseKeyword(module, "output", kCustomOutputs),
                            .classes = ReadCustomClasses(module, "classes"),
                            .text    = ReadTextFields(module)};
    }

    // Added to a query of its own here, so a path this system has no counter for stops the load rather than the bar.
    // Only local counters: another machine's would have the bar open a network connection.
    std::wstring ReadCounterPath(const TableReader& module, std::string_view key) {
        const std::string path = module.String(key);
        if (!path.starts_with('\\') || path.starts_with("\\\\")) {
            throw module.Error(
                std::format(R"({} must be a local counter path starting with one backslash, as "\\Processor(_Total)\\% Processor Time", got '{}')", module.KeyPath(key), path));
        }
        std::wstring wide = ToWide(path);
        try {
            const PerformanceCounter counter(wide);
        } catch (const PdhError& error) { throw module.Error(std::format("{} names no counter this system has: {}", module.KeyPath(key), error.what())); }
        return wide;
    }

    CounterConfig ReadCounterConfig(const TableReader& module) {
        module.RejectUnknownKeys(WithOptionalKeys({"type", "counter", "combine", "format", "icon", "interval"}));
        return CounterConfig{.path = ReadCounterPath(module, "counter"), .combine = ParseKeyword(module, "combine", kCounterCombines), .text = ReadTextFields(module)};
    }

    AudioConfig ReadAudioConfig(const TableReader& module) {
        module.RejectUnknownKeys(WithOptionalKeys({"type", "flow", "format", "format_muted", "icon", "icon_muted", "step"}));
        const std::int64_t step = PositiveWholeNumber(module, "step");
        if (step > 100) {
            throw module.Error(std::format("{} is a percent of full volume, from 1 to 100, got {}", module.KeyPath("step"), step));
        }
        return AudioConfig{.flow        = ParseKeyword(module, "flow", kAudioFlows),
                           .format      = module.String("format"),
                           .formatMuted = module.String("format_muted"),
                           .tooltip     = ReadTooltip(module),
                           .icon        = ToWide(module.String("icon")),
                           .iconMuted   = ToWide(module.String("icon_muted")),
                           .stepPercent = static_cast<std::int32_t>(step),
                           .actions     = ReadActions(module)};
    }

    // A list of icons for a level from 0 to 100, lowest first. order says what the first one stands for.
    std::vector<std::wstring> ReadLevelIcons(const TableReader& module, std::string_view key, std::string_view order) {
        std::vector<std::wstring> icons;
        for (const std::string& icon : module.StringList(key)) {
            icons.push_back(ToWide(icon));
        }
        if (icons.empty()) {
            throw module.Error(std::format("{} must hold at least one icon, {} first", module.KeyPath(key), order));
        }
        return icons;
    }

    NetworkConfig ReadNetworkConfig(const TableReader& module) {
        module.RejectUnknownKeys(WithOptionalKeys({"type", "format_wifi", "format_ethernet", "format_disconnected", "icons_wifi", "icon_ethernet", "icon_disconnected"}));
        return NetworkConfig{.formatWifi         = module.String("format_wifi"),
                             .formatEthernet     = module.String("format_ethernet"),
                             .formatDisconnected = module.String("format_disconnected"),
                             .tooltip            = ReadTooltip(module),
                             .iconsWifi          = ReadLevelIcons(module, "icons_wifi", "weakest signal"),
                             .iconEthernet       = ToWide(module.String("icon_ethernet")),
                             .iconDisconnected   = ToWide(module.String("icon_disconnected")),
                             .actions            = ReadActions(module)};
    }

    std::int64_t ChargePercent(const TableReader& module, std::string_view key) {
        const std::int64_t percent = PositiveWholeNumber(module, key);
        if (percent > 100) {
            throw module.Error(std::format("{} is a percent of full charge, from 1 to 100, got {}", module.KeyPath(key), percent));
        }
        return percent;
    }

    BatteryConfig ReadBatteryConfig(const TableReader& module) {
        module.RejectUnknownKeys(
            WithOptionalKeys({"type", "format_discharging", "format_charging", "format_plugged", "icons", "icon_charging", "icon_plugged", "warning_at", "critical_at"}));
        const std::int64_t warningAt  = ChargePercent(module, "warning_at");
        const std::int64_t criticalAt = ChargePercent(module, "critical_at");
        if (criticalAt >= warningAt) {
            throw module.Error(std::format("{} ({}) must be under {} ({}), since a battery runs low before it is nearly empty", module.KeyPath("critical_at"), criticalAt,
                                           module.KeyPath("warning_at"), warningAt));
        }
        return BatteryConfig{.formatDischarging = module.String("format_discharging"),
                             .formatCharging    = module.String("format_charging"),
                             .formatPlugged     = module.String("format_plugged"),
                             .tooltip           = ReadTooltip(module),
                             .icons             = ReadLevelIcons(module, "icons", "emptiest"),
                             .iconCharging      = ToWide(module.String("icon_charging")),
                             .iconPlugged       = ToWide(module.String("icon_plugged")),
                             .warningAtPercent  = static_cast<std::int32_t>(warningAt),
                             .criticalAtPercent = static_cast<std::int32_t>(criticalAt),
                             .actions           = ReadActions(module)};
    }

    WindowConfig ReadWindowConfig(const TableReader& window) {
        window.RejectUnknownKeys(WithOptionalKeys({"type", "max_length"}));
        return WindowConfig{.maxLength = static_cast<size_t>(PositiveWholeNumber(window, "max_length")), .tooltip = ReadTooltip(window), .actions = ReadActions(window)};
    }

    // what names the file in the error, as "config" or "style". Every string the file holds is then valid UTF-8, so
    // converting one later cannot fail.
    std::string ReadFile(const std::filesystem::path& path, std::string_view what) {
        const std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw ConfigError(std::format("cannot open {} file {}", what, ToUtf8(path.wstring())));
        }
        std::ostringstream contents;
        contents << file.rdbuf();
        std::string text = contents.str();
        if (!IsValidUtf8(text)) {
            throw ConfigError(std::format("{} file {} is not valid UTF-8", what, ToUtf8(path.wstring())));
        }
        return text;
    }

    // Far more than any real config needs (a few thousand instructions), and still under a second to reach.
    constexpr std::int32_t kMaxConfigInstructions = 50'000'000;
    // Far more than any real config allocates (a few hundred KiB with the libraries), and a string built in one C call
    // runs no instructions for the count hook to see.
    constexpr size_t kMaxConfigBytes = size_t{16} << 20;

    // What the Lua state may use, and whether it was stopped, shared with the allocator and the hook through the
    // allocator's user data.
    struct LuaLimits {
        lua_Alloc allocate; // Lua's own allocator, which the limited one bounds
        void*     allocateData;
        size_t    usedBytes;
        size_t    limitBytes;
        bool      runaway;
    };

    // Lua's own allocator with a ceiling: a refused request raises a memory error inside the lua_pcall.
    void* LimitedAlloc(void* userData, void* block, size_t oldSize, size_t newSize) {
        auto* limits = static_cast<LuaLimits*>(userData);
        // oldSize is the block's size only for a live block, else a tag of what is being allocated.
        const size_t held = block == nullptr ? 0 : oldSize;
        if (newSize > held && newSize - held > limits->limitBytes - limits->usedBytes) {
            return nullptr;
        }
        void* resized = limits->allocate(limits->allocateData, block, oldSize, newSize);
        if (newSize != 0 && resized == nullptr) {
            return nullptr;
        }
        limits->usedBytes = limits->usedBytes - held + newSize;
        return resized;
    }

    // Puts the limited allocator over the state's own, counting from what the state holds already so that freeing it
    // later balances.
    void LimitAllocations(lua_State* lua, LuaLimits& limits) {
        limits.allocate  = lua_getallocf(lua, &limits.allocateData);
        limits.usedBytes = (static_cast<size_t>(lua_gc(lua, LUA_GCCOUNT)) << 10) + static_cast<size_t>(lua_gc(lua, LUA_GCCOUNTB));
        lua_setallocf(lua, LimitedAlloc, &limits);
    }

    LuaLimits& LimitsOf(lua_State* lua) {
        void* userData = nullptr;
        lua_getallocf(lua, &userData);
        return *static_cast<LuaLimits*>(userData);
    }

    // Called once the count is reached. The error raised is catchable, so from here on every instruction raises again
    // until it reaches LoadConfig's own call, though no protected call is left in the sandbox to catch it.
    void AbortRunawayConfig(lua_State* lua, lua_Debug*) {
        LimitsOf(lua).runaway = true;
        lua_sethook(lua, AbortRunawayConfig, LUA_MASKCOUNT, 1);
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

    // The ConfigError for a load or call that did not return LUA_OK.
    ConfigError LuaError(lua_State* lua, std::int32_t status, std::string_view source) {
        if (LimitsOf(lua).runaway) {
            return ConfigError(std::format("{}: the config ran {} instructions without finishing, is there an endless loop?", source, kMaxConfigInstructions));
        }
        if (status == LUA_ERRMEM) {
            return ConfigError(std::format("{}: the config used more than {} MiB of memory", source, kMaxConfigBytes >> 20));
        }
        return ConfigError(PopErrorMessage(lua));
    }

} // namespace

std::filesystem::path DefaultConfigPath() {
    return KnownFolderPath(FOLDERID_Profile, "FOLDERID_Profile") / L".config" / L"intarsia" / L"config.lua";
}

std::filesystem::path StylePath(const std::filesystem::path& configPath) {
    return configPath.parent_path() / L"style.css";
}

Config LoadConfig(const std::filesystem::path& path) {
    const std::string source = ToUtf8(path.wstring());
    const std::string chunk  = ReadFile(path, "config");

    // Declared before the state, whose close frees through the limited allocator.
    LuaLimits      limits{.allocate = nullptr, .allocateData = nullptr, .usedBytes = 0, .limitBytes = kMaxConfigBytes, .runaway = false};
    const LuaState lua(luaL_newstate());
    if (lua == nullptr) {
        throw ConfigError("luaL_newstate failed to allocate a Lua state");
    }
    LimitAllocations(lua.get(), limits);
    // Only libraries that cannot reach the filesystem or other processes: base minus its loaders. load goes too, since it
    // accepts precompiled chunks and Lua does not verify bytecode, pcall since a config is data with nothing to recover
    // from, and setmetatable since Lua runs a __gc metamethod with hooks off, where an endless loop could not be stopped.
    OpenLibrary(lua.get(), LUA_GNAME, luaopen_base);
    OpenLibrary(lua.get(), LUA_STRLIBNAME, luaopen_string);
    OpenLibrary(lua.get(), LUA_TABLIBNAME, luaopen_table);
    OpenLibrary(lua.get(), LUA_MATHLIBNAME, luaopen_math);
    for (const char* removed : {"dofile", "loadfile", "load", "pcall", "xpcall", "setmetatable"}) {
        lua_pushnil(lua.get());
        lua_setglobal(lua.get(), removed);
    }
    // Pattern matching backtracks in C, out of the count hook's reach, and a short pattern can take exponential time.
    // The string metatable's __index is this same table, so the method form goes too.
    lua_getglobal(lua.get(), LUA_STRLIBNAME);
    for (const char* removed : {"find", "match", "gmatch", "gsub"}) {
        lua_pushnil(lua.get());
        lua_setfield(lua.get(), -2, removed);
    }
    lua_pop(lua.get(), 1);
    // Reload runs on the UI thread, so a config that never finishes would freeze the bar: stop it instead.
    lua_sethook(lua.get(), AbortRunawayConfig, LUA_MASKCOUNT, kMaxConfigInstructions);

    const std::string  chunkName = "@" + source;
    const std::int32_t loaded    = luaL_loadbufferx(lua.get(), chunk.data(), chunk.size(), chunkName.c_str(), "t");
    if (loaded != LUA_OK) {
        throw LuaError(lua.get(), loaded, source);
    }
    const std::int32_t ran = lua_pcall(lua.get(), 0, 1, 0);
    // What follows pushes strings outside any protected call, where a refused allocation reaches Lua's panic handler and
    // aborts the bar. Only the reader allocates from here on, so the config's ceiling has done its job.
    limits.limitBytes = std::numeric_limits<size_t>::max();
    if (ran != LUA_OK) {
        throw LuaError(lua.get(), ran, source);
    }
    if (lua_type(lua.get(), -1) != LUA_TTABLE) {
        throw ConfigError(std::format("{}: must return a table, got {}", source, luaL_typename(lua.get(), -1)));
    }

    const TableReader root(lua.get(), lua_gettop(lua.get()), source, "");
    root.RejectUnknownKeys({"bar", "window_manager", "modules_left", "modules_center", "modules_right", "layouts", "modules"});

    const std::filesystem::path stylePath   = StylePath(path);
    const StyleSheet            sheet       = ParseStyleSheet(ReadFile(stylePath, "style"), ToUtf8(stylePath.wstring()));
    StyleConfig                 style       = ResolveBarStyle(sheet);
    const TableReader           definitions = root.Table("modules");
    Sections<ModuleInstance>    modules     = ReadSections(root, definitions, sheet, style);
    std::vector<LayoutConfig>   layouts     = ReadLayouts(root, definitions, sheet, style);

    const TableReader           bar = root.Table("bar");
    bar.RejectUnknownKeys({"edge", "layer", "space", "thickness"});
    const BarConfig barConfig{
        .edge = ParseKeyword(bar, "edge", kEdges), .layer = ParseKeyword(bar, "layer", kLayers), .space = ParseKeyword(bar, "space", kSpaces), .thicknessDips = BarThickness(bar)};

    Config config{.bar           = barConfig,
                  .style         = std::move(style),
                  .windowManager = ParseKeyword(root, "window_manager", kWindowManagers),
                  .modules       = std::move(modules),
                  .layouts       = std::move(layouts)};
    RejectUnshownModules(definitions, config);
    RequireMembersInOneGroup(definitions, config);
    RequireKnownIds(sheet, definitions.Keys());
    std::vector<DeclaredClasses> declared;
    ForEachModuleInConfig(config, [&declared](const ModuleInstance& instance) {
        if (const auto* custom = std::get_if<CustomConfig>(&instance.settings)) {
            declared.push_back(DeclaredClasses{.module = instance.name, .classes = custom->classes});
        }
    });
    RequireDeclaredClasses(sheet, declared);

    for (const auto [kind, name] : {std::pair{ModuleKind::Workspaces, "workspaces"}, std::pair{ModuleKind::Windows, "windows"}, std::pair{ModuleKind::BindingMode, "binding_mode"},
                                    std::pair{ModuleKind::TilingDirection, "tiling_direction"}}) {
        if (config.windowManager == WindowManagerKind::Windows && AnyModuleInConfig(config, [kind](const ModuleInstance& instance) { return instance.kind == kind; })) {
            throw root.Error(std::format("{} needs a tiling window manager, window_manager 'windows' is none", name));
        }
    }
    const bool sendsCommands = AnyModuleInConfig(config, [](const ModuleInstance& instance) {
        const ModuleActions* actions = ActionsOf(instance.settings);
        return actions != nullptr && SendsWindowManagerCommands(*actions);
    });
    if (config.windowManager == WindowManagerKind::Windows && sendsCommands) {
        throw root.Error("a window_manager action needs a window manager that takes commands, window_manager 'windows' takes none");
    }

    return config;
}

const Sections<ModuleInstance>& ModulesForEdgeLength(const Config& config, float edgeLengthDips) {
    const Sections<ModuleInstance>* chosen = &config.modules;
    for (const LayoutConfig& layout : config.layouts) {
        if (layout.minLengthDips <= edgeLengthDips) {
            chosen = &layout.modules;
        }
    }
    return *chosen;
}

bool ShowsKind(const Config& config, ModuleKind kind) {
    return AnyModuleInConfig(config, [kind](const ModuleInstance& instance) { return instance.kind == kind; });
}
