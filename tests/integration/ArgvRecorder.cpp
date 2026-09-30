// The program integration tests name in a config's actions and custom commands. It appends each argument after the
// first to the file the first names, one UTF-8 line each, and prints them joined by spaces to stdout when it has one.
// Built for the GUI subsystem, so an action the bar starts through the shell opens no console window.
//
//   argv_recorder.exe <record file> [argument...]

#include <windows.h>

#include <shellapi.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

    // Nullopt when the text is not valid UTF-16.
    std::optional<std::string> Utf8(std::wstring_view text) {
        if (text.empty()) {
            return std::string();
        }
        const std::int32_t size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<std::int32_t>(text.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0) {
            return std::nullopt;
        }
        std::string converted(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<std::int32_t>(text.size()), converted.data(), size, nullptr, nullptr);
        return converted;
    }

    bool WriteAll(HANDLE file, const std::string& text) {
        DWORD written = 0;
        return WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != FALSE && written == text.size();
    }

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { // NOLINT(misc-use-internal-linkage)
    std::int32_t                                        count = 0;
    const std::unique_ptr<LPWSTR, decltype(&LocalFree)> argv(CommandLineToArgvW(GetCommandLineW(), &count), &LocalFree);
    if (argv == nullptr || count < 2) {
        return 2;
    }
    std::string lines;
    std::string joined;
    for (std::int32_t index = 2; index < count; ++index) {
        const std::optional<std::string> argument = Utf8(argv.get()[index]);
        if (!argument.has_value()) {
            return 6;
        }
        lines += *argument + "\n";
        joined += (index > 2 ? " " : "") + *argument;
    }
    HANDLE record = CreateFileW(argv.get()[1], FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (record == INVALID_HANDLE_VALUE) {
        return 3;
    }
    const bool recorded = WriteAll(record, lines);
    CloseHandle(record);
    if (!recorded) {
        return 4;
    }
    // Null when the shell started it, as for an action.
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output != nullptr && output != INVALID_HANDLE_VALUE && !WriteAll(output, joined + "\n")) {
        return 5;
    }
    return 0;
}
