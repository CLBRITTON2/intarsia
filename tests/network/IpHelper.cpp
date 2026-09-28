#include "network/IpHelper.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace {

    constexpr std::uint32_t kChanged = WM_APP + 11;

} // namespace

TEST(IpHelper, RatesAreBytesPerSecondAndZeroWhenACounterResets) {
    EXPECT_DOUBLE_EQ(BytesPerSecond(1000, 3000, 2.0), 1000.0);
    EXPECT_DOUBLE_EQ(BytesPerSecond(3000, 1000, 2.0), 0.0);
    EXPECT_DOUBLE_EQ(BytesPerSecond(1000, 3000, 0.0), 0.0);
}

// Samples the machine's real network. It asserts only what holds whatever the machine is connected to.
TEST(IpHelper, SamplesTheDefaultRouteInterfaceAndStopsPromptly) {
    HWND         target = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    ChangeSignal changes(target, kChanged);
    {
        const IpHelper network(changes);
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
    for (std::int32_t round = 0; round < 10; ++round) {
        const IpHelper network(changes);
    }
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
    DestroyWindow(target);
}
