#include "config/Style.hpp"

#include "platform/Text.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

    constexpr D2D1_COLOR_F     kTransparent{.r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 0.0f};
    constexpr std::string_view kSpace = " \r";

    ConfigError                StyleError(std::string_view source, std::uint32_t line, std::string_view message) {
        return ConfigError(std::format("{}:{}: {}", source, line, message));
    }

    std::string_view Trim(std::string_view text) {
        const size_t first = text.find_first_not_of(kSpace);
        if (first == std::string_view::npos) {
            return {};
        }
        return text.substr(first, text.find_last_not_of(kSpace) - first + 1);
    }

    // Splits on any of separators, trimming each part and dropping empty ones.
    std::vector<std::string_view> Split(std::string_view text, std::string_view separators) {
        std::vector<std::string_view> parts;
        size_t                        start = 0;
        while (start <= text.size()) {
            const size_t           end  = text.find_first_of(separators, start);
            const std::string_view part = Trim(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
            if (!part.empty()) {
                parts.push_back(part);
            }
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }
        return parts;
    }

    // The words from index on, rejoined by single spaces.
    std::string JoinFrom(const std::vector<std::string_view>& words, size_t index) {
        std::string joined;
        for (size_t word = index; word < words.size(); ++word) {
            joined += joined.empty() ? "" : " ";
            joined += words[word];
        }
        return joined;
    }

    // from_chars also reads inf and nan, which every range check below would let through.
    std::optional<float> ParseNumber(std::string_view text) {
        float value             = 0.0f;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc() || end != text.data() + text.size() || !std::isfinite(value)) {
            return std::nullopt;
        }
        return value;
    }

    // Splitting drops empty entries, so a stray comma shows only in the count.
    template <typename Value, typename ParseEntry>
    std::vector<Value> ParseCommaList(std::string_view text, std::string_view form, ParseEntry parseEntry) {
        if (text == "none") {
            return {};
        }
        std::vector<Value> values;
        for (const std::string_view entry : Split(text, ",")) {
            values.push_back(parseEntry(entry));
        }
        if (values.empty() || static_cast<size_t>(std::ranges::count(text, ',')) != values.size() - 1) {
            throw std::invalid_argument(std::string(form));
        }
        return values;
    }

    // The value readers throw std::invalid_argument with what the value should look like. The caller adds where.
    // Far past any monitor, and small enough that every length scaled to device pixels fits the integers it is cast to.
    constexpr float kLargestNumber = 10000.0f;

    float           ParseSignedNumber(std::string_view text) {
        const std::optional<float> value = ParseNumber(text);
        if (!value.has_value()) {
            throw std::invalid_argument(std::format("must be a number with no unit, as 4, not '{}'", text));
        }
        if (std::abs(*value) > kLargestNumber) {
            throw std::invalid_argument(std::format("must be a number from -{} to {}", kLargestNumber, kLargestNumber));
        }
        return *value;
    }

    float ParseLength(std::string_view text) {
        const float value = ParseSignedNumber(text);
        if (value < 0.0f) {
            throw std::invalid_argument("must be 0 or more");
        }
        return value;
    }

    float ParsePositiveLength(std::string_view text) {
        const float value = ParseLength(text);
        if (value <= 0.0f) {
            throw std::invalid_argument("must be more than 0");
        }
        return value;
    }

    std::optional<float> ParseCap(std::string_view text) {
        return ParsePositiveLength(text);
    }

    Spacing ParseSpacing(std::string_view text) {
        const std::vector<std::string_view> numbers = Split(text, kSpace);
        if (numbers.size() == 1) {
            const float both = ParseLength(numbers[0]);
            return Spacing{.yDips = both, .xDips = both};
        }
        if (numbers.size() == 2) {
            return Spacing{.yDips = ParseLength(numbers[0]), .xDips = ParseLength(numbers[1])};
        }
        throw std::invalid_argument("takes one number for every side, or two: above and below, then left and right, as 2 6");
    }

    std::uint32_t HexDigit(char digit) {
        std::uint32_t value = 0;
        if (std::from_chars(&digit, &digit + 1, value, 16).ec != std::errc()) {
            throw std::invalid_argument(std::format("has '{}', which is not a hex digit", digit));
        }
        return value;
    }

    constexpr std::string_view kColorForm = "must be a color: #rrggbb, #rrggbbaa, #rgb or #rgba, optionally followed by an opacity as #191724 90%, or transparent";

    D2D1_COLOR_F               ParseHex(std::string_view text) {
        const std::string_view digits = text.substr(1);
        if (digits.size() != 3 && digits.size() != 4 && digits.size() != 6 && digits.size() != 8) {
            throw std::invalid_argument(std::string(kColorForm));
        }
        const bool           shortForm = digits.size() <= 4;
        const size_t         count     = shortForm ? digits.size() : digits.size() / 2;
        std::array<float, 4> channels{0.0f, 0.0f, 0.0f, 1.0f};
        for (size_t channel = 0; channel < count; ++channel) {
            const std::uint32_t value = shortForm ? HexDigit(digits[channel]) * 17 : (HexDigit(digits[channel * 2]) * 16) + HexDigit(digits[(channel * 2) + 1]);
            channels.at(channel)      = static_cast<float>(value) / 255.0f;
        }
        return D2D1_COLOR_F{.r = channels[0], .g = channels[1], .b = channels[2], .a = channels[3]};
    }

    // A hex color, optionally followed by an opacity percentage that multiplies its alpha, or transparent.
    D2D1_COLOR_F ParseColor(std::string_view text) {
        const std::vector<std::string_view> words = Split(text, kSpace);
        if (words.size() == 1 && words[0] == "transparent") {
            return kTransparent;
        }
        if (words.empty() || words.size() > 2 || !words[0].starts_with('#')) {
            throw std::invalid_argument(std::string(kColorForm));
        }
        D2D1_COLOR_F color = ParseHex(words[0]);
        if (words.size() == 2) {
            const std::optional<float> percent = words[1].ends_with('%') ? ParseNumber(words[1].substr(0, words[1].size() - 1)) : std::nullopt;
            if (!percent.has_value() || *percent < 0.0f || *percent > 100.0f) {
                throw std::invalid_argument(std::format("has the opacity '{}', which must be a percentage from 0% to 100%, as #191724 90%", words[1]));
            }
            color.a *= *percent / 100.0f;
        }
        return color;
    }

    std::wstring ParseFamily(std::string_view text) {
        if (text.starts_with('"') || text.starts_with('\'')) {
            throw std::invalid_argument("takes the family unquoted, as font: Segoe UI");
        }
        if (text.find(',') != std::string_view::npos) {
            throw std::invalid_argument("must name one font family, not a list: every font the bar uses must be installed");
        }
        return ToWide(text);
    }

    DWRITE_FONT_WEIGHT ParseWeight(std::string_view text) {
        if (text == "normal") {
            return DWRITE_FONT_WEIGHT_NORMAL;
        }
        if (text == "bold") {
            return DWRITE_FONT_WEIGHT_BOLD;
        }
        const std::optional<float> value = ParseNumber(text);
        if (!value.has_value() || *value < 100.0f || *value > 900.0f || static_cast<std::int32_t>(*value) % 100 != 0) {
            throw std::invalid_argument("must be normal, bold, or a weight from 100 to 900 in steps of 100");
        }
        return static_cast<DWRITE_FONT_WEIGHT>(static_cast<std::int32_t>(*value));
    }

    bool ParseSwitch(std::string_view text) {
        if (text != "on" && text != "off") {
            throw std::invalid_argument("must be on or off");
        }
        return text == "on";
    }

    // A transition as written: the property it animates, then the transition. none animates nothing.
    struct TransitionValue {
        std::string_view property;
        Transition       transition;
    };

    constexpr Transition kNoTransition{.duration = std::chrono::milliseconds(0), .timing = TimingFunction::Ease};

    struct TimingName {
        std::string_view name;
        TimingFunction   timing;
    };

    constexpr std::array<TimingName, 5> kTimings{{
        {.name = "linear", .timing = TimingFunction::Linear},
        {.name = "ease", .timing = TimingFunction::Ease},
        {.name = "ease-in", .timing = TimingFunction::EaseIn},
        {.name = "ease-out", .timing = TimingFunction::EaseOut},
        {.name = "ease-in-out", .timing = TimingFunction::EaseInOut},
    }};

    std::chrono::milliseconds           ParseDuration(std::string_view text) {
        const bool                 milliseconds = text.ends_with("ms");
        const std::optional<float> value        = milliseconds ? ParseNumber(text.substr(0, text.size() - 2)) :
                             text.ends_with('s')                         ? ParseNumber(text.substr(0, text.size() - 1)) :
                                                                           std::nullopt;
        const float                scaled       = value.value_or(0.0f) * (milliseconds ? 1.0f : 1000.0f);
        if (!value.has_value() || scaled < 0.0f || scaled > 60000.0f) {
            throw std::invalid_argument("takes a duration in ms or s from 0 to 60s, as 400ms or 0.4s");
        }
        return std::chrono::milliseconds(static_cast<std::int64_t>(scaled));
    }

    constexpr std::string_view kTransitionForm =
        "must be none, or transitions separated by commas, each a property, a duration and a timing function, as width 400ms ease-out, background 150ms ease";

    // A property, a duration and an optional timing function (ease when left out).
    TransitionValue ParseTransitionValue(std::string_view text) {
        const std::vector<std::string_view> parts = Split(text, kSpace);
        if (parts.size() != 2 && parts.size() != 3) {
            throw std::invalid_argument(std::string(kTransitionForm));
        }
        TimingFunction timing = TimingFunction::Ease;
        if (parts.size() == 3) {
            const auto entry = std::ranges::find(kTimings, parts[2], &TimingName::name);
            if (entry == kTimings.end()) {
                throw std::invalid_argument(std::format("has the timing function '{}': use linear, ease, ease-in, ease-out or ease-in-out", parts[2]));
            }
            timing = entry->timing;
        }
        return TransitionValue{.property = parts[0], .transition = Transition{.duration = ParseDuration(parts[1]), .timing = timing}};
    }

    std::vector<TransitionValue> ParseTransitionValues(std::string_view text) {
        return ParseCommaList<TransitionValue>(text, kTransitionForm, ParseTransitionValue);
    }

    // The transition a list gives property, the last one naming it, or none.
    Transition ParseTransition(std::string_view text, std::string_view property) {
        Transition found = kNoTransition;
        for (const TransitionValue& value : ParseTransitionValues(text)) {
            if (value.property == property) {
                found = value.transition;
            }
        }
        return found;
    }

    // A reader of one property's transition, for Read.
    auto TransitionReader(std::string_view property) {
        return [property](std::string_view text) { return ParseTransition(text, property); };
    }

    constexpr std::string_view kShadowForm = "must be none, or shadows separated by commas, each an x and y offset, an optional blur and spread, and a color, "
                                             "as 0 1 2 #ffffff 18%";

    // Numbers, then a color.
    InsetShadow ParseShadow(std::string_view text) {
        const std::vector<std::string_view> words = Split(text, kSpace);
        std::vector<float>                  numbers;
        while (numbers.size() < words.size() && ParseNumber(words[numbers.size()]).has_value()) {
            numbers.push_back(ParseSignedNumber(words[numbers.size()]));
        }
        if (numbers.size() < 2 || numbers.size() > 4 || numbers.size() == words.size()) {
            throw std::invalid_argument(std::string(kShadowForm));
        }
        const float blur = numbers.size() > 2 ? numbers[2] : 0.0f;
        if (blur < 0.0f) {
            throw std::invalid_argument("has a negative blur, which must be 0 or more");
        }
        return InsetShadow{
            .xDips = numbers[0], .yDips = numbers[1], .blurDips = blur, .spreadDips = numbers.size() > 3 ? numbers[3] : 0.0f, .color = ParseColor(JoinFrom(words, numbers.size()))};
    }

    std::vector<InsetShadow> ParseShadows(std::string_view text) {
        return ParseCommaList<InsetShadow>(text, kShadowForm, ParseShadow);
    }

    constexpr BorderSide kNoBorder{.widthDips = 0.0f, .color = kTransparent};

    // none, or a width then a color.
    BorderSide ParseBorderSide(std::string_view text) {
        if (text == "none") {
            return kNoBorder;
        }
        const std::vector<std::string_view> words = Split(text, kSpace);
        if (words.size() < 2) {
            throw std::invalid_argument("must be none, or a width and a color, as 1 #6e6a86");
        }
        return BorderSide{.widthDips = ParseLength(words[0]), .color = ParseColor(JoinFrom(words, 1))};
    }

    // What a property's value is checked and read as. A key means the same in every block that takes it.
    enum class ValueKind : std::uint8_t {
        Color,
        Border,
        Length,
        SignedNumber,
        PositiveLength,
        Spacing,
        Family,
        Weight,
        Transition,
        Shadows,
        Switch
    };

    struct PropertyKind {
        std::string_view key;
        ValueKind        kind;
    };

    constexpr std::array<PropertyKind, 23> kProperties{{
        {.key = "color", .kind = ValueKind::Color},
        {.key = "background", .kind = ValueKind::Color},
        {.key = "border", .kind = ValueKind::Border},
        {.key = "border-top", .kind = ValueKind::Border},
        {.key = "border-right", .kind = ValueKind::Border},
        {.key = "border-bottom", .kind = ValueKind::Border},
        {.key = "border-left", .kind = ValueKind::Border},
        {.key = "radius", .kind = ValueKind::Length},
        {.key = "shadow", .kind = ValueKind::Shadows},
        {.key = "padding", .kind = ValueKind::Spacing},
        {.key = "margin", .kind = ValueKind::Spacing},
        {.key = "margin-right", .kind = ValueKind::Length},
        {.key = "font", .kind = ValueKind::Family},
        {.key = "size", .kind = ValueKind::PositiveLength},
        {.key = "weight", .kind = ValueKind::Weight},
        {.key = "width", .kind = ValueKind::PositiveLength},
        {.key = "max-width", .kind = ValueKind::PositiveLength},
        {.key = "max-height", .kind = ValueKind::PositiveLength},
        {.key = "transition", .kind = ValueKind::Transition},
        {.key = "rotate", .kind = ValueKind::SignedNumber},
        {.key = "blur", .kind = ValueKind::Switch},
        {.key = "brightness", .kind = ValueKind::Length},
        {.key = "saturate", .kind = ValueKind::Length},
    }};

    void                                   CheckValue(std::string_view key, std::string_view value) {
        const auto entry = std::ranges::find(kProperties, key, &PropertyKind::key);
        if (entry == kProperties.end()) {
            throw std::invalid_argument(std::format("unknown property {}", key));
        }
        switch (entry->kind) {
            case ValueKind::Color: ParseColor(value); return;
            case ValueKind::Border: ParseBorderSide(value); return;
            case ValueKind::Length: ParseLength(value); return;
            case ValueKind::SignedNumber: ParseSignedNumber(value); return;
            case ValueKind::PositiveLength: ParsePositiveLength(value); return;
            case ValueKind::Spacing: ParseSpacing(value); return;
            case ValueKind::Family: ParseFamily(value); return;
            case ValueKind::Weight: ParseWeight(value); return;
            case ValueKind::Transition: ParseTransitionValues(value); return;
            case ValueKind::Shadows: ParseShadows(value); return;
            case ValueKind::Switch: ParseSwitch(value); return;
        }
        throw std::invalid_argument(std::format("unknown value kind {}", static_cast<std::int32_t>(entry->kind)));
    }

    // What one block may hold: its properties, its blocks by key, and what its transition may animate. A module's block
    // also holds its custom module's classes, any other block, each of the shape classes points to.
    struct BlockShape;

    struct ChildBlock {
        std::string_view  key;
        const BlockShape* shape;
    };

    struct BlockShape {
        std::span<const std::string_view> properties;
        std::span<const ChildBlock>       blocks;
        std::span<const std::string_view> animated;
        const BlockShape*                 classes = nullptr;
    };

    constexpr std::array<std::string_view, 2>  kColors{"color", "background"};
    constexpr std::array<std::string_view, 1>  kColor{"color"};
    constexpr std::array<std::string_view, 1>  kBackground{"background"};
    constexpr std::array<std::string_view, 1>  kRotate{"rotate"};
    constexpr std::array<std::string_view, 2>  kBackgroundAndWidth{"background", "width"};
    constexpr std::array<std::string_view, 3>  kBarProperties{"font", "size", "color"};
    constexpr std::array<std::string_view, 13> kSectionProperties{"background", "border", "border-top", "border-right", "border-bottom", "border-left", "radius",
                                                                  "padding",    "shadow", "margin",     "blur",         "brightness",    "saturate"};
    constexpr std::array<std::string_view, 13> kTooltipProperties{"background", "border", "border-top", "border-right", "border-bottom", "border-left", "radius",
                                                                  "padding",    "shadow", "color",      "blur",         "brightness",    "saturate"};
    constexpr std::array<std::string_view, 15> kMenuProperties{"background", "border", "border-top", "border-right", "border-bottom", "border-left", "radius",    "padding",
                                                               "shadow",     "color",  "blur",       "brightness",   "saturate",      "max-width",   "max-height"};
    constexpr std::array<std::string_view, 12> kModuleProperties{"background", "border",  "border-top", "border-right", "border-bottom", "border-left",
                                                                 "radius",     "padding", "shadow",     "margin",       "color",         "transition"};
    constexpr std::array<std::string_view, 7>  kMenuItemProperties{"color", "background", "weight", "padding", "margin", "radius", "shadow"};
    constexpr std::array<std::string_view, 8>  kItemProperties{"color", "background", "weight", "padding", "margin", "radius", "shadow", "transition"};
    constexpr std::array<std::string_view, 4>  kScrollbarProperties{"width", "background", "radius", "margin"};
    constexpr std::array<std::string_view, 6>  kIconProperties{"font", "size", "color", "margin-right", "rotate", "transition"};
    constexpr std::array<std::string_view, 4>  kDefaultIconProperties{"font", "size", "color", "margin-right"};

    constexpr BlockShape                       kBar{.properties = kBarProperties, .blocks = {}, .animated = {}, .classes = nullptr};
    constexpr BlockShape                       kSection{.properties = kSectionProperties, .blocks = {}, .animated = {}, .classes = nullptr};
    constexpr BlockShape                       kTooltip{.properties = kTooltipProperties, .blocks = {}, .animated = {}, .classes = nullptr};
    constexpr BlockShape                       kScrollbar{.properties = kScrollbarProperties, .blocks = {}, .animated = {}, .classes = nullptr};
    constexpr BlockShape                       kItemHover{.properties = kColors, .blocks = {}, .animated = {}, .classes = nullptr};
    constexpr std::array<ChildBlock, 1>        kMenuActiveBlocks{{{.key = "hover", .shape = &kItemHover}}};
    constexpr BlockShape                       kMenuActive{.properties = kMenuItemProperties, .blocks = kMenuActiveBlocks, .animated = {}, .classes = nullptr};
    constexpr std::array<ChildBlock, 2>        kMenuItemBlocks{{{.key = "hover", .shape = &kItemHover}, {.key = "active", .shape = &kMenuActive}}};
    constexpr BlockShape                       kMenuItem{.properties = kMenuItemProperties, .blocks = kMenuItemBlocks, .animated = {}, .classes = nullptr};
    constexpr std::array<ChildBlock, 2>        kMenuBlocks{{{.key = "item", .shape = &kMenuItem}, {.key = "scrollbar", .shape = &kScrollbar}}};
    constexpr BlockShape                       kMenu{.properties = kMenuProperties, .blocks = kMenuBlocks, .animated = {}, .classes = nullptr};

    constexpr BlockShape                       kIcon{.properties = kIconProperties, .blocks = {}, .animated = kRotate, .classes = nullptr};
    constexpr BlockShape                       kDefaultIcon{.properties = kDefaultIconProperties, .blocks = {}, .animated = {}, .classes = nullptr};
    constexpr BlockShape                       kItemState{.properties = kItemProperties, .blocks = {}, .animated = kColors, .classes = nullptr};
    constexpr std::array<ChildBlock, 5>        kItemBlocks{{{.key = "hover", .shape = &kItemHover},
                                                            {.key = "focused", .shape = &kItemState},
                                                            {.key = "occupied", .shape = &kItemState},
                                                            {.key = "empty", .shape = &kItemState},
                                                            {.key = "toggle", .shape = &kItemState}}};
    constexpr BlockShape                       kItem{.properties = kItemProperties, .blocks = kItemBlocks, .animated = kColors, .classes = nullptr};
    constexpr BlockShape                       kHover{.properties = kBackground, .blocks = {}, .animated = {}, .classes = nullptr};
    constexpr BlockShape                       kAlert{.properties = kColor, .blocks = {}, .animated = {}, .classes = nullptr};
    constexpr BlockShape                       kStateIcon{.properties = kColor, .blocks = {}, .animated = {}, .classes = nullptr};
    constexpr std::array<ChildBlock, 1>        kStateBlocks{{{.key = "icon", .shape = &kStateIcon}}};
    constexpr BlockShape                       kState{.properties = kColor, .blocks = kStateBlocks, .animated = {}, .classes = nullptr};
    constexpr BlockShape                       kExpandedIcon{.properties = kRotate, .blocks = {}, .animated = {}, .classes = nullptr};
    constexpr std::array<ChildBlock, 1>        kExpandedBlocks{{{.key = "icon", .shape = &kExpandedIcon}}};
    constexpr BlockShape                       kExpanded{.properties = {}, .blocks = kExpandedBlocks, .animated = {}, .classes = nullptr};
    constexpr std::array<ChildBlock, 8>        kModuleBlocks{{{.key = "icon", .shape = &kIcon},
                                                              {.key = "item", .shape = &kItem},
                                                              {.key = "hover", .shape = &kHover},
                                                              {.key = "alert", .shape = &kAlert},
                                                              {.key = "warning", .shape = &kState},
                                                              {.key = "critical", .shape = &kState},
                                                              {.key = "muted", .shape = &kState},
                                                              {.key = "expanded", .shape = &kExpanded}}};
    constexpr BlockShape                       kModule{.properties = kModuleProperties, .blocks = kModuleBlocks, .animated = kBackgroundAndWidth, .classes = &kState};
    // The module block styles every module, so it holds nothing only a tray, a group, a battery or an audio module draws.
    constexpr std::array<ChildBlock, 4> kDefaultModuleBlocks{
        {{.key = "icon", .shape = &kDefaultIcon}, {.key = "item", .shape = &kItem}, {.key = "hover", .shape = &kHover}, {.key = "alert", .shape = &kAlert}}};
    constexpr BlockShape kDefaultModule{.properties = kModuleProperties, .blocks = kDefaultModuleBlocks, .animated = kBackground, .classes = &kState};

    struct TopBlock {
        std::string_view  key;
        const BlockShape* shape;
    };

    constexpr std::array<TopBlock, 5> kTopBlocks{{{.key = "bar", .shape = &kBar},
                                                  {.key = "section", .shape = &kSection},
                                                  {.key = "module", .shape = &kDefaultModule},
                                                  {.key = "tooltip", .shape = &kTooltip},
                                                  {.key = "menu", .shape = &kMenu}}};

    bool                              IsBlock(const StyleNode& node) {
        return node.value.empty();
    }

    std::string KeysOf(std::span<const ChildBlock> blocks) {
        return JoinNames(blocks | std::views::transform(&ChildBlock::key));
    }

    // A block as an error names it, as module > item > focused.
    std::string PathOf(std::string_view parent, std::string_view key) {
        return parent.empty() ? std::string(key) : std::format("{} > {}", parent, key);
    }

    // brightness and saturate tone the system's blur, so either without blur: on would do nothing.
    void RequireBlurForTone(const StyleNode& block, std::string_view path, std::string_view source) {
        const auto blur   = std::ranges::find(block.children, std::string_view("blur"), &StyleNode::key);
        const bool blurOn = blur != block.children.end() && blur->value == "on";
        for (const StyleNode& child : block.children) {
            if ((child.key == "brightness" || child.key == "saturate") && !blurOn) {
                throw StyleError(source, child.line, std::format("{}: {} tones the system's blur, so it takes blur: on beside it", path, child.key));
            }
        }
    }

    void CheckBlock(const StyleNode& block, const BlockShape& shape, std::string_view path, std::string_view source) {
        for (const StyleNode& child : block.children) {
            const bool property = std::ranges::find(shape.properties, child.key) != shape.properties.end();
            const auto nested   = std::ranges::find(shape.blocks, child.key, &ChildBlock::key);
            const bool isClass  = nested == shape.blocks.end() && shape.classes != nullptr && !IsStyledClass(child.key);
            const auto where    = PathOf(path, child.key);
            if (IsBlock(child)) {
                if (property) {
                    throw StyleError(source, child.line, std::format("{}: {} is a property, written as {}: value", path, child.key, child.key));
                }
                if (nested == shape.blocks.end() && !isClass) {
                    throw StyleError(
                        source, child.line,
                        std::format("{}: {} is not a block {} holds, which are: {}", path, child.key, path,
                                    shape.blocks.empty() ? std::string("none") : KeysOf(shape.blocks) + (shape.classes != nullptr ? ", or a custom module's class" : "")));
                }
                CheckBlock(child, nested != shape.blocks.end() ? *nested->shape : *shape.classes, where, source);
                continue;
            }
            if (nested != shape.blocks.end()) {
                throw StyleError(source, child.line, std::format("{}: {} is a block, with its lines indented beneath it", path, child.key));
            }
            if (!property) {
                throw StyleError(source, child.line,
                                 std::format("{}: {} is not a property {} takes, which are: {}", path, child.key, path,
                                             shape.properties.empty() ? std::string("none") : JoinNames(shape.properties)));
            }
            try {
                CheckValue(child.key, child.value);
                if (child.key == "transition") {
                    for (const TransitionValue& transition : ParseTransitionValues(child.value)) {
                        if (std::ranges::find(shape.animated, transition.property) == shape.animated.end()) {
                            throw std::invalid_argument(std::format("animates {} here, not {}", JoinNames(shape.animated), transition.property));
                        }
                    }
                }
            } catch (const std::invalid_argument& error) { throw StyleError(source, child.line, std::format("{}: {} {}", path, child.key, error.what())); }
        }
        RequireBlurForTone(block, path, source);
    }

    // The last rule of the format left for once the tree is built: a block must hold something.
    void RequireFilledBlocks(const std::vector<StyleNode>& nodes, std::string_view source) {
        for (const StyleNode& node : nodes) {
            if (IsBlock(node) && node.children.empty()) {
                throw StyleError(source, node.line, std::format("{}: has no value and nothing indented beneath it", node.key));
            }
            RequireFilledBlocks(node.children, source);
        }
    }

    // One line's key and value, its depth from its indent.
    struct Line {
        std::uint32_t    number;
        size_t           level;
        std::string_view key;
        std::string_view value;
    };

    std::optional<Line> ParseLine(std::string_view raw, std::uint32_t number, std::string_view source) {
        const std::string_view text = raw.substr(0, raw.find("//"));
        if (text.find('\t') != std::string_view::npos) {
            throw StyleError(source, number, "has a tab: indent by two spaces per level");
        }
        const std::string_view content = Trim(text);
        if (content.empty()) {
            return std::nullopt;
        }
        const size_t indent = text.find_first_not_of(' ');
        if (indent % 2 != 0) {
            throw StyleError(source, number, std::format("is indented by {} spaces: indent by two spaces per level", indent));
        }
        const size_t colon = content.find(':');
        if (colon == std::string_view::npos) {
            throw StyleError(source, number, std::format("'{}' must be key: value, or key: alone to open a block", content));
        }
        const std::string_view key = content.substr(0, colon);
        if (key.empty() || !std::ranges::all_of(key, IsNameCharacter)) {
            throw StyleError(source, number, std::format("'{}' is not a key: write letters, digits, '_' and '-', then a colon", key));
        }
        return Line{.number = number, .level = indent / 2, .key = key, .value = Trim(content.substr(colon + 1))};
    }

    // Past the deepest block any shape holds, so the passes that recurse once per level stay shallow.
    constexpr size_t       kMaxLevels = 8;

    std::vector<StyleNode> ParseTree(std::string_view text, std::string_view source) {
        std::vector<StyleNode>                                 roots;
        std::vector<std::vector<StyleNode>*>                   open{&roots}; // open[n] takes the lines at level n
        std::vector<std::map<std::string_view, std::uint32_t>> keyLines(1);  // keyLines[n] holds open[n]'s keys by line
        std::uint32_t                                          number = 0;
        for (const auto piece : text | std::views::split('\n')) {
            ++number;
            const std::optional<Line> line = ParseLine(std::string_view(piece.begin(), piece.end()), number, source);
            if (!line.has_value()) {
                continue;
            }
            if (line->level >= open.size()) {
                throw StyleError(source, line->number,
                                 std::format("{}: is indented past the block above it: a block's lines sit one level deeper than its key, and a property holds none", line->key));
            }
            if (line->level >= kMaxLevels) {
                throw StyleError(source, line->number, std::format("{}: is nested {} levels deep, and a style nests at most {}", line->key, line->level + 1, kMaxLevels));
            }
            open.resize(line->level + 1);
            keyLines.resize(line->level + 1);
            const auto [earlier, added] = keyLines.back().try_emplace(line->key, line->number);
            if (!added) {
                throw StyleError(source, line->number, std::format("{}: is already set on line {} of the same block", line->key, earlier->second));
            }
            std::vector<StyleNode>& siblings = *open.back();
            siblings.push_back(StyleNode{.key = std::string(line->key), .value = std::string(line->value), .line = line->number, .children = {}});
            if (line->value.empty()) {
                open.push_back(&siblings.back().children);
                keyLines.emplace_back();
            }
        }
        RequireFilledBlocks(roots, source);
        return roots;
    }

    using Variables = std::map<std::string, std::string, std::less<>>;

    Variables ResolveVariables(const std::vector<StyleNode>& roots, std::string_view source) {
        Variables  variables;
        const auto vars = std::ranges::find(roots, std::string_view("vars"), &StyleNode::key);
        if (vars == roots.end()) {
            return variables;
        }
        if (!IsBlock(*vars)) {
            throw StyleError(source, vars->line, "vars: is a block of name: value lines");
        }
        for (const StyleNode& variable : vars->children) {
            if (IsBlock(variable)) {
                throw StyleError(source, variable.line, std::format("vars > {}: vars holds name: value lines, not blocks", variable.key));
            }
            if (variable.value.find('$') != std::string::npos) {
                throw StyleError(source, variable.line, std::format("vars > {}: a var cannot use another var: give it a value", variable.key));
            }
            variables.emplace(variable.key, variable.value);
        }
        return variables;
    }

    // Many times any real value, and it bounds what vars can multiply a file into.
    constexpr size_t kMaxSubstitutedLength = 1024;

    // Every $name in value replaced with its vars value.
    std::string Substitute(const StyleNode& property, const Variables& variables, std::string_view source) {
        std::string value;
        size_t      start = 0;
        for (size_t dollar = property.value.find('$'); dollar != std::string::npos; dollar = property.value.find('$', start)) {
            size_t end = dollar + 1;
            while (end < property.value.size() && IsNameCharacter(property.value[end])) {
                ++end;
            }
            const std::string_view name  = std::string_view(property.value).substr(dollar + 1, end - dollar - 1);
            const auto             found = variables.find(name);
            if (found == variables.end()) {
                throw StyleError(source, property.line,
                                 name.empty() ? std::format("{}: $ must be followed by the name of a var", property.key) :
                                                std::format("{}: ${} names no var: define it in vars, as vars:\n  {}: #e0def4", property.key, name, name));
            }
            value += std::string_view(property.value).substr(start, dollar - start);
            value += found->second;
            if (value.size() > kMaxSubstitutedLength) {
                throw StyleError(source, property.line, std::format("{}: is longer than {} characters once its vars are filled in", property.key, kMaxSubstitutedLength));
            }
            start = end;
        }
        value += std::string_view(property.value).substr(start);
        return value;
    }

    std::vector<StyleNode> SubstituteAll(const std::vector<StyleNode>& nodes, const Variables& variables, std::string_view source) {
        std::vector<StyleNode> substituted;
        substituted.reserve(nodes.size());
        for (const StyleNode& node : nodes) {
            substituted.push_back(StyleNode{.key      = node.key,
                                            .value    = IsBlock(node) ? std::string() : Substitute(node, variables, source),
                                            .line     = node.line,
                                            .children = SubstituteAll(node.children, variables, source)});
        }
        return substituted;
    }

    const StyleNode* Child(const StyleNode* block, std::string_view key) {
        if (block == nullptr) {
            return nullptr;
        }
        const auto found = std::ranges::find(block->children, key, &StyleNode::key);
        return found == block->children.end() ? nullptr : &*found;
    }

    const StyleNode* TopLevel(const StyleSheet& sheet, std::string_view key) {
        const auto found = std::ranges::find(sheet.blocks, key, &StyleNode::key);
        return found == sheet.blocks.end() ? nullptr : &*found;
    }

    // Blocks that set the same thing, the one that wins first, with no null entries.
    using Chain = std::vector<const StyleNode*>;

    Chain ChainOf(std::initializer_list<const StyleNode*> blocks) {
        Chain chain;
        std::ranges::copy_if(blocks, std::back_inserter(chain), [](const StyleNode* block) { return block != nullptr; });
        return chain;
    }

    // Each block's child block named key, in the chain's order.
    Chain Children(const Chain& chain, std::string_view key) {
        Chain children;
        for (const StyleNode* block : chain) {
            if (const StyleNode* child = Child(block, key); child != nullptr && IsBlock(*child)) {
                children.push_back(child);
            }
        }
        return children;
    }

    Chain Concat(const Chain& first, const Chain& second) {
        Chain joined = first;
        joined.insert(joined.end(), second.begin(), second.end());
        return joined;
    }

    // The first property named key in the chain.
    const StyleNode* FirstSet(const Chain& chain, std::string_view key) {
        for (const StyleNode* block : chain) {
            if (const StyleNode* property = Child(block, key); property != nullptr && !IsBlock(*property)) {
                return property;
            }
        }
        return nullptr;
    }

    template <typename Reader>
    auto Read(const Chain& chain, std::string_view key, Reader reader, decltype(reader(std::string_view())) inherited) -> decltype(reader(std::string_view())) {
        const StyleNode* property = FirstSet(chain, key);
        return property == nullptr ? std::move(inherited) : reader(property->value);
    }

    D2D1_COLOR_F ResolveColor(const Chain& chain, std::string_view key, const D2D1_COLOR_F& inherited) {
        return Read(chain, key, ParseColor, inherited);
    }

    constexpr Spacing kNoSpace{.yDips = 0.0f, .xDips = 0.0f};

    Spacing           ResolveSpacing(const Chain& chain, std::string_view key) {
        return Read(chain, key, ParseSpacing, kNoSpace);
    }

    // A side's own border wins over its block's border, and a more specific block's border over either.
    BorderSide ResolveBorderSide(const Chain& chain, std::string_view side) {
        const std::string own = std::format("border-{}", side);
        for (const StyleNode* block : chain) {
            const StyleNode* sideBorder = Child(block, own);
            const StyleNode* property   = sideBorder != nullptr ? sideBorder : Child(block, "border");
            if (property != nullptr) {
                return ParseBorderSide(property->value);
            }
        }
        return kNoBorder;
    }

    BoxStyle ResolveBox(const Chain& chain) {
        return BoxStyle{.background = ResolveColor(chain, "background", kTransparent),
                        .border     = {.top    = ResolveBorderSide(chain, "top"),
                                       .right  = ResolveBorderSide(chain, "right"),
                                       .bottom = ResolveBorderSide(chain, "bottom"),
                                       .left   = ResolveBorderSide(chain, "left")},
                        .radiusDips = Read(chain, "radius", ParseLength, 0.0f),
                        .padding    = ResolveSpacing(chain, "padding"),
                        .shadows    = Read(chain, "shadow", ParseShadows, std::vector<InsetShadow>{})};
    }

    constexpr Backdrop kNoBackdrop{.blur = false, .brightness = 1.0f, .saturation = 1.0f};

    Backdrop           ResolveBackdrop(const StyleNode* block) {
        const Chain chain = ChainOf({block});
        if (!Read(chain, "blur", ParseSwitch, false)) {
            return kNoBackdrop;
        }
        return Backdrop{.blur = true, .brightness = Read(chain, "brightness", ParseLength, 1.0f), .saturation = Read(chain, "saturate", ParseLength, 1.0f)};
    }

    // An item from its blocks, and its colors under the cursor from hover's.
    ItemStyle ResolveItem(const Chain& item, const Chain& hover, const D2D1_COLOR_F& color) {
        const D2D1_COLOR_F itemColor  = ResolveColor(item, "color", color);
        const D2D1_COLOR_F background = ResolveColor(item, "background", kTransparent);
        return ItemStyle{.color                = itemColor,
                         .background           = background,
                         .weight               = Read(item, "weight", ParseWeight, DWRITE_FONT_WEIGHT_NORMAL),
                         .padding              = ResolveSpacing(item, "padding"),
                         .margin               = ResolveSpacing(item, "margin"),
                         .radiusDips           = Read(item, "radius", ParseLength, 0.0f),
                         .shadows              = Read(item, "shadow", ParseShadows, std::vector<InsetShadow>{}),
                         .hoverColor           = ResolveColor(hover, "color", itemColor),
                         .hoverBackground      = ResolveColor(hover, "background", background),
                         .backgroundTransition = Read(item, "transition", TransitionReader("background"), kNoTransition),
                         .colorTransition      = Read(item, "transition", TransitionReader("color"), kNoTransition)};
    }

    // An item in one state: the state's blocks over the item's, and under the cursor the state's colors over the item's
    // hover, so a state such as focused keeps the colors it sets.
    ItemStyle ResolveItemState(const Chain& items, std::string_view state, const D2D1_COLOR_F& color) {
        const Chain states = Children(items, state);
        return ResolveItem(Concat(states, items), Concat(states, Children(items, "hover")), color);
    }

    // A module's text and icon colors in one state: the state's blocks win over the module's own, and the icon inherits
    // the state's text color.
    StateColors ResolveStateColors(const Chain& module, std::string_view state, const D2D1_COLOR_F& color) {
        const Chain        states = Children(module, state);
        const D2D1_COLOR_F text   = ResolveColor(states, "color", color);
        return StateColors{.text = text, .icon = ResolveColor(Concat(Children(states, "icon"), Children(module, "icon")), "color", text)};
    }

    std::string_view TypeName(ModuleKind kind) {
        const auto entry = std::ranges::find(kModuleTypes, kind, &ModuleTypeName::kind);
        if (entry == kModuleTypes.end()) {
            throw std::invalid_argument(std::format("unknown module kind {}", static_cast<std::int32_t>(kind)));
        }
        return entry->name;
    }

    // A state that colors a module's icon as well as its text, and the one module type that draws it.
    struct OwnedState {
        std::string_view name;
        ModuleKind       owner;
    };

    constexpr std::array<OwnedState, 3> kOwnedStates{
        {{.name = "warning", .owner = ModuleKind::Battery}, {.name = "critical", .owner = ModuleKind::Battery}, {.name = "muted", .owner = ModuleKind::Audio}}};

    // What a module's own block holds that only some module types draw.
    void RequireDrawnBlocks(const StyleNode& own, ModuleKind kind, const std::vector<std::string>& classes, std::string_view source) {
        const std::string_view type = TypeName(kind);
        for (const OwnedState& state : kOwnedStates) {
            if (const StyleNode* block = Child(&own, state.name); block != nullptr && kind != state.owner) {
                throw StyleError(source, block->line,
                                 std::format("{} > {}: {} colors only {} modules, and {} is a {} module", own.key, state.name, state.name, TypeName(state.owner), own.key, type));
            }
        }
        if (kind != ModuleKind::Tray && kind != ModuleKind::Group) {
            const StyleNode* icon = Child(&own, "icon");
            for (const StyleNode* drawn : {Child(&own, "expanded"), Child(icon, "rotate"), Child(icon, "transition")}) {
                if (drawn != nullptr) {
                    throw StyleError(source, drawn->line, std::format("{}: {} animates a tray or group as it unfolds, and {} is a {} module", own.key, drawn->key, own.key, type));
                }
            }
            const StyleNode* transition = Child(&own, "transition");
            if (transition != nullptr && std::ranges::any_of(ParseTransitionValues(transition->value), [](const TransitionValue& value) { return value.property == "width"; })) {
                throw StyleError(source, transition->line,
                                 std::format("{}: transition: width animates a tray or group as it unfolds, and {} is a {} module", own.key, own.key, type));
            }
        }
        for (const StyleNode& child : own.children) {
            const bool known = std::ranges::find(kModuleBlocks, child.key, &ChildBlock::key) != kModuleBlocks.end();
            if (IsBlock(child) && !known && std::ranges::find(classes, child.key) == classes.end()) {
                throw StyleError(source, child.line,
                                 std::format("{}: {} is not a block a module holds ({}), nor a class {} lists in its classes", own.key, child.key, KeysOf(kModuleBlocks), own.key));
            }
        }
    }

    const StyleNode& Required(const StyleSheet& sheet, const StyleNode& bar, std::string_view key, std::string_view example) {
        const StyleNode* property = Child(&bar, key);
        if (property == nullptr) {
            throw StyleError(sheet.source, bar.line, std::format("bar must set {}, as {}: {}", key, key, example));
        }
        return *property;
    }

} // namespace

StyleSheet ParseStyleSheet(std::string_view text, std::string source) {
    const std::vector<StyleNode> tree      = ParseTree(text, source);
    const Variables              variables = ResolveVariables(tree, source);
    StyleSheet                   sheet{.source = std::move(source), .blocks = {}};
    for (StyleNode& block : SubstituteAll(tree, variables, sheet.source)) {
        if (block.key == "vars") {
            continue;
        }
        if (!IsBlock(block)) {
            throw StyleError(sheet.source, block.line, std::format("{}: the top level holds blocks, as {}: with its lines indented beneath it", block.key, block.key));
        }
        const auto top = std::ranges::find(kTopBlocks, block.key, &TopBlock::key);
        CheckBlock(block, top != kTopBlocks.end() ? *top->shape : kModule, block.key, sheet.source);
        sheet.blocks.push_back(std::move(block));
    }
    return sheet;
}

StyleConfig ResolveBarStyle(const StyleSheet& sheet) {
    const StyleNode* bar = TopLevel(sheet, "bar");
    if (bar == nullptr) {
        throw ConfigError(std::format("{}: must have a bar block that sets font, size and color, as bar:\n  font: Segoe UI\n  size: 13\n  color: #e0def4", sheet.source));
    }
    const StyleNode*   section   = TopLevel(sheet, "section");
    const StyleNode*   tooltip   = TopLevel(sheet, "tooltip");
    const StyleNode*   menu      = TopLevel(sheet, "menu");
    const Chain        menuChain = ChainOf({menu});
    const Chain        items     = Children(menuChain, "item");
    const Chain        active    = Children(items, "active");
    const D2D1_COLOR_F color     = ParseColor(Required(sheet, *bar, "color", "#e0def4").value);
    const D2D1_COLOR_F menuColor = ResolveColor(menuChain, "color", color);
    const Chain        scrollbar = Children(menuChain, "scrollbar");
    return StyleConfig{.font         = ParseFamily(Required(sheet, *bar, "font", "Segoe UI").value),
                       .fontSizeDips = ParsePositiveLength(Required(sheet, *bar, "size", "13").value),
                       .color        = color,
                       .section      = {.box = ResolveBox(ChainOf({section})), .margin = ResolveSpacing(ChainOf({section}), "margin"), .backdrop = ResolveBackdrop(section)},
                       .tooltip      = {.box = ResolveBox(ChainOf({tooltip})), .color = ResolveColor(ChainOf({tooltip}), "color", color), .backdrop = ResolveBackdrop(tooltip)},
                       .menu         = {.box           = ResolveBox(menuChain),
                                        .color         = menuColor,
                                        .backdrop      = ResolveBackdrop(menu),
                                        .maxWidthDips  = Read(menuChain, "max-width", ParseCap, std::nullopt),
                                        .maxHeightDips = Read(menuChain, "max-height", ParseCap, std::nullopt),
                                        .item          = ResolveItem(items, Children(items, "hover"), menuColor),
                                        .active        = ResolveItem(Concat(active, items), Concat(Children(active, "hover"), Concat(active, Children(items, "hover"))), menuColor),
                                        .scrollbar     = {.widthDips  = Read(scrollbar, "width", ParsePositiveLength, 0.0f),
                                                          .background = ResolveColor(scrollbar, "background", kTransparent),
                                                          .radiusDips = Read(scrollbar, "radius", ParseLength, 0.0f),
                                                          .margin     = ResolveSpacing(scrollbar, "margin")}}};
}

ModuleStyle ResolveModuleStyle(const StyleSheet& sheet, const StyleConfig& bar, ModuleKind kind, std::string_view name, const std::vector<std::string>& classes) {
    const StyleNode* own = TopLevel(sheet, name);
    if (own != nullptr) {
        RequireDrawnBlocks(*own, kind, classes, sheet.source);
    }
    const Chain              module   = ChainOf({own, TopLevel(sheet, "module")});
    const BoxStyle           box      = ResolveBox(module);
    const D2D1_COLOR_F       color    = ResolveColor(module, "color", bar.color);
    const Chain              icon     = Children(module, "icon");
    const Chain              items    = Children(module, "item");
    const float              rotation = Read(icon, "rotate", ParseSignedNumber, 0.0f);
    std::vector<ClassColors> classColors;
    classColors.reserve(classes.size());
    for (const std::string& declared : classes) {
        classColors.push_back(ClassColors{.name = declared, .colors = ResolveStateColors(module, declared, color)});
    }
    return ModuleStyle{.box                         = box,
                       .margin                      = ResolveSpacing(module, "margin"),
                       .hover                       = ResolveColor(Children(module, "hover"), "background", box.background),
                       .backgroundTransition        = Read(module, "transition", TransitionReader("background"), kNoTransition),
                       .color                       = color,
                       .alertColor                  = ResolveColor(Children(module, "alert"), "color", color),
                       .warning                     = ResolveStateColors(module, "warning", color),
                       .critical                    = ResolveStateColors(module, "critical", color),
                       .muted                       = ResolveStateColors(module, "muted", color),
                       .classes                     = std::move(classColors),
                       .icon                        = {.font            = Read(icon, "font", ParseFamily, bar.font),
                                                       .sizeDips        = Read(icon, "size", ParsePositiveLength, bar.fontSizeDips),
                                                       .color           = ResolveColor(icon, "color", color),
                                                       .gapDips         = Read(icon, "margin-right", ParseLength, 0.0f),
                                                       .rotationDegrees = rotation,
                                                       .transition      = Read(icon, "transition", TransitionReader("rotate"), kNoTransition)},
                       .item                        = ResolveItem(items, Children(items, "hover"), color),
                       .focused                     = ResolveItemState(items, "focused", color),
                       .occupied                    = ResolveItemState(items, "occupied", color),
                       .empty                       = ResolveItemState(items, "empty", color),
                       .toggle                      = ResolveItemState(items, "toggle", color),
                       .transition                  = Read(module, "transition", TransitionReader("width"), kNoTransition),
                       .expandedIconRotationDegrees = Read(Children(Children(module, "expanded"), "icon"), "rotate", ParseSignedNumber, rotation)};
}

void RequireKnownModules(const StyleSheet& sheet, const std::vector<std::string>& names) {
    for (const StyleNode& block : sheet.blocks) {
        if (!IsReservedModuleName(block.key) && std::ranges::find(names, block.key) == names.end()) {
            throw StyleError(sheet.source, block.line,
                             std::format("{}: names no module, and the top level holds vars, bar, section, module, tooltip, menu and the modules the config defines: {}", block.key,
                                         names.empty() ? std::string("none") : JoinNames(names)));
        }
    }
}

bool IsReservedModuleName(std::string_view name) {
    return name == "vars" || std::ranges::find(kTopBlocks, name, &TopBlock::key) != kTopBlocks.end();
}

bool IsNameCharacter(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '_' || character == '-';
}

bool IsStyledClass(std::string_view name) {
    return std::ranges::find(kModuleBlocks, name, &ChildBlock::key) != kModuleBlocks.end();
}

void RequireDeclaredClasses(const StyleSheet& sheet, const std::vector<DeclaredClasses>& declared) {
    const StyleNode* module = TopLevel(sheet, "module");
    if (module == nullptr) {
        return;
    }
    for (const StyleNode& child : module->children) {
        const bool known = std::ranges::find(kDefaultModuleBlocks, child.key, &ChildBlock::key) != kDefaultModuleBlocks.end();
        const bool declares =
            std::ranges::any_of(declared, [&child](const DeclaredClasses& custom) { return std::ranges::find(custom.classes, child.key) != custom.classes.end(); });
        if (IsBlock(child) && !known && !declares) {
            throw StyleError(sheet.source, child.line,
                             std::format("module: {} is not a block module holds ({}), nor a class a custom module lists in its classes", child.key, KeysOf(kDefaultModuleBlocks)));
        }
    }
}
