#include "platform/Text.hpp"

#include "platform/Errors.hpp"

#include <windows.h>

#include <format>

std::string ToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length == 0) {
        ThrowLastError("WideCharToMultiByte");
    }
    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), utf8.data(), length, nullptr, nullptr);
    return utf8;
}

std::wstring ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length == 0) {
        ThrowLastError("MultiByteToWideChar");
    }
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

std::string LogExcerpt(std::string_view text, size_t maxBytes) {
    size_t kept = text.size() <= maxBytes ? text.size() : maxBytes;
    // Back off over UTF-8 continuation bytes (10xxxxxx) so the cut lands on a character boundary.
    while (kept < text.size() && kept > 0 && (static_cast<unsigned char>(text[kept]) & 0xC0u) == 0x80u) {
        --kept;
    }

    std::string excerpt;
    excerpt.reserve(kept + 16);
    for (const char character : text.substr(0, kept)) {
        const auto byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            excerpt += '\\';
            excerpt += character;
        } else if (byte < 0x20u || byte == 0x7Fu) {
            excerpt += std::format("\\x{:02x}", byte);
        } else {
            excerpt += character;
        }
    }
    if (kept < text.size()) {
        excerpt += std::format("...({} more bytes)", text.size() - kept);
    }
    return excerpt;
}
