#include "Harness.hpp"

#include "../platform/Loopback.hpp"
#include "platform/Errors.hpp"
#include "platform/Text.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace integration {

    namespace {

        constexpr wchar_t kWindowClass[] = L"IntarsiaIntegrationWindow";
        // Narrower than any item the bar draws, so a sweep clicks every item at least once.
        constexpr std::int32_t kSweepStepPixels = 3;
        // How long the command stream stays unchanged before a sweep's commands count as all sent.
        constexpr std::chrono::milliseconds kQuietFor{300};
        constexpr std::chrono::seconds      kStartLimit{15};
        constexpr std::chrono::seconds      kExitLimit{10};

        // Windows quotes arguments as CommandLineToArgvW reads them back, so a path holding spaces survives.
        std::wstring Quoted(const std::wstring& argument) {
            std::wstring quoted  = L"\"";
            size_t       slashes = 0;
            for (const wchar_t unit : argument) {
                if (unit == L'\\') {
                    ++slashes;
                    continue;
                }
                quoted.append(unit == L'"' ? (slashes * 2) + 1 : slashes, L'\\');
                slashes = 0;
                quoted += unit;
            }
            quoted.append(slashes * 2, L'\\');
            return quoted + L"\"";
        }

        UniqueHandle InheritableFile(const std::filesystem::path& path) {
            SECURITY_ATTRIBUTES inherit{.nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE};
            return OwnHandle(
                CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr),
                "CreateFileW(child output)");
        }

        UniqueHandle KillOnCloseJob() {
            UniqueHandle                         job = OwnHandle(CreateJobObjectW(nullptr, nullptr), "CreateJobObjectW");
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
                ThrowLastError("SetInformationJobObject");
            }
            return job;
        }

        struct ProcessWindows {
            DWORD             processId;
            std::vector<HWND> windows;
        };

        BOOL CALLBACK CollectProcessWindow(HWND window, LPARAM collection) {
            auto* collected = reinterpret_cast<ProcessWindows*>(collection); // NOLINT(performance-no-int-to-ptr)
            DWORD owner     = 0;
            GetWindowThreadProcessId(window, &owner);
            if (owner == collected->processId) {
                collected->windows.push_back(window);
            }
            return TRUE;
        }

        std::vector<HWND> TopLevelWindows(std::uint32_t processId) {
            ProcessWindows collection{.processId = processId, .windows = {}};
            EnumWindows(CollectProcessWindow, reinterpret_cast<LPARAM>(&collection));
            return collection.windows;
        }

        std::string ReadAll(const std::filesystem::path& path) {
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

        std::string LogValue(std::string_view value) {
            return std::format("\"{}\"", value);
        }

        std::string WindowHandle(HWND window) {
            return std::format("{:#x}", reinterpret_cast<std::uintptr_t>(window));
        }

        std::string CommandText(const std::string& line) {
            constexpr std::string_view kPrefix = "command ";
            return line.substr(kPrefix.size());
        }

        void LogPixelCount(std::uint32_t rgb, const std::optional<size_t>& count) {
            Log("pixels_counted", std::format("color=#{:06x} count={}", rgb, count.has_value() ? std::to_string(*count) : std::string("no_bar")));
        }

    } // namespace

    void Log(std::string_view event, std::string_view fields) {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        std::cout << std::format("{:02}:{:02}:{:02}.{:03} event={} {}", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, event, fields) << std::endl;
    }

    ChildProcess::ChildProcess(const std::filesystem::path& program, const std::vector<std::wstring>& arguments, const std::filesystem::path& stdoutPath,
                               const std::filesystem::path& stderrPath) : _job(KillOnCloseJob()), _id(0) {
        std::wstring commandLine = Quoted(program.wstring());
        for (const std::wstring& argument : arguments) {
            commandLine += L" " + Quoted(argument);
        }
        const UniqueHandle  output = InheritableFile(stdoutPath);
        const UniqueHandle  errors = InheritableFile(stderrPath);
        SECURITY_ATTRIBUTES inherit{.nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE};
        HANDLE              readEnd  = nullptr;
        HANDLE              writeEnd = nullptr;
        if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0)) {
            ThrowLastError("CreatePipe(child stdin)");
        }
        const UniqueHandle input(readEnd);
        _input = UniqueHandle(writeEnd);
        // Only the child's end is inherited, so the pipe reaches end of file once this side closes.
        if (!SetHandleInformation(_input.get(), HANDLE_FLAG_INHERIT, 0)) {
            ThrowLastError("SetHandleInformation(child stdin)");
        }
        STARTUPINFOW        startup{};
        PROCESS_INFORMATION started{};
        startup.cb         = sizeof(startup);
        startup.dwFlags    = STARTF_USESTDHANDLES;
        startup.hStdInput  = input.get();
        startup.hStdOutput = output.get();
        startup.hStdError  = errors.get();
        if (!CreateProcessW(program.c_str(), commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &started)) {
            ThrowLastError(std::format("CreateProcessW({})", program.string()));
        }
        _process = UniqueHandle(started.hProcess);
        const UniqueHandle thread(started.hThread);
        _id = started.dwProcessId;
        // Suspended until it is in the job, so nothing it starts escapes the kill on close.
        if (!AssignProcessToJobObject(_job.get(), _process.get())) {
            TerminateProcess(_process.get(), 1);
            ThrowLastError("AssignProcessToJobObject");
        }
        ResumeThread(thread.get());
    }

    std::uint32_t ChildProcess::Id() const {
        return _id;
    }

    bool ChildProcess::Running() const {
        return WaitForSingleObject(_process.get(), 0) == WAIT_TIMEOUT;
    }

    std::optional<std::uint32_t> ChildProcess::Close(std::chrono::milliseconds limit) const {
        for (HWND window : TopLevelWindows(_id)) {
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
        if (WaitForSingleObject(_process.get(), static_cast<DWORD>(limit.count())) != WAIT_OBJECT_0) {
            return std::nullopt;
        }
        DWORD code = 0;
        if (!GetExitCodeProcess(_process.get(), &code)) {
            ThrowLastError("GetExitCodeProcess");
        }
        return static_cast<std::uint32_t>(code);
    }

    void ChildProcess::Kill() const {
        if (Running()) {
            TerminateProcess(_process.get(), 1);
            WaitForSingleObject(_process.get(), INFINITE);
        }
    }

    void ChildProcess::WriteLine(std::string_view line) const {
        const std::string text    = std::string(line) + "\n";
        DWORD             written = 0;
        if (!WriteFile(_input.get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr) || written != text.size()) {
            ThrowLastError(std::format("WriteFile(child stdin, '{}')", line));
        }
    }

    TestWindow::TestWindow(std::wstring_view suffix) {
        const WNDCLASSEXW windowClass{.cbSize        = sizeof(WNDCLASSEXW),
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
                                      .hIconSm       = nullptr};
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

    std::uint16_t FreePort() {
        const loopback::Winsock winsock;
        return loopback::Listen().Port();
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

    std::uint32_t SweepClicks(HWND bar) {
        RECT client{};
        if (!GetClientRect(bar, &client)) {
            ThrowLastError("GetClientRect(bar)");
        }
        std::uint32_t clicks = 0;
        for (std::int32_t x = client.left; x < client.right; x += kSweepStepPixels) {
            SendClick(bar, x);
            ++clicks;
        }
        return clicks;
    }

    void SendClick(HWND bar, std::int32_t x) {
        RECT client{};
        if (!GetClientRect(bar, &client)) {
            ThrowLastError("GetClientRect(bar)");
        }
        DWORD_PTR result = 0;
        // Sent, not posted, so the bar handles each click before the next and none waits in its queue.
        if (SendMessageTimeoutW(bar, WM_LBUTTONUP, 0, MAKELPARAM(x, (client.bottom - client.top) / 2), SMTO_ABORTIFHUNG, 1000, &result) == 0) {
            ThrowLastError(std::format("SendMessageTimeoutW(WM_LBUTTONUP at {})", x));
        }
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

    void PlaceCursor(HWND bar, std::int32_t x) {
        const RECT rect = ScreenRect(bar);
        if (!SetCursorPos(rect.left + x, (rect.top + rect.bottom) / 2)) {
            ThrowLastError(std::format("SetCursorPos(bar column {})", x));
        }
    }

    HWND VisibleTooltipWindow(std::uint32_t processId) {
        HWND window = nullptr;
        while ((window = FindWindowExW(nullptr, window, L"IntarsiaTooltip", nullptr)) != nullptr) {
            DWORD owner = 0;
            GetWindowThreadProcessId(window, &owner);
            if (owner == processId && IsWindowVisible(window)) {
                return window;
            }
        }
        return nullptr;
    }

    RECT ScreenRect(HWND window) {
        RECT rect{};
        if (!GetWindowRect(window, &rect)) {
            ThrowLastError("GetWindowRect");
        }
        return rect;
    }

    Capture CaptureScreenOver(HWND window) {
        const RECT rect = ScreenRect(window);
        Capture    capture{.width = rect.right - rect.left, .height = rect.bottom - rect.top, .pixels = {}};
        capture.pixels.resize(static_cast<size_t>(capture.width) * static_cast<size_t>(capture.height));

        HDC screen = GetDC(nullptr);
        if (screen == nullptr) {
            throw std::runtime_error("GetDC(screen) failed");
        }
        HDC     memory = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, capture.width, capture.height);
        HGDIOBJ old    = SelectObject(memory, bitmap);
        // CAPTUREBLT takes layered windows too, as the bar is one.
        const BOOL copied = BitBlt(memory, 0, 0, capture.width, capture.height, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT);
        SelectObject(memory, old);
        // A negative height asks for rows top to bottom.
        BITMAPINFO info{};
        info.bmiHeader          = BITMAPINFOHEADER{.biSize          = sizeof(BITMAPINFOHEADER),
                                                   .biWidth         = capture.width,
                                                   .biHeight        = -capture.height,
                                                   .biPlanes        = 1,
                                                   .biBitCount      = 32,
                                                   .biCompression   = BI_RGB,
                                                   .biSizeImage     = 0,
                                                   .biXPelsPerMeter = 0,
                                                   .biYPelsPerMeter = 0,
                                                   .biClrUsed       = 0,
                                                   .biClrImportant  = 0};
        const std::int32_t rows = GetDIBits(memory, bitmap, 0, static_cast<UINT>(capture.height), capture.pixels.data(), &info, DIB_RGB_COLORS);
        DeleteObject(bitmap);
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        if (!copied) {
            ThrowLastError("BitBlt(screen)");
        }
        if (rows != capture.height) {
            throw std::runtime_error(std::format("GetDIBits copied {} of {} rows", rows, capture.height));
        }
        for (std::uint32_t& pixel : capture.pixels) {
            pixel &= 0xFFFFFF;
        }
        return capture;
    }

    size_t CountPixels(const Capture& capture, std::uint32_t rgb) {
        return static_cast<size_t>(std::ranges::count(capture.pixels, rgb));
    }

    std::optional<std::int32_t> ColumnSpan(const Capture& capture, std::uint32_t rgb) {
        std::optional<std::int32_t> leftmost;
        std::optional<std::int32_t> rightmost;
        for (size_t index = 0; index < capture.pixels.size(); ++index) {
            if (capture.pixels[index] == rgb) {
                const auto column = static_cast<std::int32_t>(index % static_cast<size_t>(capture.width));
                leftmost          = std::min(leftmost.value_or(column), column);
                rightmost         = std::max(rightmost.value_or(column), column);
            }
        }
        if (!leftmost.has_value() || !rightmost.has_value()) {
            return std::nullopt;
        }
        return *rightmost - *leftmost + 1;
    }

    std::optional<double> MeanColumn(const Capture& capture, std::uint32_t rgb) {
        double sum   = 0;
        size_t count = 0;
        for (size_t index = 0; index < capture.pixels.size(); ++index) {
            if (capture.pixels[index] == rgb) {
                sum += static_cast<double>(index % static_cast<size_t>(capture.width));
                ++count;
            }
        }
        if (count == 0) {
            return std::nullopt;
        }
        return sum / static_cast<double>(count);
    }

    std::filesystem::path ArgvRecorderPath() {
        return Executable(INTARSIA_ARGV_RECORDER).make_preferred();
    }

    std::string LuaString(std::string_view text) {
        return std::format("[==[{}]==]", text);
    }

    std::vector<std::string> WithoutRepeats(const std::vector<std::string>& lines) {
        std::vector<std::string> folded;
        std::ranges::unique_copy(lines, std::back_inserter(folded));
        return folded;
    }

    RunningBar::RunningBar(std::string_view testName, std::string_view config, std::string_view style) : _work(NewWorkDirectory(testName)), _port(FreePort()), _serverStarts(0) {
        Log("work_directory", std::format("path={}", LogValue(_work.string())));
        const std::filesystem::path configPath = _work / "config.lua";
        WriteText(configPath, config);
        WriteText(_work / "style.css", style);
        StartServer();
        const auto started = std::chrono::steady_clock::now();
        _bar.emplace(Executable(INTARSIA_EXE),
                     std::vector<std::wstring>{L"--config", configPath.wstring(), L"--log", (_work / "intarsia-file.log").wstring(), L"--glazewm-port", std::to_wstring(_port),
                                               L"--tray-class", std::wstring(kTrayClass)},
                     _work / "bar.out", _work / "bar.err");
        Log("bar_started", std::format("pid={} program={}", _bar->Id(), LogValue(INTARSIA_EXE)));
        if (!Eventually([this] { return !LogLines("event=connected").empty() && PrimaryBar() != nullptr; }, kStartLimit)) {
            PrintLogs();
            throw std::runtime_error(
                std::format("the bar did not connect and show a bar on the primary monitor within {} s (bar log {})", kStartLimit.count(), (_work / "bar.err").string()));
        }
        RECT client{};
        GetClientRect(PrimaryBar(), &client);
        Log("bar_ready",
            std::format("pid={} after_ms={} primary_bar={} size={}x{}", _bar->Id(), MillisecondsSince(started), WindowHandle(PrimaryBar()), client.right, client.bottom));
    }

    // A child left running is killed with its job as the members go.
    RunningBar::~RunningBar() {
        try {
            if (_bar.has_value()) {
                const std::optional<std::uint32_t> code = Close();
                if (!code.has_value()) {
                    ADD_FAILURE() << "the bar did not exit within " << kExitLimit.count() << " s of WM_CLOSE (bar log " << (_work / "bar.err").string() << ")";
                } else if (*code != 0) {
                    ADD_FAILURE() << "the bar exited with code " << *code << " on WM_CLOSE (bar log " << (_work / "bar.err").string() << ")";
                }
            }
            PrintLogs();
        } catch (const std::exception& error) { ADD_FAILURE() << "closing the bar failed: " << error.what() << " (logs in " << _work.string() << ")"; }
    }

    void RunningBar::PrintLogs() const {
        std::vector<std::filesystem::path> files{_work / "bar.err"};
        for (std::uint32_t start = 1; start <= _serverStarts; ++start) {
            files.push_back(_work / std::format("server-{}.err", start));
        }
        for (const std::filesystem::path& file : files) {
            for (const std::string& line : ReadLines(file)) {
                std::cout << file.filename().string() << "| " << line << std::endl;
            }
        }
    }

    HWND RunningBar::PrimaryBar() const {
        if (!_bar.has_value()) {
            throw std::logic_error("the bar is closed");
        }
        return PrimaryBarWindow(_bar->Id());
    }

    void RunningBar::ClickAt(std::int32_t x) const {
        SendClick(PrimaryBar(), x);
        Log("click", std::format("x={}", x));
    }

    void RunningBar::MoveCursorTo(std::int32_t x) const {
        PlaceCursor(PrimaryBar(), x);
        Log("cursor_moved", std::format("x={}", x));
    }

    HWND RunningBar::Tooltip() const {
        if (!_bar.has_value()) {
            throw std::logic_error("the bar is closed");
        }
        return VisibleTooltipWindow(_bar->Id());
    }

    std::optional<size_t> RunningBar::PixelsOf(std::uint32_t rgb) const {
        HWND window = PrimaryBar();
        if (window == nullptr) {
            return std::nullopt;
        }
        return CountPixels(CaptureScreenOver(window), rgb);
    }

    std::optional<double> RunningBar::ColumnOf(std::uint32_t rgb) const {
        HWND window = PrimaryBar();
        if (window == nullptr) {
            return std::nullopt;
        }
        return MeanColumn(CaptureScreenOver(window), rgb);
    }

    bool RunningBar::Draws(std::uint32_t rgb, std::chrono::milliseconds limit) const {
        std::optional<size_t> count;
        const bool            drawn = Eventually(
            [&] {
                count = PixelsOf(rgb);
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
                count = PixelsOf(rgb);
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
        std::ranges::copy_if(ReadLines(_work / "bar.err"), std::back_inserter(matching), [text](const std::string& line) { return line.find(text) != std::string::npos; });
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
        _server.emplace(Executable(INTARSIA_GLAZEWM_SERVER), std::vector<std::wstring>{L"--port", std::to_wstring(_port), L"--scenario", L"integration"},
                        ServerOutput(_serverStarts), _work / std::format("server-{}.err", _serverStarts));
        if (!Eventually([this] { return !ReadLines(ServerOutput(_serverStarts)).empty(); }, kStartLimit)) {
            PrintLogs();
            throw std::runtime_error(std::format("the test GlazeWM server did not listen on port {} within {} s (logs in {})", _port, kStartLimit.count(), _work.string()));
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
            throw std::runtime_error(std::format("the test GlazeWM server did not push focus {} within {} s (logs in {})", index, kStartLimit.count(), _work.string()));
        }
        Log("focus_pushed", std::format("index={} after_ms={}", index, MillisecondsSince(started)));
    }

    void RunningBar::SaveConfig(std::string_view config) const {
        WriteText(_work / "config.lua", config);
        Log("file_saved", "file=config.lua");
    }

    void RunningBar::SaveStyle(std::string_view style) const {
        WriteText(_work / "style.css", style);
        Log("file_saved", "file=style.css");
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

    std::filesystem::path RunningBar::ServerOutput(std::uint32_t start) const {
        return _work / std::format("server-{}.out", start);
    }

} // namespace integration
