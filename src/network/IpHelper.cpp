#include "network/IpHelper.hpp"

#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Text.hpp"

// winsock2.h must come before iphlpapi.h, which leaves out every declaration that takes a socket address without it.
#include <winsock2.h>
#include <ws2tcpip.h>

#include <iphlpapi.h>
#include <netioapi.h>
#include <wlanapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

    constexpr DWORD kSampleMs = 1000;

    using Clock = std::chrono::steady_clock;

    // One interface's byte counters at one moment, for the next sample's rates.
    struct Counters {
        NET_IFINDEX       index;
        std::uint64_t     received;
        std::uint64_t     sent;
        Clock::time_point at;
    };

    // The interface a packet to a public address would leave through: IPv4 first, then IPv6 for a network with no IPv4
    // route. A route lookup only, nothing is sent. Nullopt when neither has a route.
    std::optional<NET_IFINDEX> DefaultRouteInterface() {
        DWORD       index = 0;
        sockaddr_in v4{};
        v4.sin_family           = AF_INET;
        v4.sin_addr.S_un.S_addr = htonl(0x01010101); // 1.1.1.1
        if (GetBestInterfaceEx(reinterpret_cast<sockaddr*>(&v4), &index) == NO_ERROR) {
            return index;
        }
        sockaddr_in6 v6{};
        v6.sin6_family = AF_INET6;
        // 2606:4700:4700::1111
        constexpr std::array<std::uint8_t, 16> kAddress{0x26, 0x06, 0x47, 0x00, 0x47, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0x11, 0x11};
        std::ranges::copy(kAddress, std::begin(v6.sin6_addr.u.Byte));
        if (GetBestInterfaceEx(reinterpret_cast<sockaddr*>(&v6), &index) == NO_ERROR) {
            return index;
        }
        return std::nullopt;
    }

    // The interface's first usable IPv4 address, or empty when it has none.
    std::wstring FirstIpv4(NET_IFINDEX index) {
        PMIB_UNICASTIPADDRESS_TABLE raw = nullptr;
        ThrowIfWin32Failed(GetUnicastIpAddressTable(AF_INET, &raw), "GetUnicastIpAddressTable");
        const std::unique_ptr<MIB_UNICASTIPADDRESS_TABLE, decltype(&FreeMibTable)> table(raw, &FreeMibTable);
        for (ULONG row = 0; row < table->NumEntries; ++row) {
            const MIB_UNICASTIPADDRESS_ROW& address = table->Table[row];
            if (address.InterfaceIndex != index || address.DadState != IpDadStatePreferred) {
                continue;
            }
            std::array<wchar_t, INET_ADDRSTRLEN> text{};
            if (InetNtopW(AF_INET, &address.Address.Ipv4.sin_addr, text.data(), text.size()) == nullptr) {
                throw Win32Error("InetNtopW", HRESULT_FROM_WIN32(WSAGetLastError()));
            }
            return std::wstring(text.data());
        }
        return {};
    }

    // A WLAN API client, open for as long as it lives.
    class WlanClient {
      public:
        WlanClient() {
            DWORD negotiated = 0;
            ThrowIfWin32Failed(WlanOpenHandle(2, nullptr, &negotiated, &_handle), "WlanOpenHandle");
        }
        ~WlanClient() {
            WlanCloseHandle(_handle, nullptr);
        }
        WlanClient(const WlanClient&)            = delete;
        WlanClient& operator=(const WlanClient&) = delete;

        // The network the interface is connected to. Throws Win32Error, with ERROR_ACCESS_DENIED when Windows keeps the
        // network's name from apps without location access.
        std::pair<std::wstring, std::int32_t> Connection(const GUID& interfaceGuid) const {
            PWLAN_CONNECTION_ATTRIBUTES attributes = nullptr;
            DWORD                       size       = 0;
            ThrowIfWin32Failed(WlanQueryInterface(_handle, &interfaceGuid, wlan_intf_opcode_current_connection, nullptr, &size, reinterpret_cast<void**>(&attributes), nullptr),
                               "WlanQueryInterface(current_connection)");
            const std::unique_ptr<WLAN_CONNECTION_ATTRIBUTES, decltype(&WlanFreeMemory)> owned(attributes, &WlanFreeMemory);
            const DOT11_SSID&                                                            ssid = owned->wlanAssociationAttributes.dot11Ssid;
            // Anyone nearby can name a network, so its name is drawn only once it is safe to.
            std::wstring name = DrawableText(std::string_view(reinterpret_cast<const char*>(ssid.ucSSID), std::min<ULONG>(ssid.uSSIDLength, DOT11_SSID_MAX_LENGTH)));
            return {std::move(name), static_cast<std::int32_t>(owned->wlanAssociationAttributes.wlanSignalQuality)};
        }

      private:
        HANDLE _handle = nullptr;
    };

} // namespace

IpHelper::IpHelper(ChangeSignal& changes) :
    _changes(changes), _stopEvent(OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(network stop)")),
    _state{.link = NetworkLink::None, .interfaceName = {}, .ssid = {}, .signalPercent = std::nullopt, .ipv4 = {}, .receivedBytesPerSecond = 0.0, .sentBytesPerSecond = 0.0},
    _thread([this] { Run(); }) {}

IpHelper::~IpHelper() {
    SetEvent(_stopEvent.get());
    _thread.join();
}

NetworkState IpHelper::State() const {
    const std::scoped_lock lock(_mutex);
    return _state;
}

// Nothing here may throw out of the thread, so every failure is logged once and the next sample tries again.
void IpHelper::Run() {
    std::optional<Counters>     last;
    std::unique_ptr<WlanClient> wlan;
    FailureLog                  sampleLog("network");
    FailureLog                  wlanLog("network");
    do {
        NetworkState state{
            .link = NetworkLink::None, .interfaceName = {}, .ssid = {}, .signalPercent = std::nullopt, .ipv4 = {}, .receivedBytesPerSecond = 0.0, .sentBytesPerSecond = 0.0};
        try {
            const std::optional<NET_IFINDEX> index = DefaultRouteInterface();
            if (index.has_value()) {
                MIB_IF_ROW2 row{};
                row.InterfaceIndex = *index;
                ThrowIfWin32Failed(GetIfEntry2(&row), "GetIfEntry2");
                const Counters now{.index = *index, .received = row.InOctets, .sent = row.OutOctets, .at = Clock::now()};
                if (last.has_value() && last->index == now.index) {
                    const double seconds         = std::chrono::duration<double>(now.at - last->at).count();
                    state.receivedBytesPerSecond = BytesPerSecond(last->received, now.received, seconds);
                    state.sentBytesPerSecond     = BytesPerSecond(last->sent, now.sent, seconds);
                }
                last                = now;
                state.link          = row.Type == IF_TYPE_IEEE80211 ? NetworkLink::Wifi : NetworkLink::Ethernet;
                state.interfaceName = std::wstring(row.Alias);
                state.ipv4          = FirstIpv4(*index);
                if (state.link == NetworkLink::Wifi) {
                    try {
                        if (wlan == nullptr) {
                            wlan = std::make_unique<WlanClient>();
                        }
                        auto [ssid, signal] = wlan->Connection(row.InterfaceGuid);
                        state.ssid          = std::move(ssid);
                        state.signalPercent = signal;
                        wlanLog.Succeeded();
                    } catch (const Win32Error& error) {
                        // The link is still Wi-Fi, only its details are missing. Windows 11 withholds them from apps
                        // without location access (ERROR_ACCESS_DENIED), which the log's reason shows.
                        wlan.reset();
                        wlanLog.Failed("wifi_details_unavailable", error);
                    }
                }
            } else {
                last.reset();
            }
            sampleLog.Succeeded();
        } catch (const Win32Error& error) {
            last.reset();
            state = NetworkState{
                .link = NetworkLink::None, .interfaceName = {}, .ssid = {}, .signalPercent = std::nullopt, .ipv4 = {}, .receivedBytesPerSecond = 0.0, .sentBytesPerSecond = 0.0};
            sampleLog.Failed("sample_failed", error);
        }
        bool changed = false;
        {
            const std::scoped_lock lock(_mutex);
            changed = !SameLink(state, _state);
            _state  = std::move(state);
        }
        if (changed) {
            _changes.Notify("network");
        }
    } while (WaitForSingleObject(_stopEvent.get(), kSampleMs) == WAIT_TIMEOUT);
    if (WaitForSingleObject(_stopEvent.get(), 0) != WAIT_OBJECT_0) {
        Log(LogLevel::Error, "network", "wait_failed", {{.key = "error", .value = std::to_string(GetLastError())}});
    }
}

bool SameLink(const NetworkState& first, const NetworkState& second) {
    return first.link == second.link && first.interfaceName == second.interfaceName && first.ssid == second.ssid && first.signalPercent == second.signalPercent &&
        first.ipv4 == second.ipv4;
}

double BytesPerSecond(std::uint64_t before, std::uint64_t after, double seconds) {
    if (after < before || seconds <= 0.0) {
        return 0.0;
    }
    return static_cast<double>(after - before) / seconds;
}
