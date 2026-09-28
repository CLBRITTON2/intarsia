#include "modules/Network.hpp"

#include "platform/Text.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <stdexcept>
#include <string_view>
#include <utility>

Network::Network(NetworkConfig config, const INetwork& network) : _config(std::move(config)), _network(network), _shown{.icon = {}, .text = {}}, _text(EmptyIconText()) {
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
    NetworkText shown = FormatNetwork(_config, _network.State());
    if (_laidOut && shown == _shown) {
        return false;
    }
    _text    = shown.text.empty() && shown.icon.empty() ? EmptyIconText() : LayOutIconText(render, shown.icon, shown.text);
    _shown   = std::move(shown);
    _laidOut = true;
    return true;
}

D2D1_SIZE_F Network::Size() const {
    return _text.size;
}

void Network::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    DrawIconText(render, origin, _text, render.foreground);
}

std::optional<std::uint32_t> Network::MillisecondsUntilChange() const {
    return std::nullopt;
}

bool Network::Hover(std::optional<D2D1_POINT_2F>) {
    return false;
}

void Network::Scroll(ScrollDirection) {}

void Network::Click(D2D1_POINT_2F) {}

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

NetworkText FormatNetwork(const NetworkConfig& config, const NetworkState& state) {
    switch (state.link) {
        case NetworkLink::None: return NetworkText{.icon = config.iconDisconnected, .text = ToWide(FormatPlaceholders(config.formatDisconnected, NetworkPlaceholders(state)))};
        case NetworkLink::Ethernet: return NetworkText{.icon = config.iconEthernet, .text = ToWide(FormatPlaceholders(config.formatEthernet, NetworkPlaceholders(state)))};
        case NetworkLink::Wifi:
            return NetworkText{.icon = WifiIcon(config.iconsWifi, state.signalPercent), .text = ToWide(FormatPlaceholders(config.formatWifi, NetworkPlaceholders(state)))};
    }
    throw std::invalid_argument(std::format("unknown network link {}", static_cast<std::int32_t>(state.link)));
}

const std::wstring& WifiIcon(const std::vector<std::wstring>& icons, std::optional<std::int32_t> signalPercent) {
    if (!signalPercent.has_value()) {
        return icons.back();
    }
    const auto count = static_cast<std::int32_t>(icons.size());
    const auto index = std::clamp(std::clamp(*signalPercent, 0, 100) * count / 101, 0, count - 1);
    return icons[static_cast<size_t>(index)];
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
