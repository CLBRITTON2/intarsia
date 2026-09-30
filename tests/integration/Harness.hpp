#pragma once

#include "platform/Handle.hpp"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Runs the real intarsia.exe against the test GlazeWM server (tests/stress/GlazeWmServer.cpp) and observes it only
// from outside the process: the commands the server receives, the bar's windows and its log.
namespace integration {

    // The test server lists only windows whose title starts with this (tests/stress/GlazeWmServer.cpp).
    inline constexpr std::wstring_view kWindowTitle = L"intarsia integration";

    // A child process with stdout and stderr in files, killed with its job if the test ends without closing it.
    class ChildProcess {
      public:
        ChildProcess(const std::filesystem::path& program, const std::vector<std::wstring>& arguments, const std::filesystem::path& stdoutPath,
                     const std::filesystem::path& stderrPath);

        std::uint32_t Id() const;
        bool          Running() const;
        // Posts WM_CLOSE to every top-level window of the process, hidden ones included, as taskkill without /F does,
        // then waits for it to exit. The exit code, or nullopt when it is still running after limit.
        std::optional<std::uint32_t> Close(std::chrono::milliseconds limit) const;
        void                         Kill() const;

      private:
        UniqueHandle  _job;
        UniqueHandle  _process;
        std::uint32_t _id;
    };

    // A visible, minimized top-level window titled kWindowTitle plus suffix, so the test server lists it.
    class TestWindow {
      public:
        explicit TestWindow(std::wstring_view suffix);
        ~TestWindow();
        TestWindow(const TestWindow&)            = delete;
        TestWindow& operator=(const TestWindow&) = delete;

      private:
        HWND _window;
    };

    // A new empty directory under the temp directory, kept after the test so its logs can be read.
    std::filesystem::path    NewWorkDirectory(std::string_view name);
    std::uint16_t            FreePort();
    void                     WriteText(const std::filesystem::path& path, std::string_view text);
    std::vector<std::string> ReadLines(const std::filesystem::path& path);

    // Polls condition until it holds or limit passes, pumping this thread's messages meanwhile so the bar's reads of
    // the test's own windows are answered.
    bool Eventually(const std::function<bool()>& condition, std::chrono::milliseconds limit);

    // The bar's window on the primary monitor, where the test server puts every workspace, or null while it has none.
    HWND PrimaryBarWindow(std::uint32_t processId);

    // Sends a left click every few pixels along the bar's middle, left to right, each handled before the next is sent.
    void SweepClicks(HWND bar);

    // What the screen shows over a window, as 0xRRGGBB per pixel, row by row.
    struct Capture {
        std::int32_t               width;
        std::int32_t               height;
        std::vector<std::uint32_t> pixels;
    };

    // Copies the window's rect from the screen, since a DirectComposition window draws nothing into its own DC.
    Capture CaptureScreenOver(HWND window);
    size_t  CountPixels(const Capture& capture, std::uint32_t rgb);

    // Each run of equal lines folded into one.
    std::vector<std::string> WithoutRepeats(const std::vector<std::string>& lines);

    // The test server and a bar connected to it, both started from config and style text written to a work directory.
    class RunningBar {
      public:
        RunningBar(std::string_view testName, std::string_view config, std::string_view style);
        // Closes the bar as a user would and records a test failure unless it exits 0.
        ~RunningBar();
        RunningBar(const RunningBar&)            = delete;
        RunningBar& operator=(const RunningBar&) = delete;

        HWND        PrimaryBar() const;
        // Every command the server has received so far, across its restarts.
        std::vector<std::string> Commands() const;
        // Commands sent after a sweep of clicks along the primary bar, with repeats folded. Waits until the stream
        // has been quiet for a moment, since the bar sends them from a thread of its own.
        std::vector<std::string> CommandsFromSweep() const;
        // Lines of the bar's structured stderr log that contain text.
        std::vector<std::string> LogLines(std::string_view text) const;
        void                     StopServer();
        void                     StartServer();

      private:
        std::filesystem::path       ServerOutput(std::uint32_t start) const;

        std::filesystem::path       _work;
        std::uint16_t               _port;
        std::uint32_t               _serverStarts;
        std::optional<ChildProcess> _server;
        std::optional<ChildProcess> _bar;
    };

} // namespace integration
