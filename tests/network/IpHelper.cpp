#include "network/IpHelper.hpp"
#include "support/MessageWindow.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>

namespace {

    constexpr std::uint32_t kChanged = WM_APP + 11;

} // namespace

TEST(IpHelper, SameLinkIgnoresTheRatesAlone) {
    const NetworkState first{
        .link                   = NetworkLink::Wifi,
        .interfaceName          = L"Wi-Fi",
        .ssid                   = L"home",
        .signalPercent          = 70,
        .ipv4                   = L"192.0.2.7",
        .receivedBytesPerSecond = 1.0,
        .sentBytesPerSecond     = 2.0,
    };
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

TEST(IpHelper, StopsPromptlyRightAfterStarting) {
    ChangeTarget target(kChanged);
    const auto   started = std::chrono::steady_clock::now();
    // Without rates the thread waits on no clock at all, so the stop event alone ends it.
    for (std::int32_t round = 0; round < 10; ++round) {
        const IpHelper network(target.signal, round % 2 == 0);
    }
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
}
