#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/IconText.hpp"
#include "modules/Placeholders.hpp"
#include "network/INetwork.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// What the network module shows for the state as it is.
struct NetworkText {
    std::wstring icon;
    std::wstring text;

    bool         operator==(const NetworkText&) const = default;
};

// The interface the default route leaves through: its format and icon for Wi-Fi (the icon by signal strength),
// Ethernet or no connection. Input does nothing unless the config binds actions.
class Network final : public IModule {
  public:
    // Throws ConfigError when a format uses a name it has no value for.
    Network(NetworkConfig config, const INetwork& network);

    bool                         Layout(const RenderContext& render) override;
    D2D1_SIZE_F                  Size() const override;
    void                         Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    std::optional<std::uint32_t> MillisecondsUntilChange() const override;
    void                         Click(D2D1_POINT_2F point) override;
    void                         RightClick(D2D1_POINT_2F point) override;
    void                         Scroll(ScrollDirection direction) override;
    bool                         Hover(std::optional<D2D1_POINT_2F> point) override;
    const ModuleActions&         Actions() const override;
    std::optional<std::wstring>  TooltipText() override;
    std::optional<D2D1_RECT_F>   TooltipItem() const override;

  private:
    NetworkConfig   _config;
    const INetwork& _network;
    NetworkText     _shown; // what _text holds, once laid out
    bool            _laidOut = false;
    IconText        _text;
};

// The values every network format can name, from the state.
std::vector<Placeholder> NetworkPlaceholders(const NetworkState& state);

NetworkText              FormatNetwork(const NetworkConfig& config, const NetworkState& state);

// The Wi-Fi icon for a signal: icons split 0 to 100 evenly, weakest first. The strongest while the signal is unknown.
const std::wstring& WifiIcon(const std::vector<std::wstring>& icons, std::optional<std::int32_t> signalPercent);

// A rate in SI units with one decimal, as "850 B/s", "1.2 kB/s" or "3.4 MB/s".
std::string FormatRate(double bytesPerSecond);
