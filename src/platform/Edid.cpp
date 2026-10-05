#include "platform/Edid.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace {

    constexpr size_t                      kBaseBlockBytes = 128;
    constexpr std::array<std::uint8_t, 8> kHeader{0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    constexpr size_t                      kFirstDescriptor  = 54;
    constexpr size_t                      kDescriptorBytes  = 18;
    constexpr size_t                      kDescriptorCount  = 4;
    constexpr size_t                      kDescriptorText   = 5;
    constexpr std::uint8_t                kSerialDescriptor = 0xFF;
    constexpr std::uint8_t                kTextEnd          = 0x0A;
    constexpr size_t                      kModelLetters     = 3;
    constexpr size_t                      kModelLength      = 7;
    // The longest descriptor text, past the decimal digits of a 32-bit serial.
    constexpr size_t kMaxSerialLength = 13;

    bool             IsPrintable(char character) {
        return character >= 0x20 && character <= 0x7E;
    }

    // A descriptor's text runs to its line feed, padded with spaces. Text holding anything but printable ASCII counts as
    // none, so nothing a monitor sends reaches a log or a config unescaped.
    std::optional<std::string> DescriptorText(std::span<const std::uint8_t> descriptor) {
        std::string text;
        for (const std::uint8_t byte : descriptor.subspan(kDescriptorText)) {
            if (byte == kTextEnd) {
                break;
            }
            text.push_back(static_cast<char>(byte));
        }
        const size_t end   = text.find_last_not_of(' ');
        const size_t start = text.find_first_not_of(' ');
        if (end == std::string::npos || !std::ranges::all_of(text, IsPrintable)) {
            return std::nullopt;
        }
        return text.substr(start, end - start + 1);
    }

    // Each of the four 18-byte descriptors that starts with three zero bytes is a display descriptor tagged by its fourth.
    std::optional<std::string> TaggedText(std::span<const std::uint8_t> edid, std::uint8_t tag) {
        for (size_t index = 0; index < kDescriptorCount; ++index) {
            const std::span<const std::uint8_t> descriptor = edid.subspan(kFirstDescriptor + (index * kDescriptorBytes), kDescriptorBytes);
            if (descriptor[0] == 0 && descriptor[1] == 0 && descriptor[2] == 0 && descriptor[3] == tag) {
                return DescriptorText(descriptor);
            }
        }
        return std::nullopt;
    }

    // Three letters of five bits each, big-endian, 1 for A.
    std::string ManufacturerLetters(std::uint8_t high, std::uint8_t low) {
        const auto  packed = static_cast<std::uint16_t>((high << 8) | low);
        std::string letters;
        for (const std::uint32_t shift : {10U, 5U, 0U}) {
            const std::uint32_t code = (packed >> shift) & 0x1FU;
            if (code < 1 || code > 26) {
                throw EdidError(std::format("EDID manufacturer id 0x{:04X} holds a letter code {} outside 1 to 26", packed, code));
            }
            letters.push_back(static_cast<char>('A' + code - 1));
        }
        return letters;
    }

    std::optional<std::string> Serial(std::span<const std::uint8_t> edid) {
        std::optional<std::string> text = TaggedText(edid, kSerialDescriptor);
        if (text.has_value()) {
            return text;
        }
        const std::uint32_t number = static_cast<std::uint32_t>(edid[12]) | (static_cast<std::uint32_t>(edid[13]) << 8) | (static_cast<std::uint32_t>(edid[14]) << 16) |
            (static_cast<std::uint32_t>(edid[15]) << 24);
        if (number == 0) {
            return std::nullopt;
        }
        return std::to_string(number);
    }

    bool IsUpperHex(char character) {
        return (character >= '0' && character <= '9') || (character >= 'A' && character <= 'F');
    }

} // namespace

EdidIdentity ParseEdid(std::span<const std::uint8_t> edid) {
    if (edid.size() < kBaseBlockBytes) {
        throw EdidError(std::format("EDID holds {} bytes, short of its {}-byte base block", edid.size(), kBaseBlockBytes));
    }
    if (!std::ranges::equal(edid.first(kHeader.size()), kHeader)) {
        throw EdidError("EDID does not start with its 00 FF FF FF FF FF FF 00 header");
    }
    const auto product = static_cast<std::uint16_t>(edid[10] | (edid[11] << 8));
    return EdidIdentity{.model = std::format("{}{:04X}", ManufacturerLetters(edid[8], edid[9]), product), .serial = Serial(edid)};
}

std::string MonitorId(const EdidIdentity& identity) {
    if (!identity.serial.has_value()) {
        return identity.model;
    }
    return std::format("{}-{}", identity.model, *identity.serial);
}

bool IsMonitorId(std::string_view text) {
    if (text.size() < kModelLength) {
        return false;
    }
    const std::string_view letters = text.substr(0, kModelLetters);
    const std::string_view product = text.substr(kModelLetters, kModelLength - kModelLetters);
    if (!std::ranges::all_of(letters, [](char character) { return character >= 'A' && character <= 'Z'; }) || !std::ranges::all_of(product, IsUpperHex)) {
        return false;
    }
    if (text.size() == kModelLength) {
        return true;
    }
    const std::string_view serial = text.substr(kModelLength + 1);
    return text[kModelLength] == '-' && !serial.empty() && serial.size() <= kMaxSerialLength && serial.front() != ' ' && serial.back() != ' ' &&
        std::ranges::all_of(serial, IsPrintable);
}
