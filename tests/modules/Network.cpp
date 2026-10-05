#include "modules/Network.hpp"

#include "../network/FakeNetwork.hpp"
#include "TextOnlyRender.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

    NetworkConfig TestConfig() {
        return NetworkConfig{
            .formatWifi         = "{ssid} {signal}%",
            .formatEthernet     = "{interface}",
            .formatDisconnected = "offline",
            .tooltip            = "{ip} down {down} up {up}",
            .iconsWifi          = {L"0", L"1", L"2", L"3", L"4"},
            .iconEthernet       = L"E",
            .iconDisconnected   = L"X",
            .actions =
                {
                    .click       = std::nullopt,
                    .rightClick  = std::nullopt,
                    .middleClick = std::nullopt,
                    .scrollUp    = std::nullopt,
                    .scrollDown  = std::nullopt,
                },
        };
    }

    NetworkState Wifi(std::optional<std::int32_t> signal) {
        return NetworkState{
            .link                   = NetworkLink::Wifi,
            .interfaceName          = L"Wi-Fi",
            .ssid                   = L"home",
            .signalPercent          = signal,
            .ipv4                   = L"192.0.2.7",
            .receivedBytesPerSecond = 1'250'000.0,
            .sentBytesPerSecond     = 800.0,
        };
    }

} // namespace

TEST(Network, EachLinkHasItsFormatAndIcon) {
    EXPECT_EQ(FormatNetwork(TestConfig(), Wifi(72)),
              (ModuleText{
                  .icon       = L"3",
                  .text       = L"home 72%",
                  .state      = TextState::Normal,
                  .classIndex = 0,
              }));
    NetworkState wired  = Wifi(std::nullopt);
    wired.link          = NetworkLink::Ethernet;
    wired.interfaceName = L"Ethernet 2";
    EXPECT_EQ(FormatNetwork(TestConfig(), wired),
              (ModuleText{
                  .icon       = L"E",
                  .text       = L"Ethernet 2",
                  .state      = TextState::Normal,
                  .classIndex = 0,
              }));
    const FakeNetwork none;
    EXPECT_EQ(FormatNetwork(TestConfig(), none.state),
              (ModuleText{
                  .icon       = L"X",
                  .text       = L"offline",
                  .state      = TextState::Normal,
                  .classIndex = 0,
              }));
}

TEST(Network, WifiIconsSplitTheSignalRangeEvenly) {
    const std::vector<std::wstring> icons{L"0", L"1", L"2", L"3", L"4"};
    EXPECT_EQ(WifiIcon(icons, 0), L"0");
    EXPECT_EQ(WifiIcon(icons, 20), L"0");
    EXPECT_EQ(WifiIcon(icons, 21), L"1");
    EXPECT_EQ(WifiIcon(icons, 100), L"4");
    // Windows can withhold the signal: the icon still says the link is Wi-Fi.
    EXPECT_EQ(WifiIcon(icons, std::nullopt), L"4");
    EXPECT_EQ(WifiIcon({L"only"}, 3), L"only");
}

TEST(Network, TooltipCarriesTheAddressAndRates) {
    FakeNetwork network;
    network.state = Wifi(50);
    Network module(TestConfig(), network);
    EXPECT_EQ(module.TooltipText(), L"192.0.2.7 down 1.2 MB/s up 800 B/s");
}

TEST(Network, LayoutReportsAChangeOnlyWhenTheShownTextChanges) {
    FakeNetwork          network;
    const TextOnlyRender render;
    Network              module(TestConfig(), network);
    EXPECT_TRUE(module.Layout(render.Context()));
    EXPECT_FALSE(module.Layout(render.Context()));
    network.state = Wifi(72);
    EXPECT_TRUE(module.Layout(render.Context()));
    // Rates only reach the tooltip here, so they change nothing drawn.
    network.state.receivedBytesPerSecond = 9.0;
    EXPECT_FALSE(module.Layout(render.Context()));
    EXPECT_GT(module.Size().width, 0.0f);
}

// The backend tells of link changes only, so a rate on the bar is read on a tick, and one only in the tooltip is not.
TEST(Network, ARateInAFormatMakesTheModuleTick) {
    const FakeNetwork network;
    EXPECT_FALSE(ShowsRates(TestConfig()));
    EXPECT_EQ(Network(TestConfig(), network).MillisecondsUntilChange(), std::nullopt);
    NetworkConfig rates  = TestConfig();
    rates.formatEthernet = "{interface} {down}";
    EXPECT_TRUE(ShowsRates(rates));
    EXPECT_EQ(Network(rates, network).MillisecondsUntilChange(), 1000u);
    NetworkConfig literal = TestConfig();
    literal.formatWifi    = "{{down}}";
    EXPECT_FALSE(ShowsRates(literal));
}

TEST(Network, ATooltipNamingARateNeedsRatesSampledButNoTick) {
    const FakeNetwork network;
    NetworkConfig     config = TestConfig();
    EXPECT_TRUE(ReadsRates(config));
    EXPECT_FALSE(ShowsRates(config));
    EXPECT_EQ(Network(config, network).MillisecondsUntilChange(), std::nullopt);
    config.tooltip = "{ip}";
    EXPECT_FALSE(ReadsRates(config));
    config.tooltip    = std::nullopt;
    config.formatWifi = "{down}";
    EXPECT_TRUE(ReadsRates(config));
}

TEST(Network, AFormatNamingAnUnknownValueIsAConfigError) {
    FakeNetwork   network;
    NetworkConfig config = TestConfig();
    config.formatWifi    = "{essid}";
    EXPECT_THAT([&] { return Network(config, network); }, testing::ThrowsMessage<ConfigError>(testing::HasSubstr("network format_wifi '{essid}' is not a valid format")));
}
