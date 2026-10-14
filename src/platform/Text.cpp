#include "platform/Text.hpp"

#include "platform/Errors.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <format>

namespace {

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

    // The code point of a valid UTF-8 sequence, already checked by Utf8SequenceBytes.
    char32_t DecodeUtf8(std::string_view sequence) {
        const auto lead = static_cast<std::uint8_t>(sequence[0]);
        char32_t   code = sequence.size() == 4 ? lead & 0x07u : sequence.size() == 3 ? lead & 0x0Fu : lead & 0x1Fu;
        for (size_t index = 1; index < sequence.size(); ++index) {
            code = (code << 6) | (static_cast<std::uint8_t>(sequence[index]) & 0x3Fu);
        }
        return code;
    }

    // The code points IsUnsafeCharacter describes.
    bool IsUnsafeCodePoint(char32_t code) {
        return code < 0x20 || (code >= 0x7F && code <= 0x9F) || code == 0x061C || code == 0x200E || code == 0x200F || code == 0x2028 || code == 0x2029 ||
            (code >= 0x202A && code <= 0x202E) || (code >= 0x2066 && code <= 0x2069) || code == 0xFEFF;
    }

    std::wstring Widen(std::string_view text, std::uint32_t flags, std::string_view call) {
        if (text.empty()) {
            return {};
        }
        const std::int32_t length = MultiByteToWideChar(CP_UTF8, flags, text.data(), static_cast<std::int32_t>(text.size()), nullptr, 0);
        if (length == 0) {
            ThrowLastError(call);
        }
        std::wstring wide(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, flags, text.data(), static_cast<std::int32_t>(text.size()), wide.data(), length);
        return wide;
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
    return Widen(text, MB_ERR_INVALID_CHARS, "MultiByteToWideChar");
}

bool SameFileName(std::wstring_view left, std::wstring_view right) {
    return CompareStringOrdinal(left.data(), static_cast<std::int32_t>(left.size()), right.data(), static_cast<std::int32_t>(right.size()), TRUE) == CSTR_EQUAL;
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

bool IsControlByte(char character) {
    const auto byte = static_cast<std::uint8_t>(character);
    return byte < 0x20u || byte == 0x7Fu;
}

bool IsUnsafeCharacter(wchar_t unit) {
    return IsUnsafeCodePoint(static_cast<char32_t>(unit));
}

bool IsValidUtf8(std::string_view bytes) {
    return bytes.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<std::int32_t>(bytes.size()), nullptr, 0) > 0;
}

std::string_view CharacterAt(std::string_view utf8, size_t at) {
    const size_t bytes = Utf8SequenceBytes(utf8, at);
    return utf8.substr(at, bytes == 0 ? 1 : bytes);
}

std::wstring DrawableText(std::string_view bytes) {
    // Without MB_ERR_INVALID_CHARS each invalid sequence decodes to U+FFFD instead of failing.
    return DrawableText(std::wstring_view(Widen(bytes, 0, "MultiByteToWideChar(drawable)")));
}

std::wstring DrawableText(std::wstring_view text) {
    std::wstring drawable(text);
    std::ranges::replace_if(drawable, IsUnsafeCharacter, kReplacementCharacter);
    return ReplaceLoneSurrogates(drawable);
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
                continue;
            }
            const std::string_view sequence = shown.substr(at, bytes);
            const char32_t         code     = DecodeUtf8(sequence);
            if (IsUnsafeCodePoint(code)) {
                excerpt += std::format("\\u{:04x}", static_cast<std::uint32_t>(code));
            } else {
                excerpt += sequence;
            }
            at += bytes;
            continue;
        }
        if (character == '"' || character == '\\') {
            excerpt += '\\';
            excerpt += character;
        } else if (IsControlByte(character)) {
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
