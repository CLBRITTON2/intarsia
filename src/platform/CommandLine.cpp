#include "platform/CommandLine.hpp"

#include <windows.h>

#include <array>
#include <filesystem>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

    bool NeedsQuotes(std::wstring_view argument) {
        return argument.empty() || argument.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
    }

    // The CommandLineToArgvW rules: backslashes are literal unless they precede a quote, where each one needs doubling
    // and the quote itself needs a backslash.
    std::wstring QuoteArgument(std::wstring_view argument) {
        if (!NeedsQuotes(argument)) {
            return std::wstring(argument);
        }
        std::wstring quoted      = L"\"";
        size_t       backslashes = 0;
        for (const wchar_t character : argument) {
            if (character == L'\\') {
                ++backslashes;
                continue;
            }
            if (character == L'"') {
                quoted.append((backslashes * 2) + 1, L'\\');
            } else {
                quoted.append(backslashes, L'\\');
            }
            backslashes = 0;
            quoted += character;
        }
        quoted.append(backslashes * 2, L'\\');
        quoted += L'"';
        return quoted;
    }

    std::wstring PathVariable() {
        const DWORD length = GetEnvironmentVariableW(L"PATH", nullptr, 0);
        if (length == 0) {
            return {};
        }
        std::wstring value(length, L'\0');
        const DWORD  copied = GetEnvironmentVariableW(L"PATH", value.data(), length);
        value.resize(copied);
        return value;
    }

    // PATH's absolute entries, joined for SearchPathW, which resolves a relative one (".", an empty one, "bin")
    // against the current directory.
    std::wstring AbsoluteEntries(std::wstring_view path) {
        std::wstring kept;
        for (const auto entry : std::views::split(path, L';')) {
            const std::wstring_view text(entry.begin(), entry.end());
            if (!std::filesystem::path(text).is_absolute()) {
                continue;
            }
            if (!kept.empty()) {
                kept += L';';
            }
            kept += text;
        }
        return kept;
    }

} // namespace

std::optional<std::filesystem::path> ResolveProgram(std::wstring_view program) {
    std::filesystem::path path(program);
    if (path.is_absolute()) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            return std::nullopt;
        }
        return path;
    }
    // SearchPathW resolves a name with any directory part against the current directory, whatever path it is given.
    if (path.has_parent_path()) {
        return std::nullopt;
    }
    // An explicit search path keeps SearchPathW from also looking in the current directory.
    const std::wstring searchPath = AbsoluteEntries(PathVariable());
    if (searchPath.empty()) {
        return std::nullopt;
    }
    std::array<wchar_t, 32768> found{};
    const DWORD                length = SearchPathW(searchPath.c_str(), std::wstring(program).c_str(), L".exe", static_cast<DWORD>(found.size()), found.data(), nullptr);
    if (length == 0 || length >= found.size()) {
        return std::nullopt;
    }
    return std::filesystem::path(std::wstring(found.data(), length));
}

std::wstring JoinArguments(const std::vector<std::wstring>& arguments) {
    std::wstring line;
    for (const std::wstring& argument : arguments) {
        if (!line.empty()) {
            line += L' ';
        }
        line += QuoteArgument(argument);
    }
    return line;
}
