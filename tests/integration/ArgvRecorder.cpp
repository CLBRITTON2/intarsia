// The program integration tests name in a config's actions and custom commands. It appends each argument after the
// first to the file the first names, one UTF-8 line each, and prints them joined by spaces to stdout when it has one.
// Built for the GUI subsystem, so an action the bar starts through the shell opens no console window.
//
//   argv_recorder.exe <record file> [argument...]

#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "platform/Text.hpp"

#include <windows.h>

#include <shellapi.h>

#include <cstdint>
#include <string>

namespace {

    bool WriteAll(HANDLE file, const std::string& text) {
        DWORD written = 0;
        return WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != FALSE && written == text.size();
    }

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { // NOLINT(misc-use-internal-linkage)
    std::int32_t      count = 0;
    LPWSTR*           argv  = CommandLineToArgvW(GetCommandLineW(), &count);
    const UniqueLocal owned(static_cast<HLOCAL>(argv));
    if (argv == nullptr || count < 2) {
        return 2;
    }
    std::string lines;
    std::string joined;
    for (std::int32_t index = 2; index < count; ++index) {
        std::string argument;
        try {
            argument = ToUtf8(argv[index]);
        } catch (const Win32Error&) { return 6; }
        lines += argument + "\n";
        if (index > 2) {
            joined += " ";
        }
        joined += argument;
    }
    HANDLE record = CreateFileW(argv[1], FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
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
