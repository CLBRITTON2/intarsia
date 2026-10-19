#include "modules/Network.hpp"

#include "modules/Placeholders.hpp"
#include "platform/Text.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

    // How often a rate shown on the bar is read again, the backend's own sampling interval.
    constexpr std::uint32_t kRateTickMs = 1000;

    // The values every network format can name, from the state.
    std::vector<Placeholder> NetworkPlaceholders(const NetworkState& state) {
        return {{.name = "ssid", .value = ToUtf8(state.ssid)},
                {.name = "signal", .value = std::int64_t{state.signalPercent.value_or(0)}},
                {.name = "ip", .value = ToUtf8(state.ipv4)},
                {.name = "interface", .value = ToUtf8(state.interfaceName)},
                {.name = "down", .value = FormatRate(state.receivedBytesPerSecond)},
                {.name = "up", .value = FormatRate(state.sentBytesPerSecond)}};
    }

    // The config once every format is known to be valid. Throws ConfigError.
    NetworkConfig Validated(NetworkConfig config) {
        const std::vector<Placeholder> samples = NetworkPlaceholders(NetworkState{
            .link = NetworkLink::Wifi,
            .interfaceName = L"Wi-Fi",
            .ssid = L"home",
            .signalPercent = 80,
            .ipv4 = L"192.0.2.1",
            .receivedBytesPerSecond = 1000.0,
            .sentBytesPerSecond = 1000.0,
        });
        RequirePlaceholders("network format_wifi", config.formatWifi, samples);
        RequirePlaceholders("network format_ethernet", config.formatEthernet, samples);
        RequirePlaceholders("network format_disconnected", config.formatDisconnected, samples);
        RequireOptionalPlaceholders("network tooltip", config.tooltip, samples);
        return config;
    }

    bool NamesRate(const std::string& format) {
        return FormatNames(format, "down") || FormatNames(format, "up");
    }

} // namespace

Network::Network(NetworkConfig config, const INetwork& network) : IconTextModule(Validated(std::move(config))), _network(network), _showsRates(ShowsRates(Settings())) {}

ModuleText Network::Content() {
    return FormatNetwork(Settings(), _network.State());
}

std::optional<std::uint32_t> Network::MillisecondsUntilChange() const {
    if (_showsRates) {
        return kRateTickMs;
    }
    return std::nullopt;
}

std::optional<std::wstring> Network::TooltipText() {
    if (!Settings().tooltip.has_value()) {
        return std::nullopt;
    }
    return FormatPlaceholders(*Settings().tooltip, NetworkPlaceholders(_network.State()));
}

ModuleText FormatNetwork(const NetworkConfig& config, const NetworkState& state) {
    switch (state.link) {
        case NetworkLink::None: return NormalText(config.iconDisconnected, FormatPlaceholders(config.formatDisconnected, NetworkPlaceholders(state)));
        case NetworkLink::Ethernet: return NormalText(config.iconEthernet, FormatPlaceholders(config.formatEthernet, NetworkPlaceholders(state)));
        case NetworkLink::Wifi: return NormalText(WifiIcon(config.iconsWifi, state.signalPercent), FormatPlaceholders(config.formatWifi, NetworkPlaceholders(state)));
    }
    throw std::invalid_argument(std::format("unknown network link {}", static_cast<std::int32_t>(state.link)));
}

bool ShowsRates(const NetworkConfig& config) {
    return std::ranges::any_of(std::array{&config.formatWifi, &config.formatEthernet, &config.formatDisconnected}, [](const std::string* format) { return NamesRate(*format); });
}

bool ReadsRates(const NetworkConfig& config) {
    return ShowsRates(config) || (config.tooltip.has_value() && NamesRate(*config.tooltip));
}

const std::wstring& WifiIcon(const std::vector<std::wstring>& icons, std::optional<std::int32_t> signalPercent) {
    if (!signalPercent.has_value()) {
        return icons.back();
    }
    return LevelIcon(icons, *signalPercent);
}
