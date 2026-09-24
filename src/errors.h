#pragma once

#include <windows.h>

#include <stdexcept>
#include <string_view>

class Win32Error : public std::runtime_error {
  public:
    Win32Error(std::string_view call, HRESULT result);
    HRESULT Result() const;

  private:
    HRESULT result_;
};

void              ThrowIfFailed(HRESULT result, std::string_view call);
[[noreturn]] void ThrowLastError(std::string_view call);
