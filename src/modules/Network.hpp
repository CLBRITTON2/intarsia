#pragma once

#include "config/Config.hpp"
#include "modules/IconTextModule.hpp"
#include "modules/TextLine.hpp"
#include "network/INetwork.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The interface the default route leaves through: its format and icon for Wi-Fi (the icon by signal strength),
// Ethernet or no connection. Input does nothing unless the config binds actions. The backend tells of link changes
// only, so a format showing a rate makes the module tick each second instead.
class Network final : public IconTextModule<NetworkConfig> {
  public:
    // Throws ConfigError when a format uses a name it has no value for.
    Network(NetworkConfig config, const INetwork& network);

    std::optional<std::uint32_t> MillisecondsUntilChange() const override;
    std::optional<std::wstring>  TooltipText() override;

  private:
    ModuleText      Content() override;

    const INetwork& _network;
    bool            _showsRates;
};

ModuleText FormatNetwork(const NetworkConfig& config, const NetworkState& state);

// Whether a format drawn on the bar (not the tooltip, read only as it shows) names {down} or {up}.
bool ShowsRates(const NetworkConfig& config);
// Whether any format, the tooltip's included, names {down} or {up}, so the backend has to sample rates.
bool ReadsRates(const NetworkConfig& config);

// The Wi-Fi icon for a signal: icons split 0 to 100 evenly, weakest first. The strongest while the signal is unknown.
const std::wstring& WifiIcon(const std::vector<std::wstring>& icons, std::optional<std::int32_t> signalPercent);
