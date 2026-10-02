#include "network/IpHelper.hpp"
#include "support/MessageWindow.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace {

    constexpr std::uint32_t kChanged = WM_APP + 11;

} // namespace

TEST(IpHelper, SameLinkIgnoresTheRatesAlone) {
    const NetworkState first{
        .link = NetworkLink::Wifi, .interfaceName = L"Wi-Fi", .ssid = L"home", .signalPercent = 70, .ipv4 = L"192.0.2.7", .receivedBytesPerSecond = 1.0, .sentBytesPerSecond = 2.0};
    NetworkState second           = first;
    second.receivedBytesPerSecond = 900.0;
    second.sentBytesPerSecond     = 0.0;
    EXPECT_TRUE(SameLink(first, second));
    second.signalPercent = 71;
    EXPECT_FALSE(SameLink(first, second));
    second      = first;
    second.ssid = L"work";
    EXPECT_FALSE(SameLink(first, second));
    second      = first;
    second.link = NetworkLink::Ethernet;
    EXPECT_FALSE(SameLink(first, second));
}

TEST(IpHelper, RatesAreBytesPerSecondAndZeroWhenACounterResets) {
    EXPECT_DOUBLE_EQ(BytesPerSecond(1000, 3000, 2.0), 1000.0);
    EXPECT_DOUBLE_EQ(BytesPerSecond(3000, 1000, 2.0), 0.0);
    EXPECT_DOUBLE_EQ(BytesPerSecond(1000, 3000, 0.0), 0.0);
}

// Samples the machine's real network. It asserts only what holds whatever the machine is connected to.
TEST(IpHelper, SamplesTheDefaultRouteInterfaceAndStopsPromptly) {
    const MessageWindow target;
    ChangeSignal        changes(target.Get(), kChanged);
    {
        const IpHelper network(changes, true);
        // The first sample runs as the thread starts, and a second gives the rates.
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        const NetworkState state = network.State();
        if (state.link != NetworkLink::None) {
            EXPECT_FALSE(state.interfaceName.empty());
        }
        EXPECT_GE(state.receivedBytesPerSecond, 0.0);
        if (state.link != NetworkLink::Wifi) {
            EXPECT_TRUE(state.ssid.empty());
            EXPECT_FALSE(state.signalPercent.has_value());
        }
    }
    const auto started = std::chrono::steady_clock::now();
    // Without rates the thread waits on no clock at all, so the stop event alone ends it.
    for (std::int32_t round = 0; round < 10; ++round) {
        const IpHelper network(changes, round % 2 == 0);
    }
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
}
