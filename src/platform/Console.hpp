#pragma once

#include <windows.h>

#include <string_view>

// Writes UTF-8 text to a standard handle: as UTF-16 to a console, whose code page need not be UTF-8, and as the bytes
// themselves to a pipe or file. Throws Win32Error when a write fails.
void WriteUtf8(HANDLE output, std::string_view utf8);
