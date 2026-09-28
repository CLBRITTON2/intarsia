#include "platform/Text.hpp"

#include "platform/Errors.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <format>

namespace {

    constexpr wchar_t kReplacementCharacter = 0xFFFD;

    // Bytes of the valid UTF-8 sequence that starts at text[at], or 0 when none does.
    size_t Utf8SequenceBytes(std::string_view text, size_t at) {
        const auto   lead  = static_cast<std::uint8_t>(text[at]);
        const size_t bytes = lead >= 0xF0u ? 4 : lead >= 0xE0u ? 3 : lead >= 0xC0u ? 2 : 0;
        if (bytes == 0 || at + bytes > text.size()) {
            return 0;
        }
        // Also rejects overlong forms, encoded surrogates and code points past U+10FFFF.
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data() + at, static_cast<std::int32_t>(bytes), nullptr, 0) > 0 ? bytes : 0;
    }

} // namespace

std::string ToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const std::int32_t length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<std::int32_t>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length == 0) {
        ThrowLastError("WideCharToMultiByte");
    }
    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<std::int32_t>(text.size()), utf8.data(), length, nullptr, nullptr);
    return utf8;
}

std::wstring ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const std::int32_t length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<std::int32_t>(text.size()), nullptr, 0);
    if (length == 0) {
        ThrowLastError("MultiByteToWideChar");
    }
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<std::int32_t>(text.size()), wide.data(), length);
    return wide;
}

std::wstring ReplaceLoneSurrogates(std::wstring_view text) {
    std::wstring valid(text);
    for (size_t index = 0; index < valid.size(); ++index) {
        if (IS_HIGH_SURROGATE(valid[index]) && index + 1 < valid.size() && IS_LOW_SURROGATE(valid[index + 1])) {
            ++index;
        } else if (IS_HIGH_SURROGATE(valid[index]) || IS_LOW_SURROGATE(valid[index])) {
            valid[index] = kReplacementCharacter;
        }
    }
    return valid;
}

bool IsUnsafeCharacter(wchar_t unit) {
    return unit < 0x20 || (unit >= 0x7F && unit <= 0x9F) || (unit >= 0x202A && unit <= 0x202E) || (unit >= 0x2066 && unit <= 0x2069);
}

std::wstring DrawableText(std::string_view bytes) {
    if (bytes.empty()) {
        return {};
    }
    // Without MB_ERR_INVALID_CHARS each invalid sequence decodes to U+FFFD instead of failing.
    const std::int32_t length = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<std::int32_t>(bytes.size()), nullptr, 0);
    if (length == 0) {
        ThrowLastError("MultiByteToWideChar(drawable)");
    }
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<std::int32_t>(bytes.size()), wide.data(), length);
    std::ranges::replace_if(wide, IsUnsafeCharacter, kReplacementCharacter);
    return ReplaceLoneSurrogates(wide);
}

std::string LogExcerpt(std::string_view text, size_t maxBytes) {
    size_t kept = text.size() <= maxBytes ? text.size() : maxBytes;
    // Back off over UTF-8 continuation bytes (10xxxxxx) so the cut lands on a character boundary.
    while (kept < text.size() && kept > 0 && (static_cast<std::uint8_t>(text[kept]) & 0xC0u) == 0x80u) {
        --kept;
    }

    const std::string_view shown = text.substr(0, kept);
    std::string            excerpt;
    excerpt.reserve(kept + 16);
    size_t at = 0;
    while (at < shown.size()) {
        const char character = shown[at];
        const auto byte      = static_cast<std::uint8_t>(character);
        if (byte >= 0x80u) {
            const size_t bytes = Utf8SequenceBytes(shown, at);
            if (bytes == 0) {
                excerpt += std::format("\\x{:02x}", byte);
                ++at;
            } else {
                excerpt += shown.substr(at, bytes);
                at += bytes;
            }
            continue;
        }
        if (character == '"' || character == '\\') {
            excerpt += '\\';
            excerpt += character;
        } else if (byte < 0x20u || byte == 0x7Fu) {
            excerpt += std::format("\\x{:02x}", byte);
        } else {
            excerpt += character;
        }
        ++at;
    }
    if (kept < text.size()) {
        excerpt += std::format("...({} more bytes)", text.size() - kept);
    }
    return excerpt;
}
