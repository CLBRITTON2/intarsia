// Foreground retitles for the retitle scenario in scripts/run-soak-tests.ps1. It shows a window, takes the foreground
// and retitles the window about 60 times a second with a spinner and a percentage, as a terminal showing progress
// does. It runs until killed, and exits 2 when it cannot take the foreground or loses it, since the bar then follows
// another window.
//
//   window_sender.exe

#include <windows.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

    constexpr std::int32_t kExitNotForeground = 2;
    constexpr wchar_t kWindowClass[] = L"IntarsiaSoakRetitle";
    // Negative, which SetWaitableTimerEx reads as relative, in 100 ns units: about 60 a second.
    constexpr LONGLONG kPeriod100ns = -166'667;

    class NotForeground : public std::runtime_error {
      public:
        using std::runtime_error::runtime_error;
    };

    HWND CreateShownWindow() {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = kWindowClass;
        if (RegisterClassExW(&windowClass) == 0) {
            throw std::runtime_error(std::format("RegisterClassExW failed with {}", GetLastError()));
        }
        HWND window = CreateWindowExW(0, kWindowClass, L"intarsia soak retitle", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 400, 120, nullptr, nullptr,
                                      windowClass.hInstance, nullptr);
        if (window == nullptr) {
            throw std::runtime_error(std::format("CreateWindowExW failed with {}", GetLastError()));
        }
        ShowWindow(window, SW_SHOWNORMAL);
        return window;
    }

    std::wstring ProgressTitle(std::uint64_t tick) {
        constexpr std::array<wchar_t, 10> kSpinner{L'⠋', L'⠙', L'⠹', L'⠸', L'⠼', L'⠴', L'⠦', L'⠧', L'⠇', L'⠏'};
        const std::uint64_t percent = (tick / 6) % 101;
        const std::wstring filled(static_cast<size_t>(percent / 5), L'#');
        const std::wstring empty(static_cast<size_t>(20 - (percent / 5)), L' ');
        return std::format(L"{} building [{}{}] {}%", kSpinner[tick % kSpinner.size()], filled, empty, percent);
    }

    std::string ForegroundOwner() {
        HWND foreground = GetForegroundWindow();
        if (foreground == nullptr) {
            return "no foreground window";
        }
        std::array<char, 256> className{};
        GetClassNameA(foreground, className.data(), static_cast<int>(className.size()));
        DWORD processId = 0;
        GetWindowThreadProcessId(foreground, &processId);
        std::array<char, MAX_PATH> image{};
        DWORD imageLength = static_cast<DWORD>(image.size());
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (process == nullptr || !QueryFullProcessImageNameA(process, 0, image.data(), &imageLength)) {
            imageLength = 0;
        }
        if (process != nullptr) {
            CloseHandle(process);
        }
        return std::format("class={} pid={} image={}", className.data(), processId, std::string(image.data(), imageLength));
    }

    void DrainMessages() {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            DispatchMessageW(&message);
        }
    }

    [[noreturn]] void Retitle() {
        HWND window = CreateShownWindow();
        if (!SetForegroundWindow(window) || GetForegroundWindow() != window) {
            throw NotForeground(std::format("could not take the foreground from {}, start it from the foreground console", ForegroundOwner()));
        }
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (timer == nullptr) {
            throw std::runtime_error(std::format("CreateWaitableTimerExW failed with {}", GetLastError()));
        }
        const LARGE_INTEGER due{.QuadPart = kPeriod100ns};
        if (!SetWaitableTimerEx(timer, &due, 1000 / 60, nullptr, nullptr, nullptr, 0)) {
            throw std::runtime_error(std::format("SetWaitableTimerEx failed with {}", GetLastError()));
        }
        std::cout << "retitling the foreground window" << std::endl;

        std::uint64_t tick = 0;
        auto report = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (true) {
            // The timer resets as this wait returns for it, so only this result tells a tick from a message.
            const DWORD woken = MsgWaitForMultipleObjects(1, &timer, FALSE, INFINITE, QS_ALLINPUT);
            if (woken == WAIT_FAILED) {
                throw std::runtime_error(std::format("MsgWaitForMultipleObjects failed with {}", GetLastError()));
            }
            DrainMessages();
            if (woken != WAIT_OBJECT_0) {
                continue;
            }
            if (GetForegroundWindow() != window) {
                throw NotForeground(std::format("lost the foreground after {} retitles to {}", tick, ForegroundOwner()));
            }
            if (!SetWindowTextW(window, ProgressTitle(tick).c_str())) {
                throw std::runtime_error(std::format("SetWindowTextW failed with {}", GetLastError()));
            }
            ++tick;
            if (std::chrono::steady_clock::now() >= report) {
                std::cout << std::format("retitles={}", tick) << std::endl;
                report += std::chrono::seconds(5);
            }
        }
    }

} // namespace

// The wide entry point, which clang-tidy does not know as one.
int wmain() { // NOLINT(misc-use-internal-linkage)
    try {
        Retitle();
    } catch (const NotForeground& error) {
        std::cerr << error.what() << std::endl;
        return kExitNotForeground;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
