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
