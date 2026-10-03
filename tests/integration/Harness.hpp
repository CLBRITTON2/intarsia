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

// Runs the real intarsia.exe against the test GlazeWM server (tests/support/GlazeWmServer.cpp) and observes it only
// from outside the process: the commands the server receives, the bar's windows and its log.
namespace integration {

    // The test server lists only windows whose title starts with this (tests/support/GlazeWmServer.cpp).
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

    // Prints a line in the bar's log layout to stdout, component harness, the fields as key=value pairs.
    void Log(std::string_view event, std::string_view fields);

    // Where NewWorkDirectory(name), and so a RunningBar named name, keeps its files, known before either makes it.
    std::filesystem::path WorkDirectoryPath(std::string_view name);
    // A new empty directory under the temp directory, kept after the test so its logs can be read.
    std::filesystem::path    NewWorkDirectory(std::string_view name);
    std::uint16_t            FreePort();
    void                     WriteText(const std::filesystem::path& path, std::string_view text);
    std::vector<std::string> ReadLines(const std::filesystem::path& path);
    // ReadLines of a file a child may not have written yet, empty until it has.
    std::vector<std::string> RecordedLines(const std::filesystem::path& path);

    // Polls condition until it holds or limit passes, pumping this thread's messages meanwhile so the bar's reads of
    // the test's own windows are answered.
    bool Eventually(const std::function<bool()>& condition, std::chrono::milliseconds limit);
    // Pumps this thread's messages for duration, for a test that checks something did not happen.
    void PumpFor(std::chrono::milliseconds duration);

    // The bar's window on the primary monitor, where the test server puts every workspace, or null while it has none.
    HWND PrimaryBarWindow(std::uint32_t processId);

    // The client point offset pixels along the bar from its start, on the middle line across it. A bar taller than it is
    // wide runs down a side of the monitor.
    POINT AlongBar(HWND bar, std::int32_t offset);
    // Sends a left click every few pixels along the bar's middle line, from its start, each handled before the next is
    // sent. The number of clicks sent.
    std::uint32_t SweepClicks(HWND bar);
    // Sends a button-up message (WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP) at a client point and waits until the window
    // has handled it.
    void SendButtonUp(HWND window, std::uint32_t message, POINT client);
    // Sends a left click offset pixels along the bar's middle line and waits until the bar has handled it.
    void SendClick(HWND bar, std::int32_t offset);
    // Sends one wheel notch (delta WHEEL_DELTA up, -WHEEL_DELTA down) offset pixels along the bar, in the screen
    // coordinates a real wheel message carries, and waits until the bar has handled it.
    void SendWheel(HWND bar, std::int32_t offset, std::int16_t delta);
    // Sends WM_KEYDOWN for a virtual key and waits until the window has handled it.
    void SendKey(HWND window, std::uint32_t virtualKey);
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

    // Moves the real cursor offset pixels along the bar's middle line. Hover and leave come only from the real cursor:
    // a sent WM_MOUSEMOVE starts TrackMouseEvent, which posts WM_MOUSELEAVE at once while the cursor is elsewhere.
    void PlaceCursor(HWND bar, std::int32_t offset);
    // The process's top-level window of a class while it is shown, or null.
    HWND VisibleWindow(std::uint32_t processId, std::wstring_view className);

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
    std::string LuaPath(const std::filesystem::path& path);

    // Each run of equal lines folded into one.
    std::vector<std::string> WithoutRepeats(const std::vector<std::string>& lines);

    // The server a RunningBar starts and the bar log event that shows the bar has reached it.
    struct TestServer {
        std::filesystem::path program;
        std::wstring          scenario;
        std::string           readyEvent;
    };

    // tests/support/GlazeWmServer.cpp, run with its scenario of that name, which the bar trusts and connects to.
    TestServer GlazeWmTestServer(std::wstring_view scenario);

    // The test server and a bar connected to it, both started from config and style text written to a work directory.
    class RunningBar {
      public:
        // Against GlazeWmTestServer(L"integration").
        RunningBar(std::string_view testName, std::string_view config, std::string_view style);
        RunningBar(std::string_view testName, std::string_view config, std::string_view style, TestServer server);
        // Closes the bar unless the test already has, and records a test failure unless it exits 0.
        ~RunningBar();
        RunningBar(const RunningBar&)            = delete;
        RunningBar& operator=(const RunningBar&) = delete;

        HWND        PrimaryBar() const;
        // Each sends its input offset pixels along the primary bar's middle line and logs it.
        void ClickAt(std::int32_t offset) const;
        void ButtonUpAt(std::uint32_t message, std::int32_t offset) const;
        void WheelAt(std::int32_t offset, std::int16_t delta) const;
        // Moves the real cursor offset pixels along the primary bar's middle line and logs it.
        void MoveCursorTo(std::int32_t offset) const;
        // Each is the bar's window of that kind while it is shown, or null.
        HWND Tooltip() const;
        HWND Menu() const;
        // Each is nullopt while the primary bar has no window, as between the bars a reload replaces and their new
        // ones.
        std::optional<size_t> PixelCount(std::uint32_t rgb) const;
        std::optional<double> MeanColumnOf(std::uint32_t rgb) const;
        // MeanColumnOf rounded to a pixel once the primary bar draws rgb, or nullopt when it does not within limit.
        std::optional<std::int32_t> DrawnColumn(std::uint32_t rgb, std::chrono::milliseconds limit) const;
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
        // Lines of the bar's structured stderr log whose event is event.
        std::vector<std::string> Events(std::string_view event) const;
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

        std::filesystem::path       _workDirectory;
        TestServer                  _testServer;
        std::uint16_t               _port;
        std::uint32_t               _serverStarts;
        std::optional<ChildProcess> _server;
        std::optional<ChildProcess> _bar;
    };

} // namespace integration
