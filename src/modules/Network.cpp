#include "modules/Network.hpp"

#include "platform/Text.hpp"

#include <array>
#include <format>
#include <stdexcept>
#include <string_view>
#include <utility>

Network::Network(NetworkConfig config, const INetwork& network) : _config(std::move(config)), _network(network) {
    const std::vector<Placeholder> samples = NetworkPlaceholders(NetworkState{.link                   = NetworkLink::Wifi,
                                                                              .interfaceName          = L"Wi-Fi",
                                                                              .ssid                   = L"home",
                                                                              .signalPercent          = 80,
                                                                              .ipv4                   = L"192.0.2.1",
                                                                              .receivedBytesPerSecond = 1000.0,
                                                                              .sentBytesPerSecond     = 1000.0});
    RequirePlaceholders("network format_wifi", _config.formatWifi, samples);
    RequirePlaceholders("network format_ethernet", _config.formatEthernet, samples);
    RequirePlaceholders("network format_disconnected", _config.formatDisconnected, samples);
    if (_config.tooltip.has_value()) {
        RequirePlaceholders("network tooltip", *_config.tooltip, samples);
    }
}

bool Network::Layout(const RenderContext& render) {
    return _line.Update(render, FormatNetwork(_config, _network.State()));
}

D2D1_SIZE_F Network::Size() const {
    return _line.Size();
}

void Network::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    _line.Draw(render, origin);
}

std::optional<std::uint32_t> Network::MillisecondsUntilChange() const {
    return std::nullopt;
}

bool Network::Hover(std::optional<D2D1_POINT_2F>) {
    return false;
}

void                       Network::Scroll(ScrollDirection) {}

void                       Network::Click(D2D1_POINT_2F) {}

void                       Network::RightClick(D2D1_POINT_2F) {}

std::optional<D2D1_RECT_F> Network::TooltipItem() const {
    return std::nullopt;
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
            return ModuleText{.icon = config.iconDisconnected, .text = ToWide(FormatPlaceholders(config.formatDisconnected, NetworkPlaceholders(state))), .alert = false};
        case NetworkLink::Ethernet:
            return ModuleText{.icon = config.iconEthernet, .text = ToWide(FormatPlaceholders(config.formatEthernet, NetworkPlaceholders(state))), .alert = false};
        case NetworkLink::Wifi:
            return ModuleText{
                .icon = WifiIcon(config.iconsWifi, state.signalPercent), .text = ToWide(FormatPlaceholders(config.formatWifi, NetworkPlaceholders(state))), .alert = false};
    }
    throw std::invalid_argument(std::format("unknown network link {}", static_cast<std::int32_t>(state.link)));
}

const std::wstring& WifiIcon(const std::vector<std::wstring>& icons, std::optional<std::int32_t> signalPercent) {
    if (!signalPercent.has_value()) {
        return icons.back();
    }
    return LevelIcon(icons, *signalPercent);
}

std::string FormatRate(double bytesPerSecond) {
    constexpr std::array<std::string_view, 4> kUnits{"B/s", "kB/s", "MB/s", "GB/s"};
    double                                    value = bytesPerSecond;
    size_t                                    unit  = 0;
    while (value >= 1000.0 && unit + 1 < kUnits.size()) {
        value /= 1000.0;
        ++unit;
    }
    return unit == 0 ? std::format("{:.0f} {}", value, kUnits[unit]) : std::format("{:.1f} {}", value, kUnits[unit]);
}
