#include "modules/Network.hpp"

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

    // The config once every format is known to be valid. Throws ConfigError.
    NetworkConfig Validated(NetworkConfig config) {
        const std::vector<Placeholder> samples = NetworkPlaceholders(NetworkState{.link                   = NetworkLink::Wifi,
                                                                                  .interfaceName          = L"Wi-Fi",
                                                                                  .ssid                   = L"home",
                                                                                  .signalPercent          = 80,
                                                                                  .ipv4                   = L"192.0.2.1",
                                                                                  .receivedBytesPerSecond = 1000.0,
                                                                                  .sentBytesPerSecond     = 1000.0});
        RequirePlaceholders("network format_wifi", config.formatWifi, samples);
        RequirePlaceholders("network format_ethernet", config.formatEthernet, samples);
        RequirePlaceholders("network format_disconnected", config.formatDisconnected, samples);
        RequireOptionalPlaceholders("network tooltip", config.tooltip, samples);
        return config;
    }

} // namespace

Network::Network(NetworkConfig config, const INetwork& network) : _config(Validated(std::move(config))), _network(network), _showsRates(ShowsRates(_config)) {}

bool Network::Layout(const RenderContext& render) {
    return _line.Update(render, FormatNetwork(_config, _network.State()));
}

std::optional<std::uint32_t> Network::MillisecondsUntilChange() const {
    return _showsRates ? std::optional<std::uint32_t>(kRateTickMs) : std::nullopt;
}

D2D1_SIZE_F Network::Size() const {
    return _line.Size();
}

void Network::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    _line.Draw(render, origin);
}

const ModuleActions& Network::Actions() const {
    return _config.actions;
}

std::optional<std::wstring> Network::TooltipText() {
    if (!_config.tooltip.has_value()) {
        return std::nullopt;
    }
    return ToWide(FormatPlaceholders(*_config.tooltip, NetworkPlaceholders(_network.State())));
}

std::vector<Placeholder> NetworkPlaceholders(const NetworkState& state) {
    return {{.name = "ssid", .value = ToUtf8(state.ssid)},
            {.name = "signal", .value = std::int64_t{state.signalPercent.value_or(0)}},
            {.name = "ip", .value = ToUtf8(state.ipv4)},
            {.name = "interface", .value = ToUtf8(state.interfaceName)},
            {.name = "down", .value = FormatRate(state.receivedBytesPerSecond)},
            {.name = "up", .value = FormatRate(state.sentBytesPerSecond)}};
}

ModuleText FormatNetwork(const NetworkConfig& config, const NetworkState& state) {
    switch (state.link) {
        case NetworkLink::None:
            return ModuleText{.icon       = config.iconDisconnected,
                              .text       = ToWide(FormatPlaceholders(config.formatDisconnected, NetworkPlaceholders(state))),
                              .state      = TextState::Normal,
                              .classIndex = 0};
        case NetworkLink::Ethernet:
            return ModuleText{
                .icon = config.iconEthernet, .text = ToWide(FormatPlaceholders(config.formatEthernet, NetworkPlaceholders(state))), .state = TextState::Normal, .classIndex = 0};
        case NetworkLink::Wifi:
            return ModuleText{.icon       = WifiIcon(config.iconsWifi, state.signalPercent),
                              .text       = ToWide(FormatPlaceholders(config.formatWifi, NetworkPlaceholders(state))),
                              .state      = TextState::Normal,
                              .classIndex = 0};
    }
    throw std::invalid_argument(std::format("unknown network link {}", static_cast<std::int32_t>(state.link)));
}

bool ShowsRates(const NetworkConfig& config) {
    return std::ranges::any_of(std::array{&config.formatWifi, &config.formatEthernet, &config.formatDisconnected},
                               [](const std::string* format) { return FormatNames(*format, "down") || FormatNames(*format, "up"); });
}

const std::wstring& WifiIcon(const std::vector<std::wstring>& icons, std::optional<std::int32_t> signalPercent) {
    if (!signalPercent.has_value()) {
        return icons.back();
    }
    return LevelIcon(icons, *signalPercent);
}
