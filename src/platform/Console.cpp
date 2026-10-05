#include "platform/Console.hpp"

#include "platform/Errors.hpp"
#include "platform/Text.hpp"

#include <windows.h>

#include <string>
#include <string_view>

namespace {

    bool IsConsole(HANDLE output) {
        DWORD mode = 0;
        return GetConsoleMode(output, &mode) != 0;
    }

    void WriteBytes(HANDLE output, std::string_view utf8) {
        while (!utf8.empty()) {
            DWORD written = 0;
            if (!WriteFile(output, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr)) {
                ThrowLastError("WriteFile");
            }
            utf8.remove_prefix(written);
        }
    }

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

void WriteUtf8(HANDLE output, std::string_view utf8) {
    if (IsConsole(output)) {
        WriteToConsole(output, utf8);
        return;
    }
    WriteBytes(output, utf8);
}
