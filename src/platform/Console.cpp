#include "platform/Console.hpp"

#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "platform/Text.hpp"

#include <windows.h>

#include <string>
#include <string_view>

namespace {

    void WriteToFile(HANDLE output, std::string_view utf8) {
        while (!utf8.empty()) {
            DWORD written = 0;
            if (!WriteFile(output, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr)) {
                ThrowLastError("WriteFile(stdout)");
            }
            utf8.remove_prefix(written);
        }
    }

    // UTF-16 through WriteConsoleW, since the console's code page need not be UTF-8.
    void WriteToConsole(HANDLE console, std::string_view utf8) {
        const std::wstring wide = ToWide(utf8);
        std::wstring_view  text = wide;
        while (!text.empty()) {
            DWORD written = 0;
            if (!WriteConsoleW(console, text.data(), static_cast<DWORD>(text.size()), &written, nullptr)) {
                ThrowLastError("WriteConsoleW");
            }
            text.remove_prefix(written);
        }
    }

} // namespace

void WriteToCaller(std::string_view utf8) {
    HANDLE redirected = GetStdHandle(STD_OUTPUT_HANDLE);
    if (redirected != nullptr && redirected != INVALID_HANDLE_VALUE && GetFileType(redirected) != FILE_TYPE_UNKNOWN) {
        if (GetFileType(redirected) == FILE_TYPE_CHAR) {
            WriteToConsole(redirected, utf8);
            return;
        }
        WriteToFile(redirected, utf8);
        return;
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        ThrowLastError("AttachConsole(the starting process's console, which --monitors prints to)");
    }
    const UniqueHandle console = OwnHandle(CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr), "CreateFileW(CONOUT$)");
    WriteToConsole(console.get(), utf8);
}
