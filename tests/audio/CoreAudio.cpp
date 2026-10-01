#include "audio/CoreAudio.hpp"
#include "support/Eventually.hpp"
#include "support/MessageWindow.hpp"

#include <gtest/gtest.h>

#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <chrono>

namespace {

    constexpr std::uint32_t kChanged = WM_APP + 9;

    // Whether the system has a default output device, asked of Core Audio directly.
    bool HasDefaultOutput() {
        const HRESULT                               initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
        Microsoft::WRL::ComPtr<IMMDevice>           device;
        const bool                                  found = SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&enumerator))) &&
            SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device));
        device.Reset();
        enumerator.Reset();
        if (SUCCEEDED(initialized)) {
            CoUninitialize();
        }
        return found;
    }

} // namespace

// Reads the real default device and never changes it: a test must not touch the volume of the desktop it runs on.
TEST(CoreAudio, ReadsTheDefaultOutputDevice) {
    if (!HasDefaultOutput()) {
        GTEST_SKIP() << "no default output device on this machine";
    }
    const MessageWindow target;
    ChangeSignal        changes(target.Get(), kChanged);
    {
        const CoreAudio audio(changes, AudioFlow::Output);
        Eventually([&audio] { return audio.Device().has_value(); }, std::chrono::seconds(5));
        const std::optional<AudioDevice> device = audio.Device();
        if (!device.has_value()) {
            FAIL() << "no output device read within 5 s";
        }
        EXPECT_FALSE(device->name.empty());
        EXPECT_GE(device->volume, 0.0f);
        EXPECT_LE(device->volume, 1.0f);
        MSG message{};
        EXPECT_TRUE(PeekMessageW(&message, target.Get(), kChanged, kChanged, PM_REMOVE));
    }
}

TEST(CoreAudio, StopsPromptlyRightAfterStarting) {
    const MessageWindow target;
    ChangeSignal        changes(target.Get(), kChanged);
    const auto          started = std::chrono::steady_clock::now();
    for (std::int32_t round = 0; round < 20; ++round) {
        const CoreAudio audio(changes, AudioFlow::Output);
    }
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
}
