#include "Harness.hpp"

#include "Capture.hpp"
#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Text.hpp"
#include "support/Loopback.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <utility>

namespace integration {

    namespace {

        constexpr wchar_t kWindowClass[] = L"IntarsiaIntegrationWindow";
        // Narrower than any item the bar draws, so a sweep clicks every item at least once.
        constexpr std::int32_t kSweepStepPixels = 3;
        // How long the command stream stays unchanged before a sweep's commands count as all sent.
        constexpr std::chrono::milliseconds kQuietFor{300};
        constexpr std::chrono::seconds      kStartLimit{15};
        constexpr std::chrono::seconds      kExitLimit{10};

        std::string                         ReadAll(const std::filesystem::path& path) {
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                throw std::runtime_error(std::format("cannot open {}", path.string()));
            }
            return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        }

        // Pumps this thread's messages, so the bar's reads of the test's own windows are answered, then sleeps.
        void PumpMessagesAndWait() {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        // Waits until count stops changing, then returns its last value.
        size_t SettledCount(const std::function<size_t()>& count) {
            size_t last    = count();
            auto   changed = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - changed < kQuietFor) {
                PumpMessagesAndWait();
                const size_t now = count();
                if (now != last) {
                    last    = now;
                    changed = std::chrono::steady_clock::now();
                }
            }
            return last;
        }

        std::filesystem::path Executable(const char* path) {
            return std::filesystem::path(ToWide(path));
        }

        std::int64_t MillisecondsSince(std::chrono::steady_clock::time_point start) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        }

        // The event of a bar log line, the word after its time, level and component.
        std::string_view EventOf(std::string_view line) {
            size_t at = 0;
            for (std::uint32_t skipped = 0; skipped < 3; ++skipped) {
                at = line.find(' ', line.find_first_not_of(' ', at));
            }
            at = line.find_first_not_of(' ', at);
            if (at == std::string_view::npos) {
                return {};
            }
            return line.substr(at, line.find(' ', at) - at);
        }

        std::string WindowHandle(HWND window) {
            return std::format("{:#x}", reinterpret_cast<std::uintptr_t>(window));
        }

        std::string CommandText(const std::string& line) {
            constexpr std::string_view kPrefix = "command ";
            return line.substr(kPrefix.size());
        }

        RECT ClientRect(HWND window) {
            RECT client{};
            if (!GetClientRect(window, &client)) {
                ThrowLastError("GetClientRect");
            }
            return client;
        }

        // A bar taller than it is wide runs down a side of the monitor.
        bool Vertical(const RECT& client) {
            return client.bottom - client.top > client.right - client.left;
        }

        std::int32_t LengthAlongBar(const RECT& client) {
            if (Vertical(client)) {
                return client.bottom;
            }
            return client.right;
        }

        void SendAndWait(HWND window, std::uint32_t message, WPARAM wParam, LPARAM lParam, std::string_view what) {
            DWORD_PTR result = 0;
            if (SendMessageTimeoutW(window, message, wParam, lParam, SMTO_ABORTIFHUNG, 1000, &result) == 0) {
                ThrowLastError(std::format("SendMessageTimeoutW({})", what));
            }
        }

        void LogPixelCount(std::uint32_t rgb, const std::optional<size_t>& count) {
            if (!count.has_value()) {
                Log("pixels_counted", std::format("color=#{:06x} count=no_bar", rgb));
                return;
            }
            Log("pixels_counted", std::format("color=#{:06x} count={}", rgb, *count));
        }

        std::uint16_t FreePort() {
            const loopback::Winsock winsock;
            return loopback::Listen().Port();
        }

        // The bar's window on the primary monitor, where the test server puts every workspace, or null while it has none.
        HWND PrimaryBarWindow(std::uint32_t processId) {
            HMONITOR primary = MonitorFromPoint(POINT{.x = 0, .y = 0}, MONITOR_DEFAULTTOPRIMARY);
            HWND     window  = nullptr;
            while ((window = FindWindowExW(nullptr, window, L"IntarsiaBar", nullptr)) != nullptr) {
                DWORD owner = 0;
                GetWindowThreadProcessId(window, &owner);
                if (owner == processId && MonitorFromWindow(window, MONITOR_DEFAULTTONULL) == primary) {
                    return window;
                }
            }
            return nullptr;
        }

        // The process's top-level window of a class while it is shown, or null.
        HWND VisibleWindow(std::uint32_t processId, std::wstring_view className) {
            const std::wstring name(className);
            HWND               window = nullptr;
            while ((window = FindWindowExW(nullptr, window, name.c_str(), nullptr)) != nullptr) {
                DWORD owner = 0;
                GetWindowThreadProcessId(window, &owner);
                if (owner == processId && IsWindowVisible(window)) {
                    return window;
                }
            }
            return nullptr;
        }

        // The client point offset pixels along the bar from its start, on the middle line across it.
        POINT AlongBar(HWND bar, std::int32_t offset) {
            const RECT client = ClientRect(bar);
            if (Vertical(client)) {
                return POINT{.x = client.right / 2, .y = offset};
            }
            return POINT{.x = offset, .y = client.bottom / 2};
        }

        void SendButtonUp(HWND window, std::uint32_t message, POINT client) {
            // Sent, not posted, so the window handles each input before the next and none waits in its queue.
            SendAndWait(window, message, 0, MAKELPARAM(client.x, client.y), std::format("button message {:#x} at {},{}", message, client.x, client.y));
        }

        void SendClick(HWND bar, std::int32_t offset) {
            SendButtonUp(bar, WM_LBUTTONUP, AlongBar(bar, offset));
        }

        // Each run of equal lines folded into one.
        std::vector<std::string> WithoutRepeats(const std::vector<std::string>& lines) {
            std::vector<std::string> folded;
            std::ranges::unique_copy(lines, std::back_inserter(folded));
            return folded;
        }

    } // namespace

    void Log(std::string_view event, std::string_view fields) {
        std::cout << LogHead(LogLevel::Info, "harness", event) << " " << fields << std::endl;
    }

    std::string LogValue(std::string_view value) {
        if (!value.empty() && value.find(' ') == std::string_view::npos) {
            return std::string(value);
        }
        return std::format("\"{}\"", value);
    }

    TestWindow::TestWindow(std::wstring_view suffix) {
        const WNDCLASSEXW windowClass{
            .cbSize        = sizeof(WNDCLASSEXW),
            .style         = 0,
            .lpfnWndProc   = DefWindowProcW,
            .cbClsExtra    = 0,
            .cbWndExtra    = 0,
            .hInstance     = GetModuleHandleW(nullptr),
            .hIcon         = LoadIconW(nullptr, IDI_APPLICATION),
            .hCursor       = nullptr,
            .hbrBackground = nullptr,
            .lpszMenuName  = nullptr,
            .lpszClassName = kWindowClass,
            .hIconSm       = nullptr,
        };
        // A second window registers the class again, which fails harmlessly.
        RegisterClassExW(&windowClass);
        const std::wstring title = std::wstring(kWindowTitle) + std::wstring(suffix);
        _window                  = CreateWindowExW(0, kWindowClass, title.c_str(), WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (_window == nullptr) {
            ThrowLastError("CreateWindowExW(test window)");
        }
        // Minimized, so it counts as visible to the server without covering the desktop.
        ShowWindow(_window, SW_SHOWMINNOACTIVE);
        Log("window_opened", std::format("title={} handle={}", LogValue(ToUtf8(title)), WindowHandle(_window)));
    }

    TestWindow::~TestWindow() {
        DestroyWindow(_window);
    }

    std::filesystem::path WorkDirectoryPath(std::string_view name) {
        return std::filesystem::temp_directory_path() / std::format("intarsia-integration-{}-{}", name, GetCurrentProcessId());
    }

    std::filesystem::path NewWorkDirectory(std::string_view name) {
        const std::filesystem::path work = WorkDirectoryPath(name);
        std::filesystem::remove_all(work);
        std::filesystem::create_directories(work);
        return work;
    }

    void WriteText(const std::filesystem::path& path, std::string_view text) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << text;
        if (!file) {
            throw std::runtime_error(std::format("cannot write {}", path.string()));
        }
    }

    std::vector<std::string> ReadLines(const std::filesystem::path& path) {
        const std::string        text = ReadAll(path);
        std::vector<std::string> lines;
        size_t                   start = 0;
        // A last line with no newline yet is still being written.
        for (size_t end = text.find('\n'); end != std::string::npos; end = text.find('\n', start)) {
            std::string line = text.substr(start, end - start);
            if (line.ends_with('\r')) {
                line.pop_back();
            }
            lines.push_back(std::move(line));
            start = end + 1;
        }
        return lines;
    }

    std::vector<std::string> RecordedLines(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            return {};
        }
        return ReadLines(path);
    }

    void PumpFor(std::chrono::milliseconds duration) {
        const auto deadline = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < deadline) {
            PumpMessagesAndWait();
        }
    }

    bool Eventually(const std::function<bool()>& condition, std::chrono::milliseconds limit) {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < deadline) {
            if (condition()) {
                return true;
            }
            PumpMessagesAndWait();
        }
        return condition();
    }

    std::uint32_t SweepClicks(HWND bar) {
        const std::int32_t length = LengthAlongBar(ClientRect(bar));
        std::uint32_t      clicks = 0;
        for (std::int32_t offset = 0; offset < length; offset += kSweepStepPixels) {
            SendClick(bar, offset);
            ++clicks;
        }
        return clicks;
    }

    void SendKey(HWND window, std::uint32_t virtualKey) {
        SendAndWait(window, WM_KEYDOWN, virtualKey, 0, std::format("WM_KEYDOWN {:#x}", virtualKey));
    }

    SavedCursor::SavedCursor() : _position{} {
        if (!GetCursorPos(&_position)) {
            ThrowLastError("GetCursorPos");
        }
    }

    SavedCursor::~SavedCursor() {
        if (!SetCursorPos(_position.x, _position.y)) {
            Log("cursor_restore_failed", std::format("x={} y={} error={}", _position.x, _position.y, GetLastError()));
        }
    }

    std::filesystem::path ArgvRecorderPath() {
        return Executable(INTARSIA_ARGV_RECORDER).make_preferred();
    }

    std::filesystem::path CliPath() {
        return Executable(INTARSIA_CLI_EXE);
    }

    std::string LuaString(std::string_view text) {
        return std::format("[==[{}]==]", text);
    }

    std::string LuaPath(const std::filesystem::path& path) {
        return LuaString(ToUtf8(path.wstring()));
    }

    TestServer GlazeWmTestServer(std::wstring_view scenario) {
        return TestServer{
            .program    = Executable(INTARSIA_GLAZEWM_SERVER),
            .scenario   = std::wstring(scenario),
            .readyEvent = "connected",
        };
    }

    RunningBar::RunningBar(std::string_view testName, std::string_view config, std::string_view style) : RunningBar(testName, config, style, GlazeWmTestServer(L"integration")) {}

    RunningBar::RunningBar(std::string_view testName, std::string_view config, std::string_view style, TestServer server) :
        _workDirectory(NewWorkDirectory(testName)), _testServer(std::move(server)), _port(FreePort()), _serverStarts(0) {
        Log("work_directory", std::format("path={}", LogValue(_workDirectory.string())));
        const std::filesystem::path configPath = _workDirectory / "config.lua";
        WriteText(configPath, config);
        WriteText(_workDirectory / "style.conf", style);
        StartServer();
        const auto started = std::chrono::steady_clock::now();
        _bar.emplace(Executable(INTARSIA_EXE),
                     std::vector<std::wstring>{L"--config", configPath.wstring(), L"--log", (_workDirectory / "intarsia-file.log").wstring(), L"--glazewm-port",
                                               std::to_wstring(_port), L"--tray-class", std::wstring(kTrayClass)},
                     _workDirectory / "bar.out", BarErrors());
        Log("bar_started", std::format("pid={} program={}", _bar->Id(), LogValue(INTARSIA_EXE)));
        if (!Eventually([this] { return !Events(_testServer.readyEvent).empty() && PrimaryBar() != nullptr; }, kStartLimit)) {
            PrintLogs();
            throw std::runtime_error(std::format("the bar did not log {} and show a bar on the primary monitor within {} s (bar log {})", _testServer.readyEvent,
                                                 kStartLimit.count(), BarErrors().string()));
        }
        const RECT client = ClientRect(PrimaryBar());
        Log("bar_ready",
            std::format("pid={} after_ms={} primary_bar={} size={}x{}", _bar->Id(), MillisecondsSince(started), WindowHandle(PrimaryBar()), client.right, client.bottom));
    }

    // A child left running is killed with its job as the members go.
    RunningBar::~RunningBar() {
        try {
            if (_bar.has_value()) {
                const std::optional<std::uint32_t> code = Close();
                if (!code.has_value()) {
                    ADD_FAILURE() << "the bar did not exit within " << kExitLimit.count() << " s of WM_CLOSE (bar log " << BarErrors().string() << ")";
                } else if (*code != 0) {
                    ADD_FAILURE() << "the bar exited with code " << *code << " on WM_CLOSE (bar log " << BarErrors().string() << ")";
                }
            }
            PrintLogs();
        } catch (const std::exception& error) { ADD_FAILURE() << "closing the bar failed: " << error.what() << " (logs in " << _workDirectory.string() << ")"; }
    }

    void RunningBar::PrintLogs() const {
        // The bar's lines name their component, the server's plain text needs its source.
        for (const std::string& line : ReadLines(BarErrors())) {
            std::cout << "    " << line << std::endl;
        }
        for (std::uint32_t start = 1; start <= _serverStarts; ++start) {
            for (const std::string& line : ReadLines(ServerErrors(start))) {
                std::cout << std::format("    server-{}: ", start) << line << std::endl;
            }
        }
    }

    HWND RunningBar::PrimaryBar() const {
        if (!_bar.has_value()) {
            throw std::logic_error("the bar is closed");
        }
        return PrimaryBarWindow(_bar->Id());
    }

    void RunningBar::ClickAt(std::int32_t offset) const {
        SendClick(PrimaryBar(), offset);
        Log("click", std::format("offset={}", offset));
    }

    void RunningBar::ButtonUpAt(std::uint32_t message, std::int32_t offset) const {
        HWND bar = PrimaryBar();
        SendButtonUp(bar, message, AlongBar(bar, offset));
        Log("button_up", std::format("message={:#x} offset={}", message, offset));
    }

    void RunningBar::WheelAt(std::int32_t offset, std::int16_t delta) const {
        HWND  bar   = PrimaryBar();
        POINT point = AlongBar(bar, offset);
        // A real wheel message carries screen coordinates.
        if (!ClientToScreen(bar, &point)) {
            ThrowLastError("ClientToScreen(bar)");
        }
        SendAndWait(bar, WM_MOUSEWHEEL, MAKEWPARAM(0, delta), MAKELPARAM(point.x, point.y), std::format("WM_MOUSEWHEEL delta {} at {},{}", delta, point.x, point.y));
        Log("wheel", std::format("delta={} offset={}", delta, offset));
    }

    void RunningBar::MoveCursorTo(std::int32_t offset) const {
        HWND  bar   = PrimaryBar();
        POINT point = AlongBar(bar, offset);
        if (!ClientToScreen(bar, &point)) {
            ThrowLastError("ClientToScreen(bar)");
        }
        if (!SetCursorPos(point.x, point.y)) {
            ThrowLastError(std::format("SetCursorPos(bar offset {})", offset));
        }
        Log("cursor_moved", std::format("offset={}", offset));
    }

    std::int32_t RunningBar::OffsetPastLastModule() const {
        const RECT barRect = ScreenRect(PrimaryBar());
        return barRect.right - barRect.left - 2;
    }

    HWND RunningBar::Tooltip() const {
        if (!_bar.has_value()) {
            throw std::logic_error("the bar is closed");
        }
        return VisibleWindow(_bar->Id(), L"IntarsiaTooltip");
    }

    HWND RunningBar::Menu() const {
        if (!_bar.has_value()) {
            throw std::logic_error("the bar is closed");
        }
        return VisibleWindow(_bar->Id(), L"IntarsiaMenu");
    }

    std::optional<size_t> RunningBar::PixelCount(std::uint32_t rgb) const {
        HWND window = PrimaryBar();
        if (window == nullptr) {
            return std::nullopt;
        }
        return CountPixels(CaptureScreenOver(window), rgb);
    }

    std::optional<double> RunningBar::MeanColumnOf(std::uint32_t rgb) const {
        HWND window = PrimaryBar();
        if (window == nullptr) {
            return std::nullopt;
        }
        return MeanColumn(CaptureScreenOver(window), rgb);
    }

    std::optional<std::int32_t> RunningBar::DrawnColumn(std::uint32_t rgb, std::chrono::milliseconds limit) const {
        std::optional<double> column;
        if (!Eventually(
                [&] {
                    column = MeanColumnOf(rgb);
                    return column.has_value();
                },
                limit)) {
            return std::nullopt;
        }
        return static_cast<std::int32_t>(std::lround(column.value_or(0)));
    }

    bool RunningBar::Draws(std::uint32_t rgb, std::chrono::milliseconds limit) const {
        std::optional<size_t> count;
        const bool            drawn = Eventually(
            [&] {
                count = PixelCount(rgb);
                return count.value_or(0) > 0;
            },
            limit);
        LogPixelCount(rgb, count);
        return drawn;
    }

    bool RunningBar::StopsDrawing(std::uint32_t rgb, std::chrono::milliseconds limit) const {
        std::optional<size_t> count;
        const bool            gone = Eventually(
            [&] {
                count = PixelCount(rgb);
                return count == size_t{0};
            },
            limit);
        LogPixelCount(rgb, count);
        return gone;
    }

    std::vector<std::string> RunningBar::Commands() const {
        std::vector<std::string> commands;
        for (std::uint32_t start = 1; start <= _serverStarts; ++start) {
            std::ranges::copy_if(ReadLines(ServerOutput(start)), std::back_inserter(commands), [](const std::string& line) { return line.starts_with("command "); });
        }
        return commands;
    }

    std::vector<std::string> RunningBar::CommandsFromSweep() const {
        // Sweeps that run before the bar has the server's answers, or while it reconnects, send nothing, so they
        // repeat until one sends a command. The sweep after that one sees what the bar settled on.
        std::uint32_t attempt = 0;
        if (!Eventually([&] { return !Sweep(++attempt).empty(); }, kStartLimit)) {
            Log("sweeps_sent_nothing", std::format("attempts={} limit_s={}", attempt, kStartLimit.count()));
            return {};
        }
        return WithoutRepeats(Sweep(++attempt));
    }

    std::vector<std::string> RunningBar::Sweep(std::uint32_t attempt) const {
        const auto          count   = [this] { return Commands().size(); };
        const size_t        before  = count();
        const auto          started = std::chrono::steady_clock::now();
        const std::uint32_t clicks  = SweepClicks(PrimaryBar());
        SettledCount(count);
        const std::vector<std::string> commands = Commands();
        const std::vector<std::string> sent(commands.begin() + static_cast<std::ptrdiff_t>(before), commands.end());
        Log("sweep", std::format("attempt={} clicks={} commands={} after_ms={}", attempt, clicks, sent.size(), MillisecondsSince(started)));
        for (auto run = sent.begin(); run != sent.end();) {
            const auto next = std::find_if(run, sent.end(), [&](const std::string& line) { return line != *run; });
            Log("command_received", std::format("attempt={} text={} clicks={}", attempt, LogValue(CommandText(*run)), std::distance(run, next)));
            run = next;
        }
        return sent;
    }

    std::vector<std::string> RunningBar::LogLines(std::string_view text) const {
        std::vector<std::string> matching;
        std::ranges::copy_if(ReadLines(BarErrors()), std::back_inserter(matching), [text](const std::string& line) { return line.find(text) != std::string::npos; });
        return matching;
    }

    std::vector<std::string> RunningBar::Events(std::string_view event) const {
        std::vector<std::string> matching;
        std::ranges::copy_if(ReadLines(BarErrors()), std::back_inserter(matching), [event](const std::string& line) { return EventOf(line) == event; });
        return matching;
    }

    void RunningBar::StopServer() {
        if (_server.has_value()) {
            const std::uint32_t pid = _server->Id();
            _server->Kill();
            _server.reset();
            Log("server_killed", std::format("pid={} start={}", pid, _serverStarts));
        }
    }

    // Waits for the server to listen, since the bar backs off for seconds after a refused connection.
    void RunningBar::StartServer() {
        ++_serverStarts;
        const auto started = std::chrono::steady_clock::now();
        _server.emplace(_testServer.program, std::vector<std::wstring>{L"--port", std::to_wstring(_port), L"--scenario", _testServer.scenario}, ServerOutput(_serverStarts),
                        ServerErrors(_serverStarts));
        if (!Eventually([this] { return !ReadLines(ServerOutput(_serverStarts)).empty(); }, kStartLimit)) {
            PrintLogs();
            throw std::runtime_error(
                std::format("the test GlazeWM server did not listen on port {} within {} s (logs in {})", _port, kStartLimit.count(), _workDirectory.string()));
        }
        Log("server_listening", std::format("pid={} port={} start={} after_ms={}", _server->Id(), _port, _serverStarts, MillisecondsSince(started)));
    }

    void RunningBar::PushFocus(size_t index) {
        if (!_server.has_value()) {
            throw std::logic_error("the test GlazeWM server is stopped");
        }
        const std::string pushed  = std::format("pushed focus {}", index);
        const size_t      before  = static_cast<size_t>(std::ranges::count(ReadLines(ServerOutput(_serverStarts)), pushed));
        const auto        started = std::chrono::steady_clock::now();
        _server->WriteLine(std::format("focus {}", index));
        if (!Eventually([&] { return static_cast<size_t>(std::ranges::count(ReadLines(ServerOutput(_serverStarts)), pushed)) > before; }, kStartLimit)) {
            PrintLogs();
            throw std::runtime_error(std::format("the test GlazeWM server did not push focus {} within {} s (logs in {})", index, kStartLimit.count(), _workDirectory.string()));
        }
        Log("focus_pushed", std::format("index={} after_ms={}", index, MillisecondsSince(started)));
    }

    void RunningBar::SaveConfig(std::string_view config) const {
        WriteText(_workDirectory / "config.lua", config);
        Log("file_saved", "file=config.lua");
    }

    void RunningBar::SaveStyle(std::string_view style) const {
        WriteText(_workDirectory / "style.conf", style);
        Log("file_saved", "file=style.conf");
    }

    // The child is killed with its job when it is reset still running.
    std::optional<std::uint32_t> RunningBar::Close() {
        if (!_bar.has_value()) {
            throw std::logic_error("the bar is closed");
        }
        const std::uint32_t                pid     = _bar->Id();
        const auto                         closing = std::chrono::steady_clock::now();
        const std::optional<std::uint32_t> code    = _bar->Close(kExitLimit);
        _bar.reset();
        if (code.has_value()) {
            Log("bar_exited", std::format("pid={} code={} after_ms={}", pid, *code, MillisecondsSince(closing)));
        } else {
            Log("bar_still_running", std::format("pid={} after_ms={}", pid, MillisecondsSince(closing)));
        }
        return code;
    }

    std::filesystem::path RunningBar::BarErrors() const {
        return _workDirectory / "bar.err";
    }

    std::filesystem::path RunningBar::ServerOutput(std::uint32_t start) const {
        return _workDirectory / std::format("server-{}.out", start);
    }

    std::filesystem::path RunningBar::ServerErrors(std::uint32_t start) const {
        return _workDirectory / std::format("server-{}.err", start);
    }

} // namespace integration
