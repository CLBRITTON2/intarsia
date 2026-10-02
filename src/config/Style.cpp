#include "config/Style.hpp"

#include "platform/Text.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

    constexpr D2D1_COLOR_F     kTransparent{.r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 0.0f};
    constexpr std::string_view kSpace = " \t\r\n";

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

    std::uint32_t LineAt(std::string_view text, size_t offset) {
        return static_cast<std::uint32_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), '\n')) + 1;
    }

    // Comments become spaces, newlines kept, so every offset keeps its line.
    std::string BlankComments(std::string_view text, std::string_view source) {
        std::string blanked(text);
        size_t      start = blanked.find("/*");
        while (start != std::string::npos) {
            const size_t end = blanked.find("*/", start + 2);
            if (end == std::string::npos) {
                throw StyleError(source, LineAt(text, start), "this /* comment is never closed with */");
            }
            std::replace_if(
                blanked.begin() + static_cast<std::ptrdiff_t>(start), blanked.begin() + static_cast<std::ptrdiff_t>(end + 2), [](char character) { return character != '\n'; },
                ' ');
            start = blanked.find("/*", end + 2);
        }
        return blanked;
    }

    // The first of stops at or after start that is outside quotes and parentheses, or npos.
    size_t FindOutside(std::string_view text, size_t start, std::string_view stops) {
        char         quote = 0;
        std::int32_t depth = 0;
        for (size_t index = start; index < text.size(); ++index) {
            const char character = text[index];
            if (quote != 0) {
                quote = character == quote ? 0 : quote;
            } else if (character == '"' || character == '\'') {
                quote = character;
            } else if (character == '(') {
                ++depth;
            } else if (character == ')') {
                --depth;
            } else if (depth == 0 && stops.find(character) != std::string_view::npos) {
                return index;
            }
        }
        return std::string_view::npos;
    }

    // Splits on separator outside quotes and parentheses, trimming each part and dropping empty ones.
    std::vector<std::string_view> SplitOutside(std::string_view text, std::string_view separators) {
        std::vector<std::string_view> parts;
        size_t                        start = 0;
        while (start <= text.size()) {
            const size_t           end  = FindOutside(text, start, separators);
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
    template <typename Value, typename ReadEntry>
    std::vector<Value> ReadCommaList(std::string_view text, std::string_view form, ReadEntry readEntry) {
        if (text == "none") {
            return {};
        }
        std::vector<Value> values;
        for (const std::string_view entry : SplitOutside(text, ",")) {
            values.push_back(readEntry(entry));
        }
        size_t separators = 0;
        for (size_t comma = FindOutside(text, 0, ","); comma != std::string_view::npos; comma = FindOutside(text, comma + 1, ",")) {
            ++separators;
        }
        if (values.empty() || separators != values.size() - 1) {
            throw std::invalid_argument(std::string(form));
        }
        return values;
    }

    // What a declaration's value is checked and read as.
    enum class ValueKind : std::uint8_t {
        Color,
        Length,
        PositiveLength,
        Spacing,
        Family,
        Weight,
        Transition,
        Rotation,
        Shadows,
        Backdrop
    };

    struct PropertyKind {
        std::string_view property;
        ValueKind        kind;
    };

    constexpr std::array<PropertyKind, 32> kProperties{{
        {.property = "color", .kind = ValueKind::Color},
        {.property = "background", .kind = ValueKind::Color},
        {.property = "border-top-width", .kind = ValueKind::Length},
        {.property = "border-right-width", .kind = ValueKind::Length},
        {.property = "border-bottom-width", .kind = ValueKind::Length},
        {.property = "border-left-width", .kind = ValueKind::Length},
        {.property = "border-top-color", .kind = ValueKind::Color},
        {.property = "border-right-color", .kind = ValueKind::Color},
        {.property = "border-bottom-color", .kind = ValueKind::Color},
        {.property = "border-left-color", .kind = ValueKind::Color},
        {.property = "border-radius", .kind = ValueKind::Length},
        {.property = "box-shadow", .kind = ValueKind::Shadows},
        {.property = "padding", .kind = ValueKind::Spacing},
        {.property = "margin", .kind = ValueKind::Spacing},
        {.property = "margin-right", .kind = ValueKind::Length},
        {.property = "font-family", .kind = ValueKind::Family},
        {.property = "font-size", .kind = ValueKind::PositiveLength},
        {.property = "font-weight", .kind = ValueKind::Weight},
        {.property = "width", .kind = ValueKind::PositiveLength},
        {.property = "transition", .kind = ValueKind::Transition},
        {.property = "transform", .kind = ValueKind::Rotation},
        {.property = "max-width", .kind = ValueKind::PositiveLength},
        {.property = "max-height", .kind = ValueKind::PositiveLength},
        {.property = "backdrop-filter", .kind = ValueKind::Backdrop},
        // Shorthands, expanded into the longhands above as they are read.
        {.property = "border", .kind = ValueKind::Color},
        {.property = "border-top", .kind = ValueKind::Color},
        {.property = "border-right", .kind = ValueKind::Color},
        {.property = "border-bottom", .kind = ValueKind::Color},
        {.property = "border-left", .kind = ValueKind::Color},
        {.property = "border-width", .kind = ValueKind::Length},
        {.property = "border-color", .kind = ValueKind::Color},
        {.property = "background-color", .kind = ValueKind::Color},
    }};

    std::optional<ValueKind>               KindOf(std::string_view property) {
        const auto entry = std::ranges::find(kProperties, property, &PropertyKind::property);
        return entry == kProperties.end() ? std::nullopt : std::optional<ValueKind>(entry->kind);
    }

    // The value readers throw std::invalid_argument with what the value should look like. The caller adds where.
    // Far past any monitor, and small enough that every length scaled to device pixels fits the integers it is cast to.
    constexpr float kLongestLengthDips = 10000.0f;

    float           ReadSignedLength(std::string_view text) {
        const std::optional<float> value = text == "0" ? std::optional<float>(0.0f) : text.ends_with("px") ? ParseNumber(text.substr(0, text.size() - 2)) : std::nullopt;
        if (!value.has_value()) {
            throw std::invalid_argument("must be a length in px, as 4px");
        }
        if (std::abs(*value) > kLongestLengthDips) {
            throw std::invalid_argument(std::format("must be a length no longer than {}px", kLongestLengthDips));
        }
        return *value;
    }

    float ReadLength(std::string_view text) {
        const float value = ReadSignedLength(text);
        if (value < 0.0f) {
            throw std::invalid_argument("must be 0px or more");
        }
        return value;
    }

    float ReadPositiveLength(std::string_view text) {
        const float value = ReadLength(text);
        if (value <= 0.0f) {
            throw std::invalid_argument("must be more than 0px");
        }
        return value;
    }

    Spacing ReadSpacing(std::string_view text) {
        const std::vector<std::string_view> lengths = SplitOutside(text, kSpace);
        if (lengths.size() == 1) {
            const float both = ReadLength(lengths[0]);
            return Spacing{.yDips = both, .xDips = both};
        }
        if (lengths.size() == 2) {
            return Spacing{.yDips = ReadLength(lengths[0]), .xDips = ReadLength(lengths[1])};
        }
        throw std::invalid_argument("takes one length for every side, or two: above and below, then left and right, as 2px 6px");
    }

    float ReadChannel(std::string_view text, float maximum) {
        const std::optional<float> value = ParseNumber(Trim(text));
        if (!value.has_value() || *value < 0.0f || *value > maximum) {
            throw std::invalid_argument(std::format("has a channel '{}' outside 0 to {}", Trim(text), maximum));
        }
        return *value;
    }

    std::uint32_t HexDigit(char digit) {
        std::uint32_t value = 0;
        if (std::from_chars(&digit, &digit + 1, value, 16).ec != std::errc()) {
            throw std::invalid_argument(std::format("has '{}', which is not a hex digit", digit));
        }
        return value;
    }

    // #rgb, #rgba, #rrggbb, #rrggbbaa, rgb(r, g, b), rgba(r, g, b, a) or transparent.
    D2D1_COLOR_F ReadColor(std::string_view text) {
        constexpr std::string_view kForms = "must be a color: #rrggbb, #rrggbbaa, #rgb, rgb(r, g, b), rgba(r, g, b, a) or transparent";
        if (text == "transparent") {
            return kTransparent;
        }
        if (text.starts_with('#')) {
            const std::string_view digits = text.substr(1);
            if (digits.size() != 3 && digits.size() != 4 && digits.size() != 6 && digits.size() != 8) {
                throw std::invalid_argument(std::string(kForms));
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
        const bool alpha = text.starts_with("rgba(");
        if ((alpha || text.starts_with("rgb(")) && text.ends_with(')')) {
            const std::string_view              inside = text.substr(alpha ? 5 : 4, text.size() - (alpha ? 6 : 5));
            const std::vector<std::string_view> parts  = SplitOutside(inside, ",");
            if (parts.size() != (alpha ? 4u : 3u)) {
                throw std::invalid_argument(alpha ? "rgba takes four values: red, green and blue from 0 to 255, then alpha from 0 to 1" :
                                                    "rgb takes three values: red, green and blue from 0 to 255");
            }
            return D2D1_COLOR_F{.r = ReadChannel(parts[0], 255.0f) / 255.0f,
                                .g = ReadChannel(parts[1], 255.0f) / 255.0f,
                                .b = ReadChannel(parts[2], 255.0f) / 255.0f,
                                .a = alpha ? ReadChannel(parts[3], 1.0f) : 1.0f};
        }
        throw std::invalid_argument(std::string(kForms));
    }

    std::wstring ReadFamily(std::string_view text) {
        if (FindOutside(text, 0, ",") != std::string_view::npos) {
            throw std::invalid_argument("must name one font family, not a list: every font the bar uses must be installed");
        }
        const bool             quoted = text.size() >= 2 && (text.front() == '"' || text.front() == '\'') && text.back() == text.front();
        const std::string_view family = quoted ? text.substr(1, text.size() - 2) : text;
        if (family.empty()) {
            throw std::invalid_argument("must name a font family, as \"Segoe UI\"");
        }
        return ToWide(family);
    }

    DWRITE_FONT_WEIGHT ReadWeight(std::string_view text) {
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

    std::chrono::milliseconds           ReadDuration(std::string_view text) {
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

    // A property, a duration and an optional timing function (ease when left out, as in CSS).
    TransitionValue ReadTransitionValue(std::string_view text) {
        const std::vector<std::string_view> parts = SplitOutside(text, kSpace);
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
        return TransitionValue{.property = parts[0], .transition = Transition{.duration = ReadDuration(parts[1]), .timing = timing}};
    }

    std::vector<TransitionValue> ReadTransitionValues(std::string_view text) {
        return ReadCommaList<TransitionValue>(text, kTransitionForm, ReadTransitionValue);
    }

    // The transition a list gives property, the last one naming it as in CSS, or none.
    Transition TransitionOf(std::string_view text, std::string_view property) {
        Transition found = kNoTransition;
        for (const TransitionValue& value : ReadTransitionValues(text)) {
            if (value.property == property) {
                found = value.transition;
            }
        }
        return found;
    }

    // A reader of one property's transition, for Cascaded::Read.
    auto TransitionReader(std::string_view property) {
        return [property](std::string_view text) { return TransitionOf(text, property); };
    }

    // none, or rotate(<degrees>deg).
    float ReadRotation(std::string_view text) {
        if (text == "none") {
            return 0.0f;
        }
        const std::optional<float> degrees = text.starts_with("rotate(") && text.ends_with("deg)") ? ParseNumber(Trim(text.substr(7, text.size() - 11))) : std::nullopt;
        if (!degrees.has_value()) {
            throw std::invalid_argument("must be none or a rotation in degrees, as rotate(180deg)");
        }
        return *degrees;
    }

    constexpr std::string_view kShadowForm = "must be none, or inset shadows separated by commas, each inset, an x and y offset, an optional blur and spread, and a color, "
                                             "as inset 0 1px 2px rgba(255, 255, 255, 0.18)";

    InsetShadow                ReadShadow(std::string_view text) {
        bool                        inset = false;
        std::vector<float>          lengths;
        std::optional<D2D1_COLOR_F> color;
        for (const std::string_view part : SplitOutside(text, kSpace)) {
            if (part == "inset" && !inset) {
                inset = true;
            } else if (part == "0" || part.ends_with("px")) {
                lengths.push_back(ReadSignedLength(part));
            } else if (!color.has_value()) {
                color = ReadColor(part);
            } else {
                throw std::invalid_argument(std::string(kShadowForm));
            }
        }
        if (!inset) {
            throw std::invalid_argument("draws inside the box only, so every shadow starts with inset, as inset 0 1px 2px rgba(255, 255, 255, 0.18)");
        }
        if (lengths.size() < 2 || lengths.size() > 4 || !color.has_value()) {
            throw std::invalid_argument(std::string(kShadowForm));
        }
        const float blur = lengths.size() > 2 ? lengths[2] : 0.0f;
        if (blur < 0.0f) {
            throw std::invalid_argument("has a negative blur, which must be 0px or more");
        }
        return InsetShadow{.xDips = lengths[0], .yDips = lengths[1], .blurDips = blur, .spreadDips = lengths.size() > 3 ? lengths[3] : 0.0f, .color = *color};
    }

    std::vector<InsetShadow> ReadShadows(std::string_view text) {
        return ReadCommaList<InsetShadow>(text, kShadowForm, ReadShadow);
    }

    constexpr std::string_view kBackdropForm = "must be none, or blur (the system's, whose radius is its own) then optionally brightness() and saturate() once each, "
                                               "as blur brightness(0.4) saturate(180%)";

    constexpr Backdrop         kNoBackdrop{.blur = false, .brightness = 1.0f, .saturation = 1.0f};

    // The amount of name(<n>) or name(<n>%), or none when text is not that function.
    std::optional<float> FilterAmount(std::string_view text, std::string_view name) {
        if (!text.starts_with(name) || !text.substr(name.size()).starts_with('(') || !text.ends_with(')')) {
            return std::nullopt;
        }
        const std::string_view     inside  = Trim(text.substr(name.size() + 1, text.size() - name.size() - 2));
        const bool                 percent = inside.ends_with('%');
        const std::optional<float> amount  = ParseNumber(percent ? inside.substr(0, inside.size() - 1) : inside);
        if (!amount.has_value() || *amount < 0.0f) {
            throw std::invalid_argument(std::format("has {}, whose amount must be a number or percentage of 0 or more, as {}(0.4) or {}(40%)", text, name, name));
        }
        return percent ? *amount / 100.0f : *amount;
    }

    Backdrop ReadBackdrop(std::string_view text) {
        if (text == "none") {
            return kNoBackdrop;
        }
        const std::vector<std::string_view> parts = SplitOutside(text, kSpace);
        if (parts.empty() || parts[0] != "blur") {
            throw std::invalid_argument(std::string(kBackdropForm));
        }
        std::optional<float> brightness;
        std::optional<float> saturation;
        for (size_t index = 1; index < parts.size(); ++index) {
            const std::optional<float> brightnessAmount = FilterAmount(parts[index], "brightness");
            const std::optional<float> saturationAmount = FilterAmount(parts[index], "saturate");
            if (brightnessAmount.has_value() && !brightness.has_value()) {
                brightness = brightnessAmount;
            } else if (saturationAmount.has_value() && !saturation.has_value()) {
                saturation = saturationAmount;
            } else {
                throw std::invalid_argument(std::string(kBackdropForm));
            }
        }
        return Backdrop{.blur = true, .brightness = brightness.value_or(1.0f), .saturation = saturation.value_or(1.0f)};
    }

    void CheckValue(ValueKind kind, std::string_view value) {
        switch (kind) {
            case ValueKind::Color: ReadColor(value); return;
            case ValueKind::Length: ReadLength(value); return;
            case ValueKind::PositiveLength: ReadPositiveLength(value); return;
            case ValueKind::Spacing: ReadSpacing(value); return;
            case ValueKind::Family: ReadFamily(value); return;
            case ValueKind::Weight: ReadWeight(value); return;
            case ValueKind::Transition: ReadTransitionValues(value); return;
            case ValueKind::Rotation: ReadRotation(value); return;
            case ValueKind::Shadows: ReadShadows(value); return;
            case ValueKind::Backdrop: ReadBackdrop(value); return;
        }
        throw std::invalid_argument(std::format("unknown value kind {}", static_cast<std::int32_t>(kind)));
    }

    constexpr std::array<std::string_view, 4> kSides{"top", "right", "bottom", "left"};

    // One longhand per side, as border-width sets border-top-width and the other three.
    std::vector<Declaration> ExpandSides(std::string_view part, const std::string& value, std::uint32_t line) {
        std::vector<Declaration> sides;
        sides.reserve(kSides.size());
        for (const std::string_view side : kSides) {
            sides.push_back(Declaration{.property = std::format("border-{}-{}", side, part), .value = value, .line = line});
        }
        return sides;
    }

    // border or border-<side>: none, or a width, solid and a color in any order, as CSS writes it.
    std::vector<Declaration> ExpandBorder(const Declaration& border, const std::vector<std::string_view>& sides) {
        std::vector<Declaration> longhands;
        if (border.value == "none") {
            for (const std::string_view side : sides) {
                longhands.push_back(Declaration{.property = std::format("border-{}-width", side), .value = "0", .line = border.line});
            }
            return longhands;
        }
        std::optional<std::string> width;
        std::optional<std::string> color;
        bool                       solid = false;
        for (const std::string_view part : SplitOutside(border.value, kSpace)) {
            if (part == "solid") {
                solid = true;
            } else if (part == "0" || part.ends_with("px")) {
                width = std::string(part);
            } else {
                color = std::string(part);
            }
        }
        if (!solid || !width.has_value() || !color.has_value()) {
            throw std::invalid_argument("must be none, or a width, solid and a color, as 1px solid #6e6a8640");
        }
        for (const std::string_view side : sides) {
            longhands.push_back(Declaration{.property = std::format("border-{}-width", side), .value = *width, .line = border.line});
            longhands.push_back(Declaration{.property = std::format("border-{}-color", side), .value = *color, .line = border.line});
        }
        return longhands;
    }

    std::vector<Declaration> ExpandShorthand(const Declaration& declaration) {
        if (declaration.property == "border") {
            return ExpandBorder(declaration, {kSides.begin(), kSides.end()});
        }
        for (const std::string_view side : kSides) {
            if (declaration.property == std::format("border-{}", side)) {
                return ExpandBorder(declaration, {side});
            }
        }
        if (declaration.property == "border-width") {
            return ExpandSides("width", declaration.value, declaration.line);
        }
        if (declaration.property == "border-color") {
            return ExpandSides("color", declaration.value, declaration.line);
        }
        return {declaration};
    }

    // A menu entry's .active is matched by the menu's own selector form, so a custom module may still declare it.
    constexpr std::array<std::string_view, 12> kClasses{"module", "alert", "warning", "critical", "muted", "expanded", "icon", "item", "focused", "occupied", "empty", "toggle"};

    bool                                       IsModuleType(std::string_view type) {
        return std::ranges::find(kModuleTypes, type, &ModuleTypeName::name) != kModuleTypes.end();
    }

    std::string KnownTypes() {
        return "section, tooltip, menu, " + JoinNames(kModuleTypes | std::views::transform(&ModuleTypeName::name));
    }

    // Reads one identifier from start, throwing when there is none.
    std::string_view ReadIdent(std::string_view text, size_t start, std::string_view what) {
        size_t end = start;
        while (end < text.size() && IsIdentCharacter(text[end])) {
            ++end;
        }
        if (end == start) {
            throw std::invalid_argument(std::format("'{}' has a {} with no name", text, what));
        }
        return text.substr(start, end - start);
    }

    Compound ParseCompound(std::string_view text) {
        Compound compound{.type = {}, .id = {}, .classes = {}, .hover = false};
        size_t   index = 0;
        if (text.starts_with('*')) {
            compound.type = "*";
            index         = 1;
        } else if (IsIdentCharacter(text.front())) {
            compound.type = std::string(ReadIdent(text, 0, "type"));
            index         = compound.type.size();
            if (compound.type != "section" && compound.type != "tooltip" && compound.type != "menu" && !IsModuleType(compound.type)) {
                throw std::invalid_argument(std::format("'{}' is not a type intarsia styles, known types are: {}", compound.type, KnownTypes()));
            }
        }
        while (index < text.size()) {
            const char marker = text[index];
            if (marker != '#' && marker != '.' && marker != ':') {
                throw std::invalid_argument(std::format("'{}' has '{}' where #, . or : should be", text, marker));
            }
            const std::string_view name = ReadIdent(text, index + 1, marker == '#' ? "#id" : marker == '.' ? ".class" : ":state");
            index += 1 + name.size();
            if (marker == '#') {
                if (!compound.id.empty()) {
                    throw std::invalid_argument(std::format("'{}' has two ids", text));
                }
                compound.id = std::string(name);
            } else if (marker == '.') {
                // A class intarsia does not style is a custom module's, checked against the config by RequireDeclaredClasses.
                compound.classes.emplace_back(name);
            } else if (name == "hover") {
                compound.hover = true;
            } else {
                throw std::invalid_argument(std::format("':{}' is not supported, :hover is the only state", name));
            }
        }
        return compound;
    }

    bool HasClass(const Compound& compound, std::string_view name) {
        return std::ranges::find(compound.classes, name) != compound.classes.end();
    }

    // A state that colors a module's icon as well as its text, and the one module type that draws it.
    struct OwnedState {
        std::string_view name;
        ModuleKind       owner;
    };

    constexpr std::array<OwnedState, 3> kOwnedStates{
        {{.name = "warning", .owner = ModuleKind::Battery}, {.name = "critical", .owner = ModuleKind::Battery}, {.name = "muted", .owner = ModuleKind::Audio}}};

    bool IsOwnedState(std::string_view name) {
        return std::ranges::find(kOwnedStates, name, &OwnedState::name) != kOwnedStates.end();
    }

    // An owned state or a custom module's class: either colors a module's text and its .icon.
    bool IsColoringState(std::string_view name) {
        return IsOwnedState(name) || !IsStyledClass(name);
    }

    bool HasColoringState(const Compound& compound) {
        return std::ranges::any_of(compound.classes, [](const std::string& name) { return IsColoringState(name); });
    }

    bool IsState(std::string_view name) {
        return name == "alert" || name == "expanded" || IsColoringState(name);
    }

    // The classes naming a state a module is in, of which it is in one at a time.
    std::int64_t StateCount(const Compound& compound) {
        return std::ranges::count_if(compound.classes, [](const std::string& name) { return IsState(name); });
    }

    // A module's box: a module type, an id, .module or a state, and :hover.
    bool IsModuleCompound(const Compound& compound) {
        const bool onlyModuleClasses = std::ranges::all_of(compound.classes, [](const std::string& name) { return name == "module" || IsState(name); });
        const bool typed             = compound.type.empty() || IsModuleType(compound.type);
        return onlyModuleClasses && typed && (!compound.type.empty() || !compound.id.empty() || !compound.classes.empty());
    }

    // .icon, or .item with at most one of .focused, .occupied, .empty and .toggle, and optionally :hover.
    std::optional<StyleTarget> PartTarget(const Compound& compound) {
        if (!compound.type.empty() || !compound.id.empty()) {
            return std::nullopt;
        }
        if (compound.classes == std::vector<std::string>{"icon"} && !compound.hover) {
            return StyleTarget::Icon;
        }
        const auto states =
            std::ranges::count_if(compound.classes, [](const std::string& name) { return name == "focused" || name == "occupied" || name == "empty" || name == "toggle"; });
        if (HasClass(compound, "item") && states <= 1 && static_cast<size_t>(states) + 1 == compound.classes.size()) {
            return StyleTarget::Item;
        }
        return std::nullopt;
    }

    StyleTarget TargetOf(const std::vector<Compound>& compounds, std::string_view text) {
        const Compound& last = compounds.back();
        if (compounds.size() == 1 && last.type == "*") {
            if (!last.id.empty() || !last.classes.empty() || last.hover) {
                throw std::invalid_argument("* stands alone: it sets the bar-wide font-family, font-size and color");
            }
            return StyleTarget::Bar;
        }
        if (compounds.size() == 1 && (last.type == "section" || last.type == "tooltip" || last.type == "menu")) {
            if (!last.id.empty() || !last.classes.empty() || last.hover) {
                throw std::invalid_argument(std::format("{} takes no id, class or :hover", last.type));
            }
            return last.type == "section" ? StyleTarget::Section : last.type == "tooltip" ? StyleTarget::Tooltip : StyleTarget::Menu;
        }
        if (compounds.front().type == "menu") {
            const Compound& menu      = compounds.front();
            const bool      plainMenu = menu.id.empty() && menu.classes.empty() && !menu.hover;
            const bool      bare      = last.type.empty() && last.id.empty();
            const bool      entry     = bare &&
                (last.classes == std::vector<std::string>{"item"} || last.classes == std::vector<std::string>{"item", "active"} ||
                 last.classes == std::vector<std::string>{"active", "item"});
            if (plainMenu && bare && last.classes == std::vector<std::string>{"scrollbar"} && !last.hover) {
                return StyleTarget::MenuScrollbar;
            }
            if (!plainMenu || !entry) {
                throw std::invalid_argument(
                    std::format("'{}': a menu's entries are menu .item, with .active and :hover, as menu .item.active:hover, and its scroll bar is menu .scrollbar", text));
            }
            return StyleTarget::MenuItem;
        }
        if (!compounds.empty() && StateCount(compounds.front()) > 1) {
            throw std::invalid_argument(
                std::format("'{}' combines two of .alert, .warning, .critical, .muted, .expanded and a custom module's classes, and a module is in one at a time", text));
        }
        if (const std::optional<StyleTarget> part = PartTarget(last)) {
            if (compounds.size() == 2 && (!IsModuleCompound(compounds[0]) || compounds[0].hover || HasClass(compounds[0], "alert"))) {
                throw std::invalid_argument(std::format("'{}' must start with a module, as #cpu .icon, without .alert or :hover", text));
            }
            if (compounds.size() == 2 && HasClass(compounds[0], "expanded") && *part != StyleTarget::Icon) {
                throw std::invalid_argument(std::format("'{}': .expanded styles an unfolded tray's or group's .icon only, as #tray.expanded .icon", text));
            }
            if (compounds.size() == 2 && HasColoringState(compounds[0]) && *part != StyleTarget::Icon) {
                throw std::invalid_argument(
                    std::format("'{}': .warning, .critical, .muted and a custom module's classes style a module's text and .icon only, as #battery.warning .icon", text));
            }
            return *part;
        }
        if (compounds.size() == 1 && IsModuleCompound(last) && HasClass(last, "expanded")) {
            throw std::invalid_argument(std::format("'{}': .expanded styles an unfolded tray's or group's .icon only, as #tray.expanded .icon", text));
        }
        if (compounds.size() == 1 && IsModuleCompound(last)) {
            if (last.hover && StateCount(last) > 0) {
                throw std::invalid_argument(std::format("'{}' combines a state with :hover, which style different things: text color and background", text));
            }
            return StyleTarget::Module;
        }
        throw std::invalid_argument(std::format("'{}' matches nothing intarsia draws: style *, section, tooltip, menu, a module (clock, #time, .module), "
                                                "a module's .icon or .item, or a menu's .item",
                                                text));
    }

    std::array<std::uint32_t, 3> SpecificityOf(const std::vector<Compound>& compounds) {
        std::array<std::uint32_t, 3> specificity{0, 0, 0};
        for (const Compound& compound : compounds) {
            specificity[0] += compound.id.empty() ? 0 : 1;
            specificity[1] += static_cast<std::uint32_t>(compound.classes.size()) + (compound.hover ? 1 : 0);
            specificity[2] += compound.type.empty() || compound.type == "*" ? 0 : 1;
        }
        return specificity;
    }

    Selector ParseSelector(std::string_view text) {
        if (text.find_first_of(">+~") != std::string_view::npos) {
            throw std::invalid_argument(std::format("'{}' uses a combinator: only a space, as in #cpu .icon, is supported", text));
        }
        std::vector<Compound> compounds;
        for (const std::string_view part : SplitOutside(text, kSpace)) {
            compounds.push_back(ParseCompound(part));
        }
        if (compounds.empty() || compounds.size() > 2) {
            throw std::invalid_argument(std::format("'{}' must be one part, or a module then its .icon or .item", text));
        }
        const StyleTarget target = TargetOf(compounds, text);
        return Selector{.compounds = compounds, .target = target, .specificity = SpecificityOf(compounds)};
    }

    // What each target takes. :hover takes background only and a state color only: each changes one thing, and none
    // changes a size.
    std::vector<std::string_view> AllowedProperties(const Selector& selector) {
        constexpr std::array<std::string_view, 11> kBox{"background",    "border",      "border-width",  "border-color", "border-top", "border-right",
                                                        "border-bottom", "border-left", "border-radius", "padding",      "box-shadow"};
        std::vector<std::string_view>              allowed;
        switch (selector.target) {
            case StyleTarget::Bar: return {"font-family", "font-size", "color"};
            case StyleTarget::Section:
                allowed.assign(kBox.begin(), kBox.end());
                allowed.insert(allowed.end(), {"margin", "backdrop-filter"});
                return allowed;
            case StyleTarget::Tooltip:
                allowed.assign(kBox.begin(), kBox.end());
                allowed.insert(allowed.end(), {"color", "backdrop-filter"});
                return allowed;
            case StyleTarget::Menu:
                allowed.assign(kBox.begin(), kBox.end());
                allowed.insert(allowed.end(), {"color", "backdrop-filter", "max-width", "max-height"});
                return allowed;
            case StyleTarget::MenuScrollbar: return {"width", "background", "border-radius", "margin"};
            case StyleTarget::Icon:
                // An unfolded tray's or group's icon turns and nothing else, so its size never changes as it unfolds.
                if (selector.compounds.size() == 2 && HasClass(selector.compounds.front(), "expanded")) {
                    return {"transform"};
                }
                if (selector.compounds.size() == 2 && HasColoringState(selector.compounds.front())) {
                    return {"color"};
                }
                return {"font-family", "font-size", "color", "margin-right", "transform", "transition"};
            case StyleTarget::Item:
            case StyleTarget::MenuItem:
                // An item under the cursor changes color only: a change of size would move every item after it.
                if (selector.compounds.back().hover) {
                    return {"color", "background"};
                }
                allowed = {"color", "background", "font-weight", "padding", "margin", "border-radius", "box-shadow"};
                if (selector.target == StyleTarget::Item) {
                    allowed.emplace_back("transition");
                }
                return allowed;
            case StyleTarget::Module: {
                const Compound& compound = selector.compounds.front();
                if (compound.hover) {
                    return {"background"};
                }
                if (HasClass(compound, "alert") || HasColoringState(compound)) {
                    return {"color"};
                }
                allowed.assign(kBox.begin(), kBox.end());
                allowed.insert(allowed.end(), {"margin", "color", "transition"});
                return allowed;
            }
        }
        throw std::invalid_argument(std::format("unknown style target {}", static_cast<std::int32_t>(selector.target)));
    }

    // One rule block as written, before its selector list is split and its values checked.
    struct RawRule {
        std::string_view         selectors;
        std::vector<Declaration> declarations;
        std::uint32_t            line;
    };

    std::vector<Declaration> ParseDeclarations(std::string_view text, std::string_view body, size_t bodyOffset, std::string_view source) {
        std::vector<Declaration> declarations;
        size_t                   start = 0;
        while (start < body.size()) {
            const size_t           end     = FindOutside(body, start, ";");
            const std::string_view piece   = body.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
            const std::string_view trimmed = Trim(piece);
            if (!trimmed.empty()) {
                const std::uint32_t line  = LineAt(text, bodyOffset + start + piece.find(trimmed.front()));
                const size_t        colon = trimmed.find(':');
                if (colon == std::string_view::npos) {
                    throw StyleError(source, line, std::format("'{}' must be property: value", trimmed));
                }
                const std::string_view value = Trim(trimmed.substr(colon + 1));
                if (value.find("!important") != std::string_view::npos) {
                    throw StyleError(source, line, "!important is not supported: a more specific selector wins instead");
                }
                declarations.push_back(Declaration{.property = std::string(Trim(trimmed.substr(0, colon))), .value = std::string(value), .line = line});
            }
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }
        return declarations;
    }

    std::vector<RawRule> ParseBlocks(std::string_view text, std::string_view source) {
        std::vector<RawRule> rules;
        size_t               position = 0;
        while (true) {
            position = text.find_first_not_of(kSpace, position);
            if (position == std::string_view::npos) {
                return rules;
            }
            const std::uint32_t line = LineAt(text, position);
            if (text[position] == '@') {
                throw StyleError(source, line, "at-rules such as @import and @media are not supported");
            }
            const size_t open = FindOutside(text, position, "{};");
            if (open == std::string_view::npos || text[open] != '{') {
                throw StyleError(source, line, "a selector must be followed by { and its declarations }");
            }
            const size_t close = FindOutside(text, open + 1, "{}");
            if (close == std::string_view::npos) {
                throw StyleError(source, line, "this rule's { is never closed with }");
            }
            if (text[close] == '{') {
                throw StyleError(source, LineAt(text, close), "rules cannot nest: close the rule above with } first");
            }
            rules.push_back(RawRule{.selectors    = Trim(text.substr(position, open - position)),
                                    .declarations = ParseDeclarations(text, text.substr(open + 1, close - open - 1), open + 1, source),
                                    .line         = line});
            position = close + 1;
        }
    }

    using Variables = std::map<std::string, std::string, std::less<>>;

    // Every var(--name) in value replaced with the :root value of --name.
    std::string Substitute(const Declaration& declaration, const Variables& variables, std::string_view source) {
        std::string value = declaration.value;
        size_t      start = value.find("var(");
        while (start != std::string::npos) {
            const size_t end = value.find(')', start);
            if (end == std::string::npos) {
                throw StyleError(source, declaration.line, "var( is never closed with )");
            }
            const std::string name  = std::string(Trim(std::string_view(value).substr(start + 4, end - start - 4)));
            const auto        found = variables.find(name);
            if (found == variables.end()) {
                throw StyleError(source, declaration.line, std::format("var({}) names no custom property: define it in :root, as :root {{ {}: #e0def4; }}", name, name));
            }
            value.replace(start, end - start + 1, found->second);
            start = value.find("var(", start + found->second.size());
        }
        return value;
    }

    Variables ReadVariables(const std::vector<RawRule>& raw, std::string_view source) {
        Variables variables;
        for (const RawRule& rule : raw) {
            if (rule.selectors != ":root") {
                continue;
            }
            for (const Declaration& declaration : rule.declarations) {
                if (!declaration.property.starts_with("--")) {
                    throw StyleError(source, declaration.line,
                                     std::format("{} does not belong in :root, which only defines custom properties such as --foreground", declaration.property));
                }
                if (declaration.value.find("var(") != std::string::npos) {
                    throw StyleError(source, declaration.line, "a custom property cannot use var(): give it a value");
                }
                variables.insert_or_assign(declaration.property, declaration.value);
            }
        }
        return variables;
    }

    // A module's box animates its background and, a tray's or group's, its width as it unfolds, an icon its rotation and
    // an item its colors.
    std::vector<std::string_view> AnimatedProperties(StyleTarget target) {
        if (target == StyleTarget::Icon) {
            return {"transform"};
        }
        if (target == StyleTarget::Item) {
            return {"background", "color"};
        }
        return {"background", "width"};
    }

    void RequireTransitionedProperties(StyleTarget target, std::string_view value) {
        const std::vector<std::string_view> animated = AnimatedProperties(target);
        for (const TransitionValue& transition : ReadTransitionValues(value)) {
            if (std::ranges::find(animated, transition.property) == animated.end()) {
                throw std::invalid_argument(std::format("animates {} here, not {}, as transition: {} 300ms ease", JoinNames(animated), transition.property, animated.front()));
            }
        }
    }

    // Declarations with var() replaced, shorthands expanded and every value checked against what the selector takes.
    std::vector<Declaration> CheckDeclarations(const Selector& selector, std::string_view selectorText, const std::vector<Declaration>& declarations, const Variables& variables,
                                               std::string_view source) {
        const std::vector<std::string_view> allowed = AllowedProperties(selector);
        std::vector<Declaration>            checked;
        for (const Declaration& written : declarations) {
            const Declaration declaration{.property = written.property == "background-color" ? std::string("background") : written.property,
                                          .value    = Substitute(written, variables, source),
                                          .line     = written.line};
            try {
                if (!KindOf(declaration.property).has_value()) {
                    throw std::invalid_argument("is not a property intarsia draws");
                }
                if (std::ranges::find(allowed, declaration.property) == allowed.end()) {
                    throw std::invalid_argument(std::format("does not apply to {}, which takes: {}", selectorText, JoinNames(allowed)));
                }
                for (const Declaration& longhand : ExpandShorthand(declaration)) {
                    CheckValue(*KindOf(longhand.property), longhand.value);
                    if (longhand.property == "transition") {
                        RequireTransitionedProperties(selector.target, longhand.value);
                    }
                    checked.push_back(longhand);
                }
            } catch (const std::invalid_argument& error) { throw StyleError(source, declaration.line, std::format("{}: {} {}", selectorText, declaration.property, error.what())); }
        }
        return checked;
    }

    // One element a selector part is matched against: a module's box, or its icon or an item.
    struct Element {
        std::string_view              type;
        std::string_view              id;
        std::vector<std::string_view> classes;
        bool                          hover;
    };

    bool Matches(const Compound& compound, const Element& element) {
        return (compound.type.empty() || compound.type == element.type) && (compound.id.empty() || compound.id == element.id) && (!compound.hover || element.hover) &&
            std::ranges::all_of(compound.classes, [&element](const std::string& name) { return std::ranges::find(element.classes, name) != element.classes.end(); });
    }

    // The winning declaration of each property for one element, and where it came from for errors.
    class Cascaded {
      public:
        Cascaded(const StyleSheet& sheet, StyleTarget target, const Element* module, const Element* part) : _source(sheet.source) {
            std::vector<const StyleRule*> matching;
            for (const StyleRule& rule : sheet.rules) {
                const std::vector<Compound>& compounds = rule.selector.compounds;
                if (rule.selector.target != target) {
                    continue;
                }
                const bool matches = part == nullptr ? module == nullptr || Matches(compounds.front(), *module) :
                                                       Matches(compounds.back(), *part) && (compounds.size() == 1 || Matches(compounds.front(), *module));
                if (matches) {
                    matching.push_back(&rule);
                }
            }
            // Later rules of equal specificity win, as in CSS, so the sort keeps file order.
            std::ranges::stable_sort(matching, {}, [](const StyleRule* rule) { return rule->selector.specificity; });
            for (const StyleRule* rule : matching) {
                for (const Declaration& declaration : rule->declarations) {
                    _winners.insert_or_assign(declaration.property, &declaration);
                }
            }
        }

        const Declaration* Find(std::string_view property) const {
            const auto found = _winners.find(property);
            return found == _winners.end() ? nullptr : found->second;
        }

        template <typename Reader>
        auto Read(std::string_view property, Reader reader, decltype(reader(std::string_view())) inherited) const -> decltype(reader(std::string_view())) {
            const Declaration* declaration = Find(property);
            return declaration == nullptr ? std::move(inherited) : reader(declaration->value);
        }

        D2D1_COLOR_F Color(std::string_view property, const D2D1_COLOR_F& inherited) const {
            return Read(property, ReadColor, inherited);
        }
        float Length(std::string_view property, float inherited) const {
            return Read(property, ReadLength, inherited);
        }
        Spacing Space(std::string_view property, const Spacing& inherited) const {
            return Read(property, ReadSpacing, inherited);
        }

        const std::string& Source() const {
            return _source;
        }

      private:
        std::string                                            _source;
        std::map<std::string, const Declaration*, std::less<>> _winners;
    };

    constexpr Spacing kNoSpace{.yDips = 0.0f, .xDips = 0.0f};

    BorderSide        ReadSide(const Cascaded& box, std::string_view side) {
        return BorderSide{.widthDips = box.Length(std::format("border-{}-width", side), 0.0f), .color = box.Color(std::format("border-{}-color", side), kTransparent)};
    }

    BoxStyle ReadBox(const Cascaded& box) {
        return BoxStyle{.background = box.Color("background", kTransparent),
                        .border     = {.top = ReadSide(box, "top"), .right = ReadSide(box, "right"), .bottom = ReadSide(box, "bottom"), .left = ReadSide(box, "left")},
                        .radiusDips = box.Length("border-radius", 0.0f),
                        .padding    = box.Space("padding", kNoSpace),
                        .shadows    = box.Read("box-shadow", ReadShadows, std::vector<InsetShadow>{})};
    }

    // An item of owner, a module or the menu, in one state or none.
    ItemStyle ReadItem(const StyleSheet& sheet, StyleTarget target, const Element& owner, std::string_view state, const D2D1_COLOR_F& color) {
        const std::vector<std::string_view> classes = state.empty() ? std::vector<std::string_view>{"item"} : std::vector<std::string_view>{"item", state};
        const Element                       item{.type = {}, .id = {}, .classes = classes, .hover = false};
        const Element                       hovered{.type = {}, .id = {}, .classes = classes, .hover = true};
        const Cascaded                      cascaded(sheet, target, &owner, &item);
        const Cascaded                      hover(sheet, target, &owner, &hovered);
        const D2D1_COLOR_F                  itemColor  = cascaded.Color("color", color);
        const D2D1_COLOR_F                  background = cascaded.Color("background", kTransparent);
        return ItemStyle{.color                = itemColor,
                         .background           = background,
                         .weight               = cascaded.Read("font-weight", ReadWeight, DWRITE_FONT_WEIGHT_NORMAL),
                         .padding              = cascaded.Space("padding", kNoSpace),
                         .margin               = cascaded.Space("margin", kNoSpace),
                         .radiusDips           = cascaded.Length("border-radius", 0.0f),
                         .shadows              = cascaded.Read("box-shadow", ReadShadows, std::vector<InsetShadow>{}),
                         .hoverColor           = hover.Color("color", itemColor),
                         .hoverBackground      = hover.Color("background", background),
                         .backgroundTransition = cascaded.Read("transition", TransitionReader("background"), kNoTransition),
                         .colorTransition      = cascaded.Read("transition", TransitionReader("color"), kNoTransition)};
    }

    std::string_view TypeName(ModuleKind kind) {
        const auto entry = std::ranges::find(kModuleTypes, kind, &ModuleTypeName::kind);
        if (entry == kModuleTypes.end()) {
            throw std::invalid_argument(std::format("unknown module kind {}", static_cast<std::int32_t>(kind)));
        }
        return entry->name;
    }

    // A module's text and icon colors in one state: rules for the state win over the module's own by specificity, and
    // the icon inherits the state's text color.
    StateColors ReadStateColors(const StyleSheet& sheet, std::string_view type, std::string_view name, std::string_view state, const D2D1_COLOR_F& color) {
        const Element      module{.type = type, .id = name, .classes = {"module", state}, .hover = false};
        const Element      icon{.type = {}, .id = {}, .classes = {"icon"}, .hover = false};
        const D2D1_COLOR_F text = Cascaded(sheet, StyleTarget::Module, &module, nullptr).Color("color", color);
        return StateColors{.text = text, .icon = Cascaded(sheet, StyleTarget::Icon, &module, &icon).Color("color", text)};
    }

    // A rule for a state only another type draws, such as .muted on a clock, would do nothing.
    void RequireOwnStates(const StyleSheet& sheet, ModuleKind kind, std::string_view type, std::string_view name) {
        for (const OwnedState& state : kOwnedStates) {
            if (state.owner == kind) {
                continue;
            }
            const Element module{.type = type, .id = name, .classes = {"module", state.name}, .hover = false};
            for (const StyleRule& rule : sheet.rules) {
                const Compound& first = rule.selector.compounds.front();
                if (HasClass(first, state.name) && Matches(first, module)) {
                    throw StyleError(sheet.source, rule.line, std::format(".{} colors only {} modules, and {} is a {} module", state.name, TypeName(state.owner), name, type));
                }
            }
        }
    }

    const Declaration& Required(const Cascaded& bar, std::string_view property, std::string_view example) {
        const Declaration* declaration = bar.Find(property);
        if (declaration == nullptr) {
            throw ConfigError(std::format("{}: * must set {}, as * {{ {}: {}; }}", bar.Source(), property, property, example));
        }
        return *declaration;
    }

} // namespace

StyleSheet ParseStyleSheet(std::string_view text, std::string source) {
    const std::string          blanked   = BlankComments(text, source);
    const std::vector<RawRule> raw       = ParseBlocks(blanked, source);
    const Variables            variables = ReadVariables(raw, source);
    StyleSheet                 sheet{.source = std::move(source), .rules = {}};
    for (const RawRule& rule : raw) {
        if (rule.selectors == ":root") {
            continue;
        }
        for (const std::string_view selectorText : SplitOutside(rule.selectors, ",")) {
            Selector selector{};
            try {
                selector = ParseSelector(selectorText);
            } catch (const std::invalid_argument& error) { throw StyleError(sheet.source, rule.line, error.what()); }
            std::vector<Declaration> declarations = CheckDeclarations(selector, selectorText, rule.declarations, variables, sheet.source);
            sheet.rules.push_back(StyleRule{.selector = std::move(selector), .declarations = std::move(declarations), .line = rule.line});
        }
    }
    return sheet;
}

StyleConfig ResolveBarStyle(const StyleSheet& sheet) {
    const Cascaded     bar(sheet, StyleTarget::Bar, nullptr, nullptr);
    const Cascaded     section(sheet, StyleTarget::Section, nullptr, nullptr);
    const Cascaded     tooltip(sheet, StyleTarget::Tooltip, nullptr, nullptr);
    const Cascaded     menu(sheet, StyleTarget::Menu, nullptr, nullptr);
    const Cascaded     scrollbar(sheet, StyleTarget::MenuScrollbar, nullptr, nullptr);
    const Element      menuElement{.type = "menu", .id = {}, .classes = {}, .hover = false};
    const D2D1_COLOR_F color     = ReadColor(Required(bar, "color", "#e0def4").value);
    const D2D1_COLOR_F menuColor = menu.Color("color", color);
    const auto         readCap   = [](std::string_view text) { return std::optional<float>(ReadPositiveLength(text)); };
    return StyleConfig{.font         = ReadFamily(Required(bar, "font-family", "\"Segoe UI\"").value),
                       .fontSizeDips = ReadPositiveLength(Required(bar, "font-size", "14px").value),
                       .color        = color,
                       .section = {.box = ReadBox(section), .margin = section.Space("margin", kNoSpace), .backdrop = section.Read("backdrop-filter", ReadBackdrop, kNoBackdrop)},
                       .tooltip = {.box = ReadBox(tooltip), .color = tooltip.Color("color", color), .backdrop = tooltip.Read("backdrop-filter", ReadBackdrop, kNoBackdrop)},
                       .menu    = {.box           = ReadBox(menu),
                                   .color         = menuColor,
                                   .backdrop      = menu.Read("backdrop-filter", ReadBackdrop, kNoBackdrop),
                                   .maxWidthDips  = menu.Read("max-width", readCap, std::nullopt),
                                   .maxHeightDips = menu.Read("max-height", readCap, std::nullopt),
                                   .item          = ReadItem(sheet, StyleTarget::MenuItem, menuElement, "", menuColor),
                                   .active        = ReadItem(sheet, StyleTarget::MenuItem, menuElement, "active", menuColor),
                                   .scrollbar     = {.widthDips  = scrollbar.Read("width", ReadPositiveLength, 0.0f),
                                                     .background = scrollbar.Color("background", kTransparent),
                                                     .radiusDips = scrollbar.Length("border-radius", 0.0f),
                                                     .margin     = scrollbar.Space("margin", kNoSpace)}}};
}

ModuleStyle ResolveModuleStyle(const StyleSheet& sheet, const StyleConfig& bar, ModuleKind kind, std::string_view name, const std::vector<std::string>& classes) {
    const std::string_view type = TypeName(kind);
    const Element          normal{.type = type, .id = name, .classes = {"module"}, .hover = false};
    const Element          hovered{.type = type, .id = name, .classes = {"module"}, .hover = true};
    const Element          alerted{.type = type, .id = name, .classes = {"module", "alert"}, .hover = false};
    const Cascaded         box(sheet, StyleTarget::Module, &normal, nullptr);
    const BoxStyle         boxStyle = ReadBox(box);
    const D2D1_COLOR_F     color    = box.Color("color", bar.color);
    const Element          icon{.type = {}, .id = {}, .classes = {"icon"}, .hover = false};
    const Element          expanded{.type = type, .id = name, .classes = {"module", "expanded"}, .hover = false};
    const Cascaded         iconRules(sheet, StyleTarget::Icon, &normal, &icon);
    const Cascaded         expandedIconRules(sheet, StyleTarget::Icon, &expanded, &icon);
    if (kind != ModuleKind::Tray && kind != ModuleKind::Group) {
        for (const auto& [rules, property] : {std::pair{&iconRules, "transform"}, std::pair{&iconRules, "transition"}, std::pair{&expandedIconRules, "transform"}}) {
            if (const Declaration* declaration = rules->Find(property)) {
                throw StyleError(sheet.source, declaration->line, std::format("{} animates a tray or group as it unfolds, and {} is a {} module", property, name, type));
            }
        }
        const Declaration* transition = box.Find("transition");
        if (transition != nullptr && std::ranges::any_of(ReadTransitionValues(transition->value), [](const TransitionValue& value) { return value.property == "width"; })) {
            throw StyleError(sheet.source, transition->line, std::format("transition: width animates a tray or group as it unfolds, and {} is a {} module", name, type));
        }
    }
    RequireOwnStates(sheet, kind, type, name);
    const float              rotation = iconRules.Read("transform", ReadRotation, 0.0f);
    std::vector<ClassColors> classColors;
    classColors.reserve(classes.size());
    for (const std::string& declared : classes) {
        classColors.push_back(ClassColors{.name = declared, .colors = ReadStateColors(sheet, type, name, declared, color)});
    }
    return ModuleStyle{.box                         = boxStyle,
                       .margin                      = box.Space("margin", kNoSpace),
                       .hover                       = Cascaded(sheet, StyleTarget::Module, &hovered, nullptr).Color("background", boxStyle.background),
                       .backgroundTransition        = box.Read("transition", TransitionReader("background"), kNoTransition),
                       .color                       = color,
                       .alertColor                  = Cascaded(sheet, StyleTarget::Module, &alerted, nullptr).Color("color", color),
                       .warning                     = ReadStateColors(sheet, type, name, "warning", color),
                       .critical                    = ReadStateColors(sheet, type, name, "critical", color),
                       .muted                       = ReadStateColors(sheet, type, name, "muted", color),
                       .classes                     = std::move(classColors),
                       .icon                        = {.font            = iconRules.Read("font-family", ReadFamily, bar.font),
                                                       .sizeDips        = iconRules.Read("font-size", ReadPositiveLength, bar.fontSizeDips),
                                                       .color           = iconRules.Color("color", color),
                                                       .gapDips         = iconRules.Length("margin-right", 0.0f),
                                                       .rotationDegrees = rotation,
                                                       .transition      = iconRules.Read("transition", TransitionReader("transform"), kNoTransition)},
                       .item                        = ReadItem(sheet, StyleTarget::Item, normal, "", color),
                       .focused                     = ReadItem(sheet, StyleTarget::Item, normal, "focused", color),
                       .occupied                    = ReadItem(sheet, StyleTarget::Item, normal, "occupied", color),
                       .empty                       = ReadItem(sheet, StyleTarget::Item, normal, "empty", color),
                       .toggle                      = ReadItem(sheet, StyleTarget::Item, normal, "toggle", color),
                       .transition                  = box.Read("transition", TransitionReader("width"), kNoTransition),
                       .expandedIconRotationDegrees = expandedIconRules.Read("transform", ReadRotation, rotation)};
}

void RequireKnownIds(const StyleSheet& sheet, const std::vector<std::string>& names) {
    for (const StyleRule& rule : sheet.rules) {
        for (const Compound& compound : rule.selector.compounds) {
            if (!compound.id.empty() && std::ranges::find(names, compound.id) == names.end()) {
                throw StyleError(sheet.source, rule.line,
                                 std::format("#{} names no module: the config's modules table defines {}", compound.id, names.empty() ? std::string("none") : JoinNames(names)));
            }
        }
    }
}

bool IsIdentCharacter(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '_' || character == '-';
}

bool IsStyledClass(std::string_view name) {
    return std::ranges::find(kClasses, name) != kClasses.end();
}

// Only a rule's first part holds a module's classes: a later part's are .icon or .item ones, which parsing checked.
void RequireDeclaredClasses(const StyleSheet& sheet, const std::vector<DeclaredClasses>& declared) {
    for (const StyleRule& rule : sheet.rules) {
        const Compound& first = rule.selector.compounds.front();
        for (const std::string& name : first.classes) {
            if (IsStyledClass(name)) {
                continue;
            }
            const bool matchesADeclarer = std::ranges::any_of(declared, [&first, &name](const DeclaredClasses& module) {
                const Element element{.type = TypeName(ModuleKind::Custom), .id = module.module, .classes = {"module", name}, .hover = false};
                return std::ranges::find(module.classes, name) != module.classes.end() && Matches(first, element);
            });
            if (!matchesADeclarer) {
                throw StyleError(sheet.source, rule.line,
                                 std::format("'.{}' is not a class intarsia styles, known classes are: .module, .alert, .warning, .critical, .muted, .expanded, .icon, .item, "
                                             ".focused, .occupied, .empty, .toggle, or one a custom module this rule matches lists in its classes",
                                             name));
            }
        }
    }
}
