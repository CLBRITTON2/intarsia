#pragma once

#include "ChildProcess.hpp"

#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Observes the real intarsia.exe only from outside the process: the server's commands, the bar's windows and its log.
namespace integration {

    // The test server lists only windows whose title starts with this (tests/support/GlazeWmServer.cpp).
    inline constexpr std::wstring_view kWindowTitle = L"intarsia integration";
    // The bar's --tray-class, one no app uses, so the bar never takes the desktop's tray.
    inline constexpr std::wstring_view kTrayClass = L"IntarsiaIntegrationTray";
    inline constexpr std::chrono::seconds kLimit{5};

    class TestWindow {
      public:
        explicit TestWindow(std::wstring_view suffix);
        ~TestWindow();
        TestWindow(const TestWindow&) = delete;
        TestWindow& operator=(const TestWindow&) = delete;

      private:
        HWND _window;
    };

    void Log(std::string_view event, std::string_view fields);
    std::string LogValue(std::string_view value);

    std::filesystem::path WorkDirectoryPath(std::string_view name);
    // Kept after the test so its logs can be read.
    std::filesystem::path NewWorkDirectory(std::string_view name);
    void WriteText(const std::filesystem::path& path, std::string_view text);
    std::vector<std::string> ReadLines(const std::filesystem::path& path);
    std::vector<std::string> RecordedLines(const std::filesystem::path& path);

    // Pumps this thread's messages while it polls, so the bar's reads of the test's own windows are answered.
    bool Eventually(const std::function<bool()>& condition, std::chrono::milliseconds limit);
    void PumpFor(std::chrono::milliseconds duration);

    // Clicks every few pixels along the bar's middle line, each handled before the next, and returns the count.
    std::uint32_t SweepClicks(HWND bar);
    // Waits until the window has handled the key.
    void SendKey(HWND window, std::uint32_t virtualKey);
    class SavedCursor {
      public:
        SavedCursor();
        ~SavedCursor();
        SavedCursor(const SavedCursor&) = delete;
        SavedCursor& operator=(const SavedCursor&) = delete;

      private:
        POINT _position;
    };

    // 0xRRGGBB per pixel, row by row.
    struct Capture {
        std::int32_t width;
        std::int32_t height;
        std::vector<std::uint32_t> pixels;
    };

    RECT ScreenRect(HWND window);
    // Copies the window's rect from the screen, since a DirectComposition window draws nothing into its own DC.
    Capture CaptureScreenOver(HWND window);
    size_t CountPixels(const Capture& capture, std::uint32_t rgb);
    std::optional<std::int32_t> ColumnSpan(const Capture& capture, std::uint32_t rgb);
    std::optional<double> MeanColumn(const Capture& capture, std::uint32_t rgb);

    std::filesystem::path ArgvRecorderPath();
    std::filesystem::path CliPath();
    // A Lua long string, which takes backslashes and quotes as they are.
    std::string LuaString(std::string_view text);
    std::string LuaPath(const std::filesystem::path& path);

    struct TestServer {
        std::filesystem::path program;
        std::wstring scenario;
        std::string readyEvent;
    };

    TestServer GlazeWmTestServer(std::wstring_view scenario);

    class RunningBar {
      public:
        RunningBar(std::string_view testName, std::string_view config, std::string_view style);
        RunningBar(std::string_view testName, std::string_view config, std::string_view style, TestServer server);
        // Closes the bar unless the test already has, and records a test failure unless it exits 0.
        ~RunningBar();
        RunningBar(const RunningBar&) = delete;
        RunningBar& operator=(const RunningBar&) = delete;

        HWND PrimaryBar() const;
        // Offsets are pixels along the primary bar's middle line from its start.
        void ClickAt(std::int32_t offset) const;
        void ButtonUpAt(std::uint32_t message, std::int32_t offset) const;
        void WheelAt(std::int32_t offset, std::int16_t delta) const;
        // A sent WM_MOUSEMOVE's TrackMouseEvent posts WM_MOUSELEAVE at once while the real cursor is elsewhere.
        void MoveCursorTo(std::int32_t offset) const;
        // No module lies there only while the right section is empty.
        std::int32_t OffsetPastLastModule() const;
        HWND Tooltip() const;
        HWND Menu() const;
        // nullopt between the bars a reload replaces and their new ones.
        std::optional<size_t> PixelCount(std::uint32_t rgb) const;
        std::optional<double> MeanColumnOf(std::uint32_t rgb) const;
        // Throws std::runtime_error when the primary bar does not draw rgb within limit.
        std::int32_t DrawnColumn(std::uint32_t rgb, std::chrono::milliseconds limit) const;
        bool Draws(std::uint32_t rgb, std::chrono::milliseconds limit) const;
        bool StopsDrawing(std::uint32_t rgb, std::chrono::milliseconds limit) const;
        // Across the server's restarts.
        std::vector<std::string> Commands() const;
        // Repeats folded. Waits for the stream to go quiet, since the bar sends from a thread of its own.
        std::vector<std::string> CommandsFromSweep() const;
        std::vector<std::string> LogLines(std::string_view text) const;
        std::vector<std::string> Events(std::string_view event) const;
        void StopServer();
        void StartServer();
        // Waits until the server has sent the bar the events for focusing its indexth listed window.
        void PushFocus(size_t index);
        void SaveConfig(std::string_view config) const;
        void SaveStyle(std::string_view style) const;
        // nullopt when the bar outlived the exit limit and was killed.
        std::optional<std::uint32_t> Close();

      private:
        const ChildProcess& OpenBar() const;
        std::vector<std::string> Sweep(std::uint32_t attempt) const;
        void PrintLogs() const;
        std::filesystem::path BarErrors() const;
        std::filesystem::path ServerOutput(std::uint32_t start) const;
        std::filesystem::path ServerErrors(std::uint32_t start) const;

        std::filesystem::path _workDirectory;
        TestServer _testServer;
        std::uint16_t _port;
        std::uint32_t _serverStarts;
        std::optional<ChildProcess> _server;
        std::optional<ChildProcess> _bar;
    };

} // namespace integration
