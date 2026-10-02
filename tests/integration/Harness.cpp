#include "Harness.hpp"

#include "../platform/Loopback.hpp"
#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Text.hpp"

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
            return !value.empty() && value.find(' ') == std::string_view::npos ? std::string(value) : std::format("\"{}\"", value);
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

        bool Vertical(const RECT& client) {
            return client.bottom - client.top > client.right - client.left;
        }

        void SendAndWait(HWND window, std::uint32_t message, WPARAM wParam, LPARAM lParam, std::string_view what) {
            DWORD_PTR result = 0;
            if (SendMessageTimeoutW(window, message, wParam, lParam, SMTO_ABORTIFHUNG, 1000, &result) == 0) {
                ThrowLastError(std::format("SendMessageTimeoutW({})", what));
            }
        }

        void LogPixelCount(std::uint32_t rgb, const std::optional<size_t>& count) {
            Log("pixels_counted", std::format("color=#{:06x} count={}", rgb, count.has_value() ? std::to_string(*count) : std::string("no_bar")));
        }

    } // namespace

    void Log(std::string_view event, std::string_view fields) {
        std::cout << LogHead(LogLevel::Info, "harness", event) << " " << fields << std::endl;
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

    std::vector<std::string> RecordedLines(const std::filesystem::path& path) {
        return std::filesystem::exists(path) ? ReadLines(path) : std::vector<std::string>{};
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

    POINT AlongBar(HWND bar, std::int32_t offset) {
        const RECT client = ClientRect(bar);
        if (Vertical(client)) {
            return POINT{.x = client.right / 2, .y = offset};
        }
        return POINT{.x = offset, .y = client.bottom / 2};
    }

    std::uint32_t SweepClicks(HWND bar) {
        const RECT         client = ClientRect(bar);
        const std::int32_t length = Vertical(client) ? client.bottom : client.right;
        std::uint32_t      clicks = 0;
        for (std::int32_t offset = 0; offset < length; offset += kSweepStepPixels) {
            SendClick(bar, offset);
            ++clicks;
        }
        return clicks;
    }

    void SendButtonUp(HWND window, std::uint32_t message, POINT client) {
        // Sent, not posted, so the window handles each input before the next and none waits in its queue.
        SendAndWait(window, message, 0, MAKELPARAM(client.x, client.y), std::format("button message {:#x} at {},{}", message, client.x, client.y));
    }

    void SendClick(HWND bar, std::int32_t offset) {
        SendButtonUp(bar, WM_LBUTTONUP, AlongBar(bar, offset));
    }

    void SendWheel(HWND bar, std::int32_t offset, std::int16_t delta) {
        POINT point = AlongBar(bar, offset);
        if (!ClientToScreen(bar, &point)) {
            ThrowLastError("ClientToScreen(bar)");
        }
        SendAndWait(bar, WM_MOUSEWHEEL, MAKEWPARAM(0, delta), MAKELPARAM(point.x, point.y), std::format("WM_MOUSEWHEEL delta {} at {},{}", delta, point.x, point.y));
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

    void PlaceCursor(HWND bar, std::int32_t offset) {
        POINT point = AlongBar(bar, offset);
        if (!ClientToScreen(bar, &point)) {
            ThrowLastError("ClientToScreen(bar)");
        }
        if (!SetCursorPos(point.x, point.y)) {
            ThrowLastError(std::format("SetCursorPos(bar offset {})", offset));
        }
    }

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

    std::string LuaPath(const std::filesystem::path& path) {
        return LuaString(ToUtf8(path.wstring()));
    }

    std::vector<std::string> WithoutRepeats(const std::vector<std::string>& lines) {
        std::vector<std::string> folded;
        std::ranges::unique_copy(lines, std::back_inserter(folded));
        return folded;
    }

    TestServer GlazeWmTestServer(std::wstring_view scenario) {
        return TestServer{.program = Executable(INTARSIA_GLAZEWM_SERVER), .scenario = std::wstring(scenario), .readyEvent = "connected"};
    }

    RunningBar::RunningBar(std::string_view testName, std::string_view config, std::string_view style) : RunningBar(testName, config, style, GlazeWmTestServer(L"integration")) {}

    RunningBar::RunningBar(std::string_view testName, std::string_view config, std::string_view style, TestServer server) :
        _work(NewWorkDirectory(testName)), _serverKind(std::move(server)), _port(FreePort()), _serverStarts(0) {
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
        if (!Eventually([this] { return !Events(_serverKind.readyEvent).empty() && PrimaryBar() != nullptr; }, kStartLimit)) {
            PrintLogs();
            throw std::runtime_error(std::format("the bar did not log {} and show a bar on the primary monitor within {} s (bar log {})", _serverKind.readyEvent,
                                                 kStartLimit.count(), (_work / "bar.err").string()));
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
                    ADD_FAILURE() << "the bar did not exit within " << kExitLimit.count() << " s of WM_CLOSE (bar log " << (_work / "bar.err").string() << ")";
                } else if (*code != 0) {
                    ADD_FAILURE() << "the bar exited with code " << *code << " on WM_CLOSE (bar log " << (_work / "bar.err").string() << ")";
                }
            }
            PrintLogs();
        } catch (const std::exception& error) { ADD_FAILURE() << "closing the bar failed: " << error.what() << " (logs in " << _work.string() << ")"; }
    }

    void RunningBar::PrintLogs() const {
        // The bar's lines name their component, the server's plain text needs its source.
        for (const std::string& line : ReadLines(_work / "bar.err")) {
            std::cout << "    " << line << std::endl;
        }
        for (std::uint32_t start = 1; start <= _serverStarts; ++start) {
            for (const std::string& line : ReadLines(_work / std::format("server-{}.err", start))) {
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
        SendWheel(PrimaryBar(), offset, delta);
        Log("wheel", std::format("delta={} offset={}", delta, offset));
    }

    void RunningBar::MoveCursorTo(std::int32_t offset) const {
        PlaceCursor(PrimaryBar(), offset);
        Log("cursor_moved", std::format("offset={}", offset));
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

    std::optional<std::int32_t> RunningBar::DrawnColumn(std::uint32_t rgb, std::chrono::milliseconds limit) const {
        std::optional<double> column;
        if (!Eventually(
                [&] {
                    column = ColumnOf(rgb);
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

    std::vector<std::string> RunningBar::Events(std::string_view event) const {
        std::vector<std::string> matching;
        std::ranges::copy_if(ReadLines(_work / "bar.err"), std::back_inserter(matching), [event](const std::string& line) { return EventOf(line) == event; });
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
        _server.emplace(_serverKind.program, std::vector<std::wstring>{L"--port", std::to_wstring(_port), L"--scenario", _serverKind.scenario}, ServerOutput(_serverStarts),
                        _work / std::format("server-{}.err", _serverStarts));
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
