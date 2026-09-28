#include "audio/CoreAudio.hpp"

#include <gtest/gtest.h>

#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <chrono>
#include <thread>

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
    HWND         target = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    ChangeSignal changes(target, kChanged);
    {
        const CoreAudio audio(changes);
        const auto      deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!audio.Output().has_value() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const std::optional<AudioOutput> output = audio.Output();
        if (!output.has_value()) {
            FAIL() << "no output device read within 5 s";
        }
        EXPECT_FALSE(output->device.empty());
        EXPECT_GE(output->volume, 0.0f);
        EXPECT_LE(output->volume, 1.0f);
        MSG message{};
        EXPECT_TRUE(PeekMessageW(&message, target, kChanged, kChanged, PM_REMOVE));
    }
    DestroyWindow(target);
}

TEST(CoreAudio, StopsPromptlyRightAfterStarting) {
    HWND         target = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    ChangeSignal changes(target, kChanged);
    const auto   started = std::chrono::steady_clock::now();
    for (std::int32_t round = 0; round < 20; ++round) {
        const CoreAudio audio(changes);
    }
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
    DestroyWindow(target);
}
