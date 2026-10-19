#pragma once

#include <windows.h>

#include <stdexcept>
#include <string_view>

// A failed Win32, COM or DirectX call: the message names the call and carries the HRESULT and its text.
class Win32Error : public std::runtime_error {
  public:
    Win32Error(std::string_view call, HRESULT result);

    HRESULT Result() const;

  private:
    HRESULT _result;
};

// The graphics device is gone (driver update, driver crash, GPU removed) and nothing made from it works again. Only
// App can recover, by creating a new device and everything drawn with it.
class DeviceLostError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// A monitor went while it was being read or waited on. Recovered as a lost device, since that recovery rebuilds every
// bar from the monitors present and a monitor's adapter can go with it.
class MonitorGoneError : public DeviceLostError {
  public:
    using DeviceLostError::DeviceLostError;
};

void ThrowIfFailed(HRESULT result, std::string_view call);
[[noreturn]] void ThrowLastError(std::string_view call);

// For the calls that return a Win32 error code rather than set the last error.
void ThrowIfWin32Failed(DWORD result, std::string_view call);
