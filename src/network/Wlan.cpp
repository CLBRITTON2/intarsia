#include "network/Wlan.hpp"

#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "platform/Text.hpp"

#include <wlanapi.h>

#include <algorithm>
#include <string_view>

namespace {

    // Only the notifications that change what the network state shows, since the WLAN service also reports every scan.
    void WINAPI WakeOnWlanChange(PWLAN_NOTIFICATION_DATA data, PVOID linkEvent) {
        const bool connectionChanged = data->NotificationSource == WLAN_NOTIFICATION_SOURCE_ACM &&
            (data->NotificationCode == wlan_notification_acm_connection_complete || data->NotificationCode == wlan_notification_acm_disconnected);
        const bool signalChanged = data->NotificationSource == WLAN_NOTIFICATION_SOURCE_MSM &&
            (data->NotificationCode == wlan_notification_msm_signal_quality_change || data->NotificationCode == wlan_notification_msm_roaming_end);
        if (connectionChanged || signalChanged) {
            SetEvent(linkEvent);
        }
    }

} // namespace

WlanClient::WlanClient(HANDLE linkEvent) {
    DWORD negotiated = 0;
    ThrowIfWin32Failed(WlanOpenHandle(2, nullptr, &negotiated, &_handle), "WlanOpenHandle");
    const DWORD registered = WlanRegisterNotification(_handle, WLAN_NOTIFICATION_SOURCE_ACM | WLAN_NOTIFICATION_SOURCE_MSM, TRUE, WakeOnWlanChange, linkEvent, nullptr, nullptr);
    if (registered != ERROR_SUCCESS) {
        WlanCloseHandle(_handle, nullptr);
        ThrowIfWin32Failed(registered, "WlanRegisterNotification");
    }
}

WlanClient::~WlanClient() {
    WlanCloseHandle(_handle, nullptr);
}

std::pair<std::wstring, std::int32_t> WlanClient::Connection(const GUID& interfaceGuid) const {
    PWLAN_CONNECTION_ATTRIBUTES attributes = nullptr;
    DWORD                       size       = 0;
    ThrowIfWin32Failed(WlanQueryInterface(_handle, &interfaceGuid, wlan_intf_opcode_current_connection, nullptr, &size, reinterpret_cast<void**>(&attributes), nullptr),
                       "WlanQueryInterface(current_connection)");
    const UniqueReleased<PWLAN_CONNECTION_ATTRIBUTES, &WlanFreeMemory> owned(attributes);
    const DOT11_SSID&                                                  ssid = owned->wlanAssociationAttributes.dot11Ssid;
    // Anyone nearby can name a network, so its name is drawn only once it is safe to.
    std::wstring name = DrawableText(std::string_view(reinterpret_cast<const char*>(ssid.ucSSID), std::min<ULONG>(ssid.uSSIDLength, DOT11_SSID_MAX_LENGTH)));
    return {std::move(name), static_cast<std::int32_t>(owned->wlanAssociationAttributes.wlanSignalQuality)};
}

bool WlanClientLost(HRESULT result) {
    return result == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE) || result == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE) || result == HRESULT_FROM_WIN32(RPC_S_CALL_FAILED);
}
