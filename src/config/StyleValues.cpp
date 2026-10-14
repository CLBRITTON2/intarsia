#include "config/StyleValues.hpp"

#include "platform/Text.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

    constexpr std::string_view kSpace = " \t\r";

    // Splits on any of separators, trimming each part and dropping empty ones.
    std::vector<std::string_view> Split(std::string_view text, std::string_view separators) {
        std::vector<std::string_view> parts;
        size_t                        start = 0;
        while (start <= text.size()) {
            const size_t end = text.find_first_of(separators, start);
            // substr clamps its count, so with end at npos the last part runs to the end of the text.
            const std::string_view part = Trim(text.substr(start, end - start));
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
            if (!joined.empty()) {
                joined += ' ';
            }
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

    // Far past any monitor, and small enough that every length scaled to device pixels fits the integers it is cast to.
    constexpr float kLargestNumber = 10000.0f;

    std::uint32_t   HexDigit(char digit) {
        std::uint32_t value = 0;
        if (std::from_chars(&digit, &digit + 1, value, 16).ec != std::errc()) {
            throw std::invalid_argument(std::format("has '{}', which is not a hex digit", digit));
        }
        return value;
    }

    constexpr std::string_view kColorForm = "must be a color: #rrggbb, #rrggbbaa, #rgb or #rgba, optionally followed by an opacity as #191724 90%, or transparent";

    // #rgb and #rgba write each channel's digit once, where #rrggbb and #rrggbbaa write two.
    std::string LongHexDigits(std::string_view digits) {
        if (digits.size() > 4) {
            return std::string(digits);
        }
        std::string doubled;
        for (const char digit : digits) {
            doubled.append(2, digit);
        }
        return doubled;
    }

    D2D1_COLOR_F ParseHex(std::string_view text) {
        const std::string_view digits = text.substr(1);
        if (digits.size() != 3 && digits.size() != 4 && digits.size() != 6 && digits.size() != 8) {
            throw std::invalid_argument(std::string(kColorForm));
        }
        const std::string    pairs = LongHexDigits(digits);
        std::array<float, 4> channels{0.0f, 0.0f, 0.0f, 1.0f};
        for (size_t channel = 0; channel < pairs.size() / 2; ++channel) {
            const std::uint32_t value = (HexDigit(pairs[channel * 2]) * 16) + HexDigit(pairs[(channel * 2) + 1]);
            channels.at(channel)      = static_cast<float>(value) / 255.0f;
        }
        return D2D1_COLOR_F{
            .r = channels[0],
            .g = channels[1],
            .b = channels[2],
            .a = channels[3],
        };
    }

    // The number before a trailing %, or none when text is not a number followed by %.
    std::optional<float> ParsePercent(std::string_view text) {
        if (!text.ends_with('%')) {
            return std::nullopt;
        }
        return ParseNumber(text.substr(0, text.size() - 1));
    }

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

    // The duration in milliseconds, or none when text is not a number followed by ms or s.
    std::optional<float> DurationMilliseconds(std::string_view text) {
        if (text.ends_with("ms")) {
            return ParseNumber(text.substr(0, text.size() - 2));
        }
        if (!text.ends_with('s')) {
            return std::nullopt;
        }
        const std::optional<float> seconds = ParseNumber(text.substr(0, text.size() - 1));
        if (!seconds.has_value()) {
            return std::nullopt;
        }
        return *seconds * 1000.0f;
    }

    std::chrono::milliseconds ParseDuration(std::string_view text) {
        const std::optional<float> milliseconds = DurationMilliseconds(text);
        if (!milliseconds.has_value() || *milliseconds < 0.0f || *milliseconds > 60000.0f) {
            throw std::invalid_argument("takes a duration in ms or s from 0 to 60s, as 400ms or 0.4s");
        }
        return std::chrono::milliseconds(static_cast<std::int64_t>(*milliseconds));
    }

    constexpr std::string_view kTransitionForm =
        "must be none, or transitions separated by commas, each a property, a duration and a timing function, as width 400ms ease-out, background 150ms ease";

    TransitionValue ParseTransitionValue(std::string_view text) {
        const std::vector<std::string_view> parts = Split(text, kSpace);
        if (parts.size() != 2 && parts.size() != 3) {
            throw std::invalid_argument(std::string(kTransitionForm));
        }
        TimingFunction timing = TimingFunction::Ease;
        if (parts.size() == 3) {
            const auto entry = std::ranges::find(kTimings, parts[2], &TimingName::name);
            if (entry == kTimings.end()) {
                throw std::invalid_argument(
                    std::format("has the timing function '{}', which is not one of {}", parts[2], JoinNames(kTimings | std::views::transform(&TimingName::name))));
            }
            timing = entry->timing;
        }
        return TransitionValue{.property = parts[0], .transition = Transition{.duration = ParseDuration(parts[1]), .timing = timing}};
    }

    constexpr std::string_view kShadowForm = "must be none, or shadows separated by commas, each an x and y offset, an optional blur and spread, and a color, "
                                             "as 0 1 2 #ffffff 18%";

    // The optional number at index, 0 when the shadow leaves it out.
    float OptionalNumber(const std::vector<float>& numbers, size_t index) {
        if (index < numbers.size()) {
            return numbers[index];
        }
        return 0.0f;
    }

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
        const float blur = OptionalNumber(numbers, 2);
        if (blur < 0.0f) {
            throw std::invalid_argument("has a negative blur, which must be 0 or more");
        }
        return InsetShadow{
            .xDips      = numbers[0],
            .yDips      = numbers[1],
            .blurDips   = blur,
            .spreadDips = OptionalNumber(numbers, 3),
            .color      = ParseColor(JoinFrom(words, numbers.size())),
        };
    }

    // What a property's value is checked and read as.
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

} // namespace

std::string_view Trim(std::string_view text) {
    const size_t first = text.find_first_not_of(kSpace);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(kSpace) - first + 1);
}

float ParseSignedNumber(std::string_view text) {
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
        const std::optional<float> percent = ParsePercent(words[1]);
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

std::vector<TransitionValue> ParseTransitionValues(std::string_view text) {
    return ParseCommaList<TransitionValue>(text, kTransitionForm, ParseTransitionValue);
}

Transition ParseTransition(std::string_view text, std::string_view property) {
    Transition found = kNoTransition;
    for (const TransitionValue& value : ParseTransitionValues(text)) {
        if (value.property == property) {
            found = value.transition;
        }
    }
    return found;
}

std::vector<InsetShadow> ParseShadows(std::string_view text) {
    return ParseCommaList<InsetShadow>(text, kShadowForm, ParseShadow);
}

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

void CheckValue(std::string_view key, std::string_view value) {
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
