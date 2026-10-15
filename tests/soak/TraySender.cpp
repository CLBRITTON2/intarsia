// Tray apps for the tray scenario in scripts/run-soak-tests.ps1. A few owner windows keep icons in the bar's tray
// window and send what apps send, at about the rate a busy desktop's apps do: adds with new pixels, tooltip and icon
// changes, hides and shows, deletes and version changes, and now and then an owner that dies without deleting its
// icons, as a crashed app's do. It runs until killed, and exits 2 when the tray stops answering and 3 when no tray
// window of the class exists.
//
//   tray_sender.exe --class <window class the bar's tray registered, from --tray-class>

#include "support/TrayPayload.hpp"
#include "tray/ShellNotifyIcon.hpp"

#include <windows.h>

#include <shellapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

    constexpr std::int32_t  kExitHung   = 2;
    constexpr std::int32_t  kExitNoTray = 3;
    constexpr UINT          kCallback   = WM_APP + 9;
    constexpr size_t        kOwners     = 4;
    constexpr std::uint32_t kIds        = 4;
    constexpr std::uint32_t kAnswerMs   = 5000;
    // About ten messages a second across every owner.
    constexpr std::chrono::milliseconds kMessageGap{100};

    // The tray answering no message for this long is a hang.
    class TrayHung : public std::runtime_error {
      public:
        using std::runtime_error::runtime_error;
    };

    class NoTray : public std::runtime_error {
      public:
        using std::runtime_error::runtime_error;
    };

    std::wstring ParseClass(std::int32_t argc, wchar_t** argv) {
        if (argc != 3 || std::wstring_view(argv[1]) != L"--class") {
            throw std::invalid_argument("usage: tray_sender.exe --class <name>");
        }
        return argv[2];
    }

    HWND FindTray(const std::wstring& windowClass) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            if (HWND tray = FindWindowW(windowClass.c_str(), nullptr)) {
                return tray;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        throw NoTray("no window of the tray class appeared within 10 s");
    }

    HWND CreateOwner() {
        HWND owner = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
        if (owner == nullptr) {
            throw std::runtime_error(std::format("CreateWindowExW(owner) failed with {}", GetLastError()));
        }
        return owner;
    }

    // Tooltips as apps write them: a name, a status with a changing number, or two lines.
    std::wstring AppTip(std::mt19937& random) {
        const std::uint32_t value = random() % 101;
        switch (random() % 4) {
            case 0: return L"Cloud storage - Up to date";
            case 1: return std::format(L"Speakers: {}%", value);
            case 2: return std::format(L"Syncing {} files", value);
            default: return std::format(L"Battery\n{}% remaining", value);
        }
    }

    void SetTip(TrayNotifyData& data, const std::wstring& tip) {
        std::memcpy(data.data.tip.data(), tip.data(), std::min(tip.size(), data.data.tip.size() - 1) * sizeof(wchar_t));
    }

    // Returns whether the tray took the message. Throws TrayHung when it does not answer in time.
    bool Send(HWND tray, HWND sender, TrayNotifyData data) {
        COPYDATASTRUCT copy{
            .dwData = kNotifyIconCopyData,
            .cbData = sizeof(data),
            .lpData = &data,
        };
        DWORD_PTR result = 0;
        if (SendMessageTimeoutW(tray, WM_COPYDATA, reinterpret_cast<WPARAM>(sender), reinterpret_cast<LPARAM>(&copy), SMTO_NORMAL, kAnswerMs, &result) == 0) {
            const DWORD error = GetLastError();
            if (error == ERROR_TIMEOUT) {
                throw TrayHung(std::format("the tray answered no WM_COPYDATA within {} ms", kAnswerMs));
            }
            throw NoTray(std::format("SendMessageTimeoutW(WM_COPYDATA) failed with {}", error));
        }
        return result != FALSE;
    }

    // An add or an icon change, with pixels the tray has not seen. The tray reads them while the send waits, as
    // Explorer does, so the icon can go right after.
    bool SendIcon(HWND tray, HWND owner, std::uint32_t command, std::uint32_t id, std::mt19937& random) {
        TrayNotifyData data = NotifyData(command, Handle32(owner), id, 0, kCallback);
        HICON          icon = CreateSolidIcon(static_cast<std::uint32_t>(random()));
        data.data.flags     = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.data.icon      = Handle32(icon);
        SetTip(data, AppTip(random));
        const bool taken = Send(tray, owner, data);
        DestroyIcon(icon);
        return taken;
    }

    // One message of a random kind from a random owner.
    bool SendOne(HWND tray, const std::array<HWND, kOwners>& owners, std::mt19937& random) {
        HWND                owner = owners[random() % owners.size()];
        const std::uint32_t id    = random() % kIds;
        switch (random() % 8) {
            case 0:
            case 1: return SendIcon(tray, owner, NIM_ADD, id, random);
            case 2: return SendIcon(tray, owner, NIM_MODIFY, id, random);
            case 3:
            case 4: {
                TrayNotifyData data = NotifyData(NIM_MODIFY, Handle32(owner), id, 0, kCallback);
                data.data.flags     = NIF_TIP | NIF_SHOWTIP;
                SetTip(data, AppTip(random));
                return Send(tray, owner, data);
            }
            case 5: {
                TrayNotifyData data = NotifyData(NIM_MODIFY, Handle32(owner), id, 0, kCallback);
                data.data.flags     = NIF_STATE;
                data.data.stateMask = static_cast<std::uint32_t>(NIS_HIDDEN);
                if ((random() % 2) == 0) {
                    data.data.state = static_cast<std::uint32_t>(NIS_HIDDEN);
                } else {
                    data.data.state = 0;
                }
                return Send(tray, owner, data);
            }
            case 6: return Send(tray, owner, NotifyData(NIM_DELETE, Handle32(owner), id, 0, kCallback));
            default: {
                TrayNotifyData data = NotifyData(NIM_SETVERSION, Handle32(owner), id, 0, kCallback);
                if ((random() % 2) == 0) {
                    data.data.version = NOTIFYICON_VERSION_4;
                } else {
                    data.data.version = NOTIFYICON_VERSION;
                }
                return Send(tray, owner, data);
            }
        }
    }

    // Clicks the bar posts to an icon's owner land here, and would fill the queue unread.
    void DrainMessages() {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            DispatchMessageW(&message);
        }
    }

    [[noreturn]] void Run(const std::wstring& windowClass) {
        const std::uint32_t seed = GetCurrentProcessId();
        std::mt19937        random(seed);
        HWND                tray = FindTray(windowClass);
        std::cout << std::format("sending to the tray with seed {}", seed) << std::endl;

        std::array<HWND, kOwners> owners{};
        for (HWND& owner : owners) {
            owner = CreateOwner();
        }
        std::uint64_t sent   = 0;
        std::uint64_t taken  = 0;
        auto          report = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (true) {
            if (SendOne(tray, owners, random)) {
                ++taken;
            }
            ++sent;
            // About every 20 s an owner dies without deleting its icons, and a new one takes its place.
            if (random() % 200 == 0) {
                HWND& dying = owners[random() % owners.size()];
                DestroyWindow(dying);
                dying = CreateOwner();
            }
            DrainMessages();
            if (std::chrono::steady_clock::now() >= report) {
                std::cout << std::format("sent={} taken={}", sent, taken) << std::endl;
                report += std::chrono::seconds(5);
            }
            std::this_thread::sleep_for(kMessageGap);
        }
    }

} // namespace

// The wide entry point, which clang-tidy does not know as one.
int wmain(int argc, wchar_t** argv) { // NOLINT(misc-use-internal-linkage)
    try {
        Run(ParseClass(argc, argv));
    } catch (const TrayHung& error) {
        std::cerr << error.what() << std::endl;
        return kExitHung;
    } catch (const NoTray& error) {
        std::cerr << error.what() << std::endl;
        return kExitNoTray;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
