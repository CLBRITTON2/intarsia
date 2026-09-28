#pragma once

#include <windows.h>

#include <stdexcept>
#include <string_view>

// A failed Win32, COM or DirectX call: the message names the call and carries the HRESULT and its text.
class Win32Error : public std::runtime_error {
  public:
    Win32Error(std::string_view call, HRESULT result);
};

void              ThrowIfFailed(HRESULT result, std::string_view call);
[[noreturn]] void ThrowLastError(std::string_view call);

// For the calls that return a Win32 error code rather than set the last error.
void ThrowIfWin32Failed(DWORD result, std::string_view call);
