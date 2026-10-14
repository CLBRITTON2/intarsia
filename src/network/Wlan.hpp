#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <utility>

// A WLAN API client, open for as long as it lives, that sets linkEvent when the connection or its signal changes.
class WlanClient {
  public:
    explicit WlanClient(HANDLE linkEvent);
    // Also unregisters the notification, waiting out a callback in progress.
    ~WlanClient();
    WlanClient(const WlanClient&)            = delete;
    WlanClient& operator=(const WlanClient&) = delete;

    // The network the interface is connected to and its signal quality as a percent. Throws Win32Error, with
    // ERROR_ACCESS_DENIED when Windows keeps the network's name from apps without location access.
    std::pair<std::wstring, std::int32_t> Connection(const GUID& interfaceGuid) const;

  private:
    HANDLE _handle = nullptr;
};

// True when the failure means the client's handle is no longer good, as after the WLAN service restarts.
bool WlanClientLost(HRESULT result);
