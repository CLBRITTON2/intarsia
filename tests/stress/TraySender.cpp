// Tray flood for the tray scenario in scripts/stress.ps1. It sends the bar's tray window what apps and hostile
// processes can: adds, modifies, deletes and version changes from owner windows that come and go, icon handles that do
// not read, tooltips of every awkward kind, malformed payloads and the appbar messages a taskbar forwards. It runs until
// killed, and exits 2 when the tray stops answering and 3 when no tray window of the class exists.
//
//   tray_sender.exe --class <window class the bar's tray registered, from --tray-class>

#include "tray/NotifyIconProtocol.hpp"

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
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

    constexpr std::int32_t kExitHung   = 2;
    constexpr std::int32_t kExitNoTray = 3;
    constexpr UINT         kCallback   = WM_APP + 9;
    constexpr size_t       kOwners     = 8;
    // Past kMaxTrayIcons across the owners, so adds keep meeting a full tray.
    constexpr std::uint32_t kIds          = 40;
    constexpr ULONG_PTR     kAppBarData   = 0; // what SHAppBarMessage sends
    constexpr ULONG_PTR     kIconRectData = 3; // what Shell_NotifyIconGetRect sends
    constexpr std::uint32_t kAnswerMs     = 5000;
    constexpr std::uint32_t kIconSide     = 16;

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

    std::uint32_t Handle32(const void* handle) {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(handle));
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

    // A solid icon of one color, so every add carries pixels the tray has not seen.
    HICON CreateSolidIcon(std::uint32_t color) {
        const std::vector<std::uint32_t> pixels(size_t{kIconSide} * kIconSide, color | 0xFF000000u);
        const std::array<std::byte, 32>  mask{};
        HBITMAP                          colorBitmap = CreateBitmap(kIconSide, kIconSide, 1, 32, pixels.data());
        HBITMAP                          maskBitmap  = CreateBitmap(kIconSide, kIconSide, 1, 1, mask.data());
        ICONINFO                         info{.fIcon = TRUE, .xHotspot = 0, .yHotspot = 0, .hbmMask = maskBitmap, .hbmColor = colorBitmap};
        HICON                            icon  = colorBitmap != nullptr && maskBitmap != nullptr ? CreateIconIndirect(&info) : nullptr;
        const DWORD                      error = GetLastError();
        DeleteObject(colorBitmap);
        DeleteObject(maskBitmap);
        if (icon == nullptr) {
            throw std::runtime_error(std::format("CreateIconIndirect failed with {}", error));
        }
        return icon;
    }

    // Tooltips an app may write: the longest, one with no null, bidi controls, lone surrogates and control characters.
    std::wstring AwkwardTip(std::mt19937& random) {
        switch (random() % 6) {
            case 0: return std::wstring(127, L'W');
            case 1: return std::wstring(128, L'x');
            case 2: return std::wstring(L"right") + static_cast<wchar_t>(0x202E) + L"to left" + static_cast<wchar_t>(0x2066) + L"isolate";
            case 3: return std::wstring(L"lone ") + static_cast<wchar_t>(0xD800) + L" high and " + static_cast<wchar_t>(0xDC00) + L" low";
            case 4: return std::wstring(L"line\r\nbreak\ttab") + static_cast<wchar_t>(0x07) + static_cast<wchar_t>(0x1B) + L"[31m";
            default: return L"";
        }
    }

    TrayNotifyData Notify(std::uint32_t command, HWND owner, std::uint32_t id) {
        TrayNotifyData data{};
        data.signature            = kTrayNotifySignature;
        data.command              = command;
        data.data.size            = sizeof(NotifyIconData32);
        data.data.window          = Handle32(owner);
        data.data.id              = id;
        data.data.callbackMessage = kCallback;
        return data;
    }

    std::vector<std::byte> Bytes(const TrayNotifyData& data) {
        std::vector<std::byte> bytes(sizeof(data));
        std::memcpy(bytes.data(), &data, sizeof(data));
        return bytes;
    }

    void SetTip(TrayNotifyData& data, const std::wstring& tip) {
        std::memcpy(data.data.tip.data(), tip.data(), std::min(tip.size(), data.data.tip.size()) * sizeof(wchar_t));
    }

    std::vector<std::byte> RandomBytes(std::mt19937& random, size_t size) {
        std::vector<std::byte> bytes(size);
        for (std::byte& byte : bytes) {
            byte = static_cast<std::byte>(random() & 0xFF);
        }
        return bytes;
    }

    // Returns whether the tray took the message. Throws TrayHung when it does not answer in time.
    bool Send(HWND tray, HWND sender, ULONG_PTR kind, std::vector<std::byte>& bytes) {
        COPYDATASTRUCT data{.dwData = kind, .cbData = static_cast<DWORD>(bytes.size()), .lpData = bytes.empty() ? nullptr : bytes.data()};
        DWORD_PTR      result = 0;
        if (SendMessageTimeoutW(tray, WM_COPYDATA, reinterpret_cast<WPARAM>(sender), reinterpret_cast<LPARAM>(&data), SMTO_NORMAL, kAnswerMs, &result) == 0) {
            const DWORD error = GetLastError();
            if (error == ERROR_TIMEOUT) {
                throw TrayHung(std::format("the tray answered no WM_COPYDATA within {} ms", kAnswerMs));
            }
            throw NoTray(std::format("SendMessageTimeoutW(WM_COPYDATA) failed with {}", error));
        }
        return result != FALSE;
    }

    // One message of a random kind from a random owner.
    bool SendOne(HWND tray, const std::array<HWND, kOwners>& owners, std::mt19937& random) {
        HWND                owner = owners[random() % owners.size()];
        const std::uint32_t id    = random() % kIds;
        switch (random() % 10) {
            case 0:
            case 1: {
                TrayNotifyData data = Notify(NIM_ADD, owner, id);
                HICON          icon = CreateSolidIcon(static_cast<std::uint32_t>(random()));
                data.data.flags     = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
                data.data.icon      = Handle32(icon);
                SetTip(data, AwkwardTip(random));
                std::vector<std::byte> bytes = Bytes(data);
                // The tray reads the pixels while this send waits, as Explorer does, so the icon can go right after.
                const bool taken = Send(tray, owner, kNotifyIconCopyData, bytes);
                DestroyIcon(icon);
                return taken;
            }
            case 2: {
                TrayNotifyData data = Notify(NIM_MODIFY, owner, id);
                data.data.flags     = NIF_TIP;
                SetTip(data, AwkwardTip(random));
                std::vector<std::byte> bytes = Bytes(data);
                return Send(tray, owner, kNotifyIconCopyData, bytes);
            }
            case 3: {
                TrayNotifyData data          = Notify(NIM_MODIFY, owner, id);
                data.data.flags              = NIF_STATE;
                data.data.state              = (random() % 2) == 0 ? static_cast<std::uint32_t>(NIS_HIDDEN) : 0u;
                data.data.stateMask          = static_cast<std::uint32_t>(NIS_HIDDEN);
                std::vector<std::byte> bytes = Bytes(data);
                return Send(tray, owner, kNotifyIconCopyData, bytes);
            }
            case 4: {
                std::vector<std::byte> bytes = Bytes(Notify(NIM_DELETE, owner, id));
                return Send(tray, owner, kNotifyIconCopyData, bytes);
            }
            case 5: {
                TrayNotifyData data          = Notify(NIM_SETVERSION, owner, id);
                data.data.version            = (random() % 2) == 0 ? NOTIFYICON_VERSION_4 : NOTIFYICON_VERSION;
                std::vector<std::byte> bytes = Bytes(data);
                return Send(tray, owner, kNotifyIconCopyData, bytes);
            }
            case 6: {
                // A handle no icon has, or one another process's object once had.
                TrayNotifyData data          = Notify((random() % 2) == 0 ? NIM_ADD : NIM_MODIFY, owner, id);
                data.data.flags              = NIF_ICON;
                data.data.icon               = static_cast<std::uint32_t>(random());
                std::vector<std::byte> bytes = Bytes(data);
                return Send(tray, owner, kNotifyIconCopyData, bytes);
            }
            case 7: {
                // Garbage of any length, some of it long enough to parse.
                std::vector<std::byte> bytes = RandomBytes(random, random() % (sizeof(TrayNotifyData) * 2));
                return Send(tray, owner, kNotifyIconCopyData, bytes);
            }
            case 8: {
                // The signature, then garbage: random commands, flags, sizes, handles and a tip with no null.
                std::vector<std::byte> bytes = RandomBytes(random, sizeof(TrayNotifyData) + (random() % 64));
                std::memcpy(bytes.data(), &kTrayNotifySignature, sizeof(kTrayNotifySignature));
                return Send(tray, owner, kNotifyIconCopyData, bytes);
            }
            default: {
                std::vector<std::byte> bytes = RandomBytes(random, random() % 128);
                return Send(tray, owner, (random() % 2) == 0 ? kAppBarData : kIconRectData, bytes);
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

    [[noreturn]] void Flood(const std::wstring& windowClass) {
        const std::uint32_t seed = GetCurrentProcessId();
        std::mt19937        random(seed);
        HWND                tray = FindTray(windowClass);
        std::cout << std::format("flooding the tray with seed {}", seed) << std::endl;

        std::array<HWND, kOwners> owners{};
        for (HWND& owner : owners) {
            owner = CreateOwner();
        }
        std::uint64_t sent   = 0;
        std::uint64_t taken  = 0;
        auto          report = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (true) {
            for (std::uint32_t batch = 0; batch < 50; ++batch) {
                taken += SendOne(tray, owners, random) ? 1 : 0;
                ++sent;
            }
            // An owner dies without deleting its icons, as a crashed app's do, and a new one takes its place.
            HWND& dying = owners[random() % owners.size()];
            if ((random() % 4) == 0) {
                DestroyWindow(dying);
                dying = CreateOwner();
            }
            DrainMessages();
            if (std::chrono::steady_clock::now() >= report) {
                std::cout << std::format("sent={} taken={}", sent, taken) << std::endl;
                report += std::chrono::seconds(5);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

} // namespace

// The wide entry point, which clang-tidy does not know as one.
int wmain(int argc, wchar_t** argv) { // NOLINT(misc-use-internal-linkage)
    try {
        Flood(ParseClass(argc, argv));
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
