#include "network/IpHelper.hpp"

#include "network/Wlan.hpp"
#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "platform/Log.hpp"

// winsock2.h must come before iphlpapi.h, which leaves out every declaration that takes a socket address without it.
#include <winsock2.h>
#include <ws2tcpip.h>

#include <iphlpapi.h>
#include <netioapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
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
        const UniqueReleased<PMIB_UNICASTIPADDRESS_TABLE, &FreeMibTable> table(raw);
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

    // Runs on a system thread, so it only wakes the worker.
    void WINAPI WakeOnInterfaceChange(PVOID linkEvent, PMIB_IPINTERFACE_ROW, MIB_NOTIFICATION_TYPE) {
        SetEvent(linkEvent);
    }
    void WINAPI WakeOnAddressChange(PVOID linkEvent, PMIB_UNICASTIPADDRESS_ROW, MIB_NOTIFICATION_TYPE) {
        SetEvent(linkEvent);
    }
    void WINAPI WakeOnRouteChange(PVOID linkEvent, PMIB_IPFORWARD_ROW2, MIB_NOTIFICATION_TYPE) {
        SetEvent(linkEvent);
    }

    // The Notify*Change registrations differ only in their callback's type.
    template <typename Callback>
    UniqueMibNotification RegisterMib(DWORD(WINAPI* notify)(ADDRESS_FAMILY, Callback, PVOID, BOOLEAN, HANDLE*), ADDRESS_FAMILY family, std::type_identity_t<Callback> callback,
                                      HANDLE linkEvent, std::string_view call) {
        HANDLE notification = nullptr;
        ThrowIfWin32Failed(notify(family, callback, linkEvent, FALSE, &notification), call);
        return UniqueMibNotification(notification);
    }

    // Without rates to sample, only a change notification wakes the worker.
    DWORD SampleWaitMs(bool samplesRates) {
        if (samplesRates) {
            return kSampleMs;
        }
        return INFINITE;
    }

    NetworkLink LinkOfType(IFTYPE type) {
        if (type == IF_TYPE_IEEE80211) {
            return NetworkLink::Wifi;
        }
        return NetworkLink::Ethernet;
    }

} // namespace

void CancelMibNotification::operator()(HANDLE notification) const {
    CancelMibChangeNotify2(notification);
}

IpHelper::IpHelper(ChangeSignal& changes, bool samplesRates) :
    _changes(changes), _samplesRates(samplesRates), _linkEvent(AutoResetEvent("network link")), _state(NetworkState::None()),
    _interfaceNotification(RegisterMib(NotifyIpInterfaceChange, AF_UNSPEC, WakeOnInterfaceChange, _linkEvent.get(), "NotifyIpInterfaceChange")),
    _addressNotification(RegisterMib(NotifyUnicastIpAddressChange, AF_INET, WakeOnAddressChange, _linkEvent.get(), "NotifyUnicastIpAddressChange")),
    _routeNotification(RegisterMib(NotifyRouteChange2, AF_UNSPEC, WakeOnRouteChange, _linkEvent.get(), "NotifyRouteChange2")),
    _thread("network", [this](HANDLE stopEvent) { Run(stopEvent); }) {}

NetworkState IpHelper::State() const {
    const std::scoped_lock lock(_mutex);
    return _state;
}

bool IpHelper::SamplesRates() const {
    return _samplesRates;
}

// Nothing here may throw out of the thread, so every failure is logged once and the next sample tries again.
void IpHelper::Run(HANDLE stopEvent) {
    std::optional<Counters>     last;
    std::unique_ptr<WlanClient> wlan;
    FailureLog                  sampleLog("network");
    FailureLog                  wlanLog("network");
    const std::array<HANDLE, 2> events{stopEvent, _linkEvent.get()};
    const DWORD                 timeout = SampleWaitMs(_samplesRates);
    DWORD                       woke    = WAIT_TIMEOUT;
    do {
        NetworkState state = NetworkState::None();
        try {
            const std::optional<NET_IFINDEX> index = DefaultRouteInterface();
            if (index.has_value()) {
                MIB_IF_ROW2 row{};
                row.InterfaceIndex = *index;
                ThrowIfWin32Failed(GetIfEntry2(&row), "GetIfEntry2");
                const Counters now{
                    .index    = *index,
                    .received = row.InOctets,
                    .sent     = row.OutOctets,
                    .at       = Clock::now(),
                };
                if (last.has_value() && last->index == now.index) {
                    const double seconds         = std::chrono::duration<double>(now.at - last->at).count();
                    state.receivedBytesPerSecond = BytesPerSecond(last->received, now.received, seconds);
                    state.sentBytesPerSecond     = BytesPerSecond(last->sent, now.sent, seconds);
                }
                last                = now;
                state.link          = LinkOfType(row.Type);
                state.interfaceName = std::wstring(row.Alias);
                state.ipv4          = FirstIpv4(*index);
                if (state.link == NetworkLink::Wifi) {
                    try {
                        if (wlan == nullptr) {
                            wlan = std::make_unique<WlanClient>(_linkEvent.get());
                        }
                        auto [ssid, signal] = wlan->Connection(row.InterfaceGuid);
                        state.ssid          = std::move(ssid);
                        state.signalPercent = signal;
                        wlanLog.Succeeded();
                    } catch (const Win32Error& error) {
                        // The link is still Wi-Fi, only its details are missing. Windows 11 withholds them from apps
                        // without location access (ERROR_ACCESS_DENIED), which the log's reason shows. The client
                        // stays open unless the WLAN service lost it.
                        if (WlanClientLost(error.Result())) {
                            wlan.reset();
                        }
                        wlanLog.Failed("wifi_details_unavailable", error);
                    }
                }
            } else {
                last.reset();
            }
            sampleLog.Succeeded();
        } catch (const Win32Error& error) {
            last.reset();
            state = NetworkState::None();
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
        woke = WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE, timeout);
    } while (woke == WAIT_TIMEOUT || woke == WAIT_OBJECT_0 + 1);
    if (woke != WAIT_OBJECT_0) {
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
