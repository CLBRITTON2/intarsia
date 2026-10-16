#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/IconText.hpp"
#include "modules/Placeholders.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The icon for a level from 0 to 100 (a signal, a charge): icons split the range evenly, lowest first. A level outside
// the range takes the nearest end.
const std::wstring& LevelIcon(const std::vector<std::wstring>& icons, std::int32_t percent);

// Which of a module's colors a line draws in.
enum class TextState : std::uint8_t {
    Normal,
    Alert,    // alert text, for a value that needs attention such as a failed command
    Warning,  // warning text and icon, a battery running low
    Critical, // critical text and icon, a battery nearly empty
    Muted,    // muted text and icon, a muted audio output
    Classed   // a custom module's declared class, by ModuleText::classIndex
};

// What a module shows as one line: its icon, the text after it, either of them empty.
struct ModuleText {
    std::wstring icon;
    std::wstring text;
    TextState    state;
    size_t       classIndex; // Classed: which of the module's declared classes, in their order, else 0

    bool         operator==(const ModuleText&) const = default;
};

ModuleText NormalText(std::wstring icon, std::wstring text);

// A ModuleText, laid out again only when it changes, drawn in its state's colors.
class TextLine {
  public:
    // Whether content differs from the last update's.
    bool        Update(const RenderContext& render, ModuleText content);
    D2D1_SIZE_F Size() const;
    void        Draw(const RenderContext& render, D2D1_POINT_2F origin) const;

  private:
    ModuleText _shown = NormalText(L"", L""); // what _text holds
    IconText   _text  = EmptyIconText();
};

// A module that shows one ModuleText, laid out again only when it changes, and runs the actions its config binds.
// Config is the module's config struct, which carries those actions. A subclass supplies only the content.
template <typename Config>
class IconTextModule : public IModule {
  public:
    bool Layout(const RenderContext& render) final {
        return _line.Update(render, Content());
    }

    D2D1_SIZE_F Size() const final {
        return _line.Size();
    }

    void Draw(const RenderContext& render, D2D1_POINT_2F origin) const final {
        _line.Draw(render, origin);
    }

    const ModuleActions& Actions() const final {
        return _config.actions;
    }

  protected:
    explicit IconTextModule(Config config) : _config(std::move(config)) {}

    const Config& Settings() const {
        return _config;
    }

    // What the module shows now.
    virtual ModuleText Content() = 0;

  private:
    Config   _config;
    TextLine _line;
};

// Time left until the next whole multiple of interval since the epoch, so a 60 s clock ticks on the minute and 1 s
// gauges tick on the second together.
std::chrono::milliseconds TimeUntilNextTick(std::chrono::system_clock::time_point now, std::chrono::seconds interval);

// Whether a sample taken at sampledAt is due again at now, for a module that samples at most once per interval: the bar
// also lays it out for other modules' changes and every animation frame. The 50 ms slack is for the redraw timer firing
// a little early.
bool SampleDue(std::chrono::steady_clock::time_point sampledAt, std::chrono::steady_clock::time_point now, std::chrono::seconds interval);

// The text each spec (the format, the tooltip) gave for the current sample, so a layout between samples formats
// nothing.
class SampledTexts {
  public:
    explicit SampledTexts(std::chrono::steady_clock::time_point sampledAt);
    // Calls sample and drops every text when a sample is due at now (SampleDue).
    void SampleIfDue(std::chrono::steady_clock::time_point now, std::chrono::seconds interval, const std::function<void()>& sample);
    // spec's text, from format the first time since the last sample.
    const std::wstring& Of(const std::string& spec, const std::function<std::wstring()>& format);

  private:
    std::chrono::steady_clock::time_point             _sampledAt;
    std::vector<std::pair<std::string, std::wstring>> _texts;
};

// A module that shows its icon and one formatted value, redrawn on each interval tick: clock, cpu, memory, custom. A
// subclass supplies only the value, formatted with whichever spec it is given: format on the bar, tooltip on hover.
class TextModule : public IconTextModule<TextModuleConfig> {
  public:
    std::optional<std::uint32_t> MillisecondsUntilChange() const override;
    std::optional<std::wstring>  TooltipText() override;

  protected:
    // Throws ConfigError when the format or tooltip names a value samples lacks or has a spec that does not fit it.
    // type names the module, such as "cpu".
    TextModule(TextModuleConfig config, std::string_view type, const std::vector<Placeholder>& samples);

    // The module's icon and its current value run through spec.
    virtual ModuleText Format(const std::string& spec) = 0;

  private:
    ModuleText Content() final;
};
