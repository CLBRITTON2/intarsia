#include "config/Config.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace {

    constexpr std::string_view kValidConfig = R"lua(
return {
    bar = { edge = "top", layer = "top", thickness = 54, border = 1, spacing = 8 },
    font = { family = "Some Font", text_size = 15, icon_size = 18 },
    colors = { background = "#191724e6", border = "#6e6a8640", foreground = "#e0def4", accent = "#c4a7e7", alert = "#eb6f92" },
    window_manager = "windows",
    modules_left = { "title" },
    modules_center = { "time" },
    modules_right = {},
    modules = {
        time = { type = "clock", format = "{:%H:%M}", icon = "x", interval = 60 },
        title = { type = "window", max_length = 80 },
    },
}
)lua";

    // A config file that deletes itself, so each case runs LoadConfig on a real file.
    class TempConfig {
      public:
        explicit TempConfig(std::string_view contents) :
            _path(std::filesystem::temp_directory_path() / ("intarsia-test-" + std::to_string(std::hash<std::string_view>{}(contents)) + ".lua")) {
            std::ofstream file(_path, std::ios::binary);
            file << contents;
        }
        ~TempConfig() {
            std::error_code error;
            if (!std::filesystem::remove(_path, error) || error) {
                std::fprintf(stderr, "could not delete temp config %ls: %s\n", _path.c_str(), error.message().c_str());
            }
        }
        TempConfig(const TempConfig&)                             = delete;
        TempConfig&                  operator=(const TempConfig&) = delete;

        const std::filesystem::path& Path() const {
            return _path;
        }

      private:
        std::filesystem::path _path;
    };

    // The config text with one snippet swapped. Throws when the snippet is not there, so a stale case cannot pass.
    std::string Replace(std::string_view config, std::string_view from, std::string_view to) {
        std::string  text(config);
        const size_t position = text.find(from);
        if (position == std::string::npos) {
            throw std::invalid_argument("snippet not in the config: " + std::string(from));
        }
        return text.replace(position, from.size(), to);
    }

    // kValidConfig with extra modules defined, and shown on the right.
    std::string WithRightModules(std::string_view names, std::string_view definitions) {
        return Replace(Replace(kValidConfig, "modules_right = {},", std::string("modules_right = ") + std::string(names) + ","), "    modules = {\n",
                       "    modules = {\n" + std::string(definitions) + "\n");
    }

    Config LoadText(std::string_view contents) {
        const TempConfig config(contents);
        return LoadConfig(config.Path());
    }

    auto ConfigErrorWith(std::string_view text) {
        return testing::ThrowsMessage<ConfigError>(testing::HasSubstr(std::string(text)));
    }

    std::vector<ModuleKind> Kinds(const std::vector<ModuleInstance>& instances) {
        std::vector<ModuleKind> kinds;
        kinds.reserve(instances.size());
        for (const ModuleInstance& instance : instances) {
            kinds.push_back(instance.kind);
        }
        return kinds;
    }

    // Throws std::bad_variant_access, failing the test, when the instance holds other settings.
    template <typename Settings>
    const Settings& SettingsOf(const std::vector<ModuleInstance>& instances, size_t index) {
        return std::get<Settings>(instances.at(index).settings);
    }

} // namespace

TEST(Config, ShippedExampleLoads) {
    const Config config = LoadConfig(INTARSIA_EXAMPLE_CONFIG);
    EXPECT_EQ(config.bar.edge, Edge::Top);
    EXPECT_EQ(Kinds(config.modules.center), std::vector<ModuleKind>{ModuleKind::Clock});
}

TEST(Config, ValidConfigParsesIntoTypedValues) {
    const Config config = LoadText(kValidConfig);
    EXPECT_FLOAT_EQ(config.bar.thicknessDips, 54.0f);
    EXPECT_FLOAT_EQ(config.bar.spacingDips, 8.0f);
    EXPECT_EQ(config.font.family, L"Some Font");
    EXPECT_FLOAT_EQ(config.colors.background.a, 0xe6 / 255.0f);
    EXPECT_FLOAT_EQ(config.colors.foreground.a, 1.0f);
    EXPECT_EQ(config.modules.center.at(0).name, "time");
    EXPECT_EQ(SettingsOf<TextModuleConfig>(config.modules.center, 0).interval, std::chrono::seconds(60));
}

TEST(Config, MissingFileIsAnError) {
    EXPECT_THAT([] { return LoadConfig(std::filesystem::temp_directory_path() / "intarsia-test-missing.lua"); }, ConfigErrorWith("cannot open config file"));
}

TEST(Config, LuaSyntaxAndRuntimeErrorsAreReported) {
    EXPECT_THAT([] { return LoadText("return {"); }, ConfigErrorWith("expected"));
    EXPECT_THAT([] { return LoadText("error('boom')"); }, ConfigErrorWith("boom"));
}

TEST(Config, ChunkMustReturnATable) {
    EXPECT_THAT([] { return LoadText("return 42"); }, ConfigErrorWith("must return a table, got number"));
}

TEST(Config, MissingKeyNamesItsPath) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(edge = "top", )", "")); }, ConfigErrorWith("bar.edge is missing"));
}

TEST(Config, WrongTypeNamesPathAndBothTypes) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "thickness = 54", R"(thickness = "54")")); }, ConfigErrorWith("bar.thickness must be a number, got string"));
}

TEST(Config, UnknownKeysAreRejectedAtRootAndInsideTables) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "modules_right = {},", "modules_right = {}, extra = 1,")); }, ConfigErrorWith("extra is not a known key"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "spacing = 8", "spacing = 8, padding = 2")); }, ConfigErrorWith("bar.padding is not a known key"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "max_length = 80", "max_length = 80, width = 3")); }, ConfigErrorWith("modules.title.width is not a known key"));
}

TEST(Config, EdgeMustBeOneOfFour) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(edge = "top")", R"(edge = "side")")); }, ConfigErrorWith("got 'side'"));
}

TEST(Config, ColorsMustBeHex) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"("#e0def4")", R"("e0def4")")); }, ConfigErrorWith("colors.foreground must be '#RRGGBB'"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"("#e0def4")", R"("#e0dzf4")")); }, ConfigErrorWith("invalid hex digits 'dz'"));
}

TEST(Config, SizesMustBePositiveAndIntervalWholeSeconds) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "thickness = 54", "thickness = 0")); }, ConfigErrorWith("bar.thickness must be greater than 0"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "border = 1", "border = -1")); }, ConfigErrorWith("bar.border must be 0 or more"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60", "interval = 1.5")); }, ConfigErrorWith("modules.time.interval must be a whole number"));
}

TEST(Config, ModuleListsMustBePlainListsOfStrings) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"({ "time" })", R"({ "time", x = 1 })")); }, ConfigErrorWith("modules_center must be a plain list"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"({ "time" })", "{ 7 }")); }, ConfigErrorWith("modules_center[1] must be a string"));
}

TEST(Config, ListsMayOnlyShowDefinedModules) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"({ "time" })", R"({ "time", "clock" })")); },
                ConfigErrorWith("modules_center shows 'clock', which is not defined in modules"));
}

TEST(Config, EveryDefinedModuleMustBeShown) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"({ "time" })", "{}")); }, ConfigErrorWith("modules.time is defined but no _modules list shows it"));
}

TEST(Config, ModuleNamesUseASafeAlphabet) {
    EXPECT_THAT([] { return LoadText(WithRightModules(R"({ "two words" })", R"(        ["two words"] = { type = "cpu", format = "{}", icon = "", interval = 1 },)")); },
                ConfigErrorWith("is not a valid module name"));
}

TEST(Config, TypeMustBeAKnownModuleType) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(type = "clock")", R"(type = "teapot")")); },
                ConfigErrorWith("modules.time.type is 'teapot', known types are: clock, window, workspaces, windows, binding_mode, tiling_direction, cpu, memory"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(type = "clock", )", "")); }, ConfigErrorWith("modules.time.type is missing"));
}

TEST(Config, OneTypeCanBeShownUnderSeveralNames) {
    const Config config = LoadText(WithRightModules(R"({ "cpu", "cpu_slow" })", R"(        cpu = { type = "cpu", format = "{}%", icon = "c", interval = 1 },
        cpu_slow = { type = "cpu", format = "{}", icon = "", interval = 5 },)"));
    EXPECT_EQ(Kinds(config.modules.right), (std::vector<ModuleKind>{ModuleKind::Cpu, ModuleKind::Cpu}));
    EXPECT_EQ(config.modules.right.at(1).name, "cpu_slow");
    EXPECT_EQ(SettingsOf<TextModuleConfig>(config.modules.right, 0).format, "{}%");
    EXPECT_EQ(SettingsOf<TextModuleConfig>(config.modules.right, 1).interval, std::chrono::seconds(5));
}

TEST(Config, WindowManagerAndWindowModuleParse) {
    const Config config = LoadText(kValidConfig);
    EXPECT_EQ(config.windowManager, WindowManagerKind::Windows);
    EXPECT_EQ(Kinds(config.modules.left), std::vector<ModuleKind>{ModuleKind::Window});
    EXPECT_EQ(SettingsOf<WindowConfig>(config.modules.left, 0).maxLength, 80u);
}

TEST(Config, WindowManagerMustBeAKnownBackend) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(window_manager = "windows")", R"(window_manager = "i3")")); },
                ConfigErrorWith("window_manager must be one of windows, glazewm, got 'i3'"));
}

TEST(Config, WorkspacesNeedAWindowManagerWithWorkspaces) {
    const std::string onWindows = WithRightModules(R"({ "desktops" })", R"(        desktops = { type = "workspaces" },)");
    EXPECT_THAT([&onWindows] { return LoadText(onWindows); }, ConfigErrorWith("window_manager 'windows' is none"));

    const std::string onGlazeWm = Replace(onWindows, R"(window_manager = "windows")", R"(window_manager = "glazewm")");
    const Config      config    = LoadText(onGlazeWm);
    EXPECT_EQ(config.windowManager, WindowManagerKind::GlazeWm);
    EXPECT_EQ(Kinds(config.modules.right), std::vector<ModuleKind>{ModuleKind::Workspaces});
    EXPECT_THAT([&onGlazeWm] { return LoadText(Replace(onGlazeWm, R"(type = "workspaces")", R"(type = "workspaces", size = 2)")); },
                ConfigErrorWith("modules.desktops.size is not a known key"));
}

TEST(Config, WindowsNeedAWindowManagerWithWorkspaces) {
    const std::string onWindows = WithRightModules(R"({ "apps" })", R"(        apps = { type = "windows" },)");
    EXPECT_THAT([&onWindows] { return LoadText(onWindows); }, ConfigErrorWith("windows needs a tiling window manager"));

    const std::string onGlazeWm = Replace(onWindows, R"(window_manager = "windows")", R"(window_manager = "glazewm")");
    EXPECT_EQ(Kinds(LoadText(onGlazeWm).modules.right), std::vector<ModuleKind>{ModuleKind::Windows});
    EXPECT_THAT([&onGlazeWm] { return LoadText(Replace(onGlazeWm, R"(type = "windows")", R"(type = "windows", icon_size = 2)")); },
                ConfigErrorWith("modules.apps.icon_size is not a known key"));
}

TEST(Config, BindingModeAndTilingDirectionParseOnATilingWindowManager) {
    const std::string onWindows = WithRightModules(R"({ "mode", "tiling" })", R"(        mode = { type = "binding_mode" },
        tiling = { type = "tiling_direction", horizontal = "H", vertical = "V" },)");
    EXPECT_THAT([&onWindows] { return LoadText(onWindows); }, ConfigErrorWith("binding_mode needs a tiling window manager"));

    const std::string onGlazeWm = Replace(onWindows, R"(window_manager = "windows")", R"(window_manager = "glazewm")");
    const Config      config    = LoadText(onGlazeWm);
    EXPECT_EQ(Kinds(config.modules.right), (std::vector<ModuleKind>{ModuleKind::BindingMode, ModuleKind::TilingDirection}));
    EXPECT_EQ(SettingsOf<TilingDirectionConfig>(config.modules.right, 1).vertical, L"V");
    EXPECT_THAT([&onGlazeWm] { return LoadText(Replace(onGlazeWm, R"(horizontal = "H", )", "")); }, ConfigErrorWith("modules.tiling.horizontal is missing"));
}

TEST(Config, WindowModuleNeedsAWholeMaxLength) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "max_length = 80", "max_length = 2.5")); }, ConfigErrorWith("modules.title.max_length must be a whole number"));
}

TEST(Config, CpuAndMemoryShareTheTextModuleShape) {
    const Config config = LoadText(WithRightModules(R"({ "cpu", "memory" })", R"(        cpu = { type = "cpu", format = "{}%", icon = "c", interval = 1 },
        memory = { type = "memory", format = "{:.1f} GB", icon = "m", interval = 2 },)"));
    EXPECT_EQ(Kinds(config.modules.right), (std::vector<ModuleKind>{ModuleKind::Cpu, ModuleKind::Memory}));
    EXPECT_EQ(SettingsOf<TextModuleConfig>(config.modules.right, 0).format, "{}%");
    EXPECT_EQ(SettingsOf<TextModuleConfig>(config.modules.right, 1).interval, std::chrono::seconds(2));
}

TEST(Config, ActionsResolveToAbsoluteExecutables) {
    const Config         config  = LoadText(Replace(kValidConfig, "interval = 60 }", R"(interval = 60, on_click = { "cmd", "/c", "exit 0" }, on_scroll_up = { "cmd" } })"));
    const ModuleActions& actions = SettingsOf<TextModuleConfig>(config.modules.center, 0).actions;
    ASSERT_TRUE(actions.click.has_value());
    const Command& click = std::get<Command>(*actions.click);
    EXPECT_TRUE(click.program.is_absolute());
    EXPECT_EQ(click.arguments, (std::vector<std::wstring>{L"/c", L"exit 0"}));
    EXPECT_TRUE(actions.scrollUp.has_value());
    EXPECT_FALSE(actions.rightClick.has_value());
    EXPECT_FALSE(actions.scrollDown.has_value());
}

TEST(Config, WindowManagerActionsSendTheirTextAsWritten) {
    const std::string    glazeWm = Replace(kValidConfig, R"(window_manager = "windows")", R"(window_manager = "glazewm")");
    const Config         config  = LoadText(Replace(glazeWm, "interval = 60 }", R"(interval = 60, on_scroll_up = { window_manager = "focus --next-workspace" } })"));
    const ModuleActions& actions = SettingsOf<TextModuleConfig>(config.modules.center, 0).actions;
    ASSERT_TRUE(actions.scrollUp.has_value());
    EXPECT_EQ(std::get<WindowManagerCommand>(*actions.scrollUp).text, "focus --next-workspace");

    EXPECT_THAT([&glazeWm] { return LoadText(Replace(glazeWm, "interval = 60 }", R"(interval = 60, on_click = { window_manager = "focus\n--workspace 2" } })")); },
                ConfigErrorWith("modules.time.on_click.window_manager must be a command on one line"));
    EXPECT_THAT([&glazeWm] { return LoadText(Replace(glazeWm, "interval = 60 }", R"(interval = 60, on_click = { window_manager = "" } })")); },
                ConfigErrorWith("must be a command on one line"));
    EXPECT_THAT([&glazeWm] { return LoadText(Replace(glazeWm, "interval = 60 }", R"(interval = 60, on_click = { window_manager = "x", extra = 1 } })")); },
                ConfigErrorWith("modules.time.on_click.extra is not a known key"));
}

TEST(Config, WindowManagerActionsNeedAWindowManagerThatTakesCommands) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60 }", R"(interval = 60, on_click = { window_manager = "focus --next-workspace" } })")); },
                ConfigErrorWith("window_manager 'windows' takes none"));
}

TEST(Config, ActionsMustNameAnExistingExecutable) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60 }", "interval = 60, on_click = {} }")); }, ConfigErrorWith("modules.time.on_click must name a program"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60 }", R"(interval = 60, on_click = { "intarsia-no-such-program" } })")); },
                ConfigErrorWith("not an existing file or a program on PATH"));
}

TEST(Config, ActionsRefuseScriptsThatWouldRunThroughCmd) {
    const std::filesystem::path script = std::filesystem::temp_directory_path() / "intarsia-test-action.bat";
    std::ofstream(script) << "@echo off\n";
    std::string escaped = script.string();
    for (size_t position = escaped.find('\\'); position != std::string::npos; position = escaped.find('\\', position + 2)) {
        escaped.insert(position, "\\");
    }
    const std::string config = Replace(kValidConfig, "interval = 60 }", "interval = 60, on_click = { \"" + escaped + "\" } }");
    EXPECT_THAT([&config] { return LoadText(config); }, ConfigErrorWith("which is not an .exe"));
    std::filesystem::remove(script);
}

TEST(Config, CustomModulesParseTheirCommandAndTimeout) {
    const Config        config = LoadText(WithRightModules(
        R"({ "hello" })", R"(        hello = { type = "custom", command = { "cmd", "/c", "echo hi" }, timeout = 5, output = "json", format = "{}", icon = "", interval = 30 },)"));
    const CustomConfig& custom = SettingsOf<CustomConfig>(config.modules.right, 0);
    EXPECT_EQ(custom.command.program.filename(), L"cmd.exe");
    EXPECT_EQ(custom.command.arguments, (std::vector<std::wstring>{L"/c", L"echo hi"}));
    EXPECT_EQ(custom.timeout, std::chrono::seconds(5));
    EXPECT_EQ(custom.output, CustomOutput::Json);
    EXPECT_EQ(custom.text.interval, std::chrono::seconds(30));
}

TEST(Config, CustomModulesNeedACommand) {
    EXPECT_THAT(
        [] {
            return LoadText(WithRightModules(R"({ "hello" })", R"(        hello = { type = "custom", timeout = 5, output = "text", format = "{}", icon = "", interval = 30 },)"));
        },
        ConfigErrorWith("modules.hello.command is missing"));
}

TEST(Config, CustomOutputIsTextOrJson) {
    EXPECT_THAT(
        [] {
            return LoadText(WithRightModules(R"({ "hello" })",
                                             R"(        hello = { type = "custom", command = { "cmd" }, timeout = 5, output = "xml", format = "{}", icon = "", interval = 30 },)"));
        },
        ConfigErrorWith("modules.hello.output must be one of text, json, got 'xml'"));
}

TEST(Config, TheConfigCannotReadFiles) {
    EXPECT_THAT([] { return LoadText("dofile('C:/Windows/win.ini')"); }, ConfigErrorWith("attempt to call a nil value (global 'dofile')"));
    EXPECT_THAT([] { return LoadText("loadfile('C:/Windows/win.ini')"); }, ConfigErrorWith("attempt to call a nil value (global 'loadfile')"));
}

// load takes precompiled bytecode, which Lua does not verify.
TEST(Config, TheConfigCannotLoadChunks) {
    EXPECT_THAT([] { return LoadText("load('return 1')"); }, ConfigErrorWith("attempt to call a nil value (global 'load')"));
}

// Each module's table stays on the Lua stack while the config is read, far past the slots Lua guarantees.
TEST(Config, ManyModulesFitOnTheLuaStack) {
    std::string names = "{ ";
    std::string definitions;
    for (int index = 0; index < 200; ++index) {
        names += "\"clock" + std::to_string(index) + "\", ";
        definitions += "        clock" + std::to_string(index) + " = { type = \"clock\", format = \"{:%H:%M}\", icon = \"x\", interval = 60 },\n";
    }
    names += "}";
    EXPECT_EQ(LoadText(WithRightModules(names, definitions)).modules.right.size(), 200u);
}

TEST(Config, TooltipIsOptional) {
    EXPECT_FALSE(SettingsOf<TextModuleConfig>(LoadText(kValidConfig).modules.center, 0).tooltip.has_value());
    const Config config = LoadText(Replace(kValidConfig, R"(interval = 60 },)", R"(interval = 60, tooltip = "{:%A %d %B %Y}" },)"));
    EXPECT_EQ(SettingsOf<TextModuleConfig>(config.modules.center, 0).tooltip, "{:%A %d %B %Y}");
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(max_length = 80 })", R"(max_length = 80, tooltip = 7 })")); },
                ConfigErrorWith("modules.title.tooltip must be a string"));
}

TEST(Config, LayerIsTopOrBottom) {
    EXPECT_EQ(LoadText(kValidConfig).bar.layer, Layer::Top);
    EXPECT_EQ(LoadText(Replace(kValidConfig, R"(layer = "top")", R"(layer = "bottom")")).bar.layer, Layer::Bottom);
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(layer = "top")", R"(layer = "overlay")")); }, ConfigErrorWith("bar.layer must be one of top, bottom, got 'overlay'"));
}

TEST(Config, AnEndlessConfigIsStoppedWithAnError) {
    EXPECT_THAT([] { return LoadText("while true do end"); }, ConfigErrorWith("without finishing, is there an endless loop?"));
}

TEST(Config, ErroringIndexMetamethodIsNotRun) {
    const std::string config = Replace(kValidConfig, R"(bar = { edge = "top", layer = "top", thickness = 54, border = 1, spacing = 8 },)",
                                       R"(bar = setmetatable({}, { __index = function() error("metamethod ran") end }),)");
    EXPECT_THAT([&config] { return LoadText(config); }, ConfigErrorWith("bar.edge is missing"));
}
