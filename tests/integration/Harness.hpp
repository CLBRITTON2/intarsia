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
    // The bar's --tray-class, one no app uses, so the bar never takes the desktop's tray.
    inline constexpr std::wstring_view kTrayClass = L"IntarsiaIntegrationTray";

    // A child process with stdin on a pipe and stdout and stderr in files, killed with its job if the test ends without
    // closing it.
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
        void                         WriteLine(std::string_view line) const;

      private:
        UniqueHandle  _job;
        UniqueHandle  _input;
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

    // Prints "<local time> event=<event> <fields>" to stdout, the fields as key=value pairs.
    void Log(std::string_view event, std::string_view fields);

    // Where NewWorkDirectory(name), and so a RunningBar named name, keeps its files, known before either makes it.
    std::filesystem::path WorkDirectoryPath(std::string_view name);
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
    // The number of clicks sent.
    std::uint32_t SweepClicks(HWND bar);
    // Sends a left click at column x of the bar's middle row and waits until the bar has handled it.
    void SendClick(HWND bar, std::int32_t x);
    // Where the real cursor was when made, where it goes back to when destroyed.
    class SavedCursor {
      public:
        SavedCursor();
        ~SavedCursor();
        SavedCursor(const SavedCursor&)            = delete;
        SavedCursor& operator=(const SavedCursor&) = delete;

      private:
        POINT _position;
    };

    // Moves the real cursor to column x of the bar's middle row. Hover and leave come only from the real cursor: a
    // sent WM_MOUSEMOVE starts TrackMouseEvent, which posts WM_MOUSELEAVE at once while the cursor is elsewhere.
    void PlaceCursor(HWND bar, std::int32_t x);
    // The process's tooltip window while it is shown, or null.
    HWND VisibleTooltipWindow(std::uint32_t processId);

    // What the screen shows over a window, as 0xRRGGBB per pixel, row by row.
    struct Capture {
        std::int32_t               width;
        std::int32_t               height;
        std::vector<std::uint32_t> pixels;
    };

    RECT ScreenRect(HWND window);
    // Copies the window's rect from the screen, since a DirectComposition window draws nothing into its own DC.
    Capture CaptureScreenOver(HWND window);
    size_t  CountPixels(const Capture& capture, std::uint32_t rgb);
    // Columns from the leftmost pixel in rgb to the rightmost, both counted, or nullopt when there are none.
    std::optional<std::int32_t> ColumnSpan(const Capture& capture, std::uint32_t rgb);
    // The mean column of the pixels in rgb, or nullopt when there are none.
    std::optional<double> MeanColumn(const Capture& capture, std::uint32_t rgb);

    // argv_recorder.exe (tests/integration/ArgvRecorder.cpp), for a config's actions and custom commands to name.
    std::filesystem::path ArgvRecorderPath();
    // text as a Lua long string, which takes backslashes and quotes as they are.
    std::string LuaString(std::string_view text);

    // Each run of equal lines folded into one.
    std::vector<std::string> WithoutRepeats(const std::vector<std::string>& lines);

    // The test server and a bar connected to it, both started from config and style text written to a work directory.
    class RunningBar {
      public:
        RunningBar(std::string_view testName, std::string_view config, std::string_view style);
        // Closes the bar unless the test already has, and records a test failure unless it exits 0.
        ~RunningBar();
        RunningBar(const RunningBar&)            = delete;
        RunningBar& operator=(const RunningBar&) = delete;

        HWND        PrimaryBar() const;
        // Clicks the primary bar at column x of its middle row and logs it.
        void ClickAt(std::int32_t x) const;
        // Moves the real cursor to column x of the primary bar's middle row and logs it.
        void MoveCursorTo(std::int32_t x) const;
        // The bar's tooltip window while it is shown, or null.
        HWND Tooltip() const;
        // Each is nullopt while the primary bar has no window, as between the bars a reload replaces and their new
        // ones.
        std::optional<size_t> PixelsOf(std::uint32_t rgb) const;
        std::optional<double> ColumnOf(std::uint32_t rgb) const;
        // Each waits until the primary bar draws some pixel in rgb, or none, within limit and logs the count.
        bool Draws(std::uint32_t rgb, std::chrono::milliseconds limit) const;
        bool StopsDrawing(std::uint32_t rgb, std::chrono::milliseconds limit) const;
        // Every command the server has received so far, across its restarts.
        std::vector<std::string> Commands() const;
        // Commands sent after a sweep of clicks along the primary bar, with repeats folded. Waits until the stream
        // has been quiet for a moment, since the bar sends them from a thread of its own.
        std::vector<std::string> CommandsFromSweep() const;
        // Lines of the bar's structured stderr log that contain text.
        std::vector<std::string> LogLines(std::string_view text) const;
        void                     StopServer();
        void                     StartServer();
        // Has the server focus its indexth listed window and waits until it has sent the bar the events for it.
        void PushFocus(size_t index);
        // Each writes over its file in one save, as an editor does, which the running bar reloads.
        void SaveConfig(std::string_view config) const;
        void SaveStyle(std::string_view style) const;
        // Closes the bar as a user would, posting WM_CLOSE to its message box too after a fatal error. Its exit code,
        // or nullopt when it still ran after the limit and was killed.
        std::optional<std::uint32_t> Close();

      private:
        // Sweeps once, logs each command it sent with how many clicks sent it, and returns them all.
        std::vector<std::string> Sweep(std::uint32_t attempt) const;
        // Prints the bar's log and every server's stderr, each line tagged with its file.
        void                        PrintLogs() const;
        std::filesystem::path       ServerOutput(std::uint32_t start) const;

        std::filesystem::path       _work;
        std::uint16_t               _port;
        std::uint32_t               _serverStarts;
        std::optional<ChildProcess> _server;
        std::optional<ChildProcess> _bar;
    };

} // namespace integration
