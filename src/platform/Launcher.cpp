#include "platform/Launcher.hpp"

#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "platform/Log.hpp"
#include "platform/Process.hpp"
#include "platform/Text.hpp"
#include "platform/Threads.hpp"

#include <windows.h>

// Declares the interface keyword the shell headers use, which WIN32_LEAN_AND_MEAN leaves out of windows.h.
#include <objbase.h>

#include <exdisp.h>
#include <oleauto.h>
#include <servprov.h>
#include <shellapi.h>
#include <shldisp.h>
#include <shlguid.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

    constexpr size_t                    kMaxQueuedLaunches = 8;

    constexpr std::chrono::milliseconds kShellPoll{500};

    // ShellExecuteExW can use COM shell extensions, which need an apartment on the calling thread. Nothing here may throw
    // out of the launcher's thread, so a failed launch is logged.
    void Start(const Command& command) {
        const std::wstring program    = command.program.wstring();
        const std::wstring parameters = JoinArguments(command.arguments);
        try {
            const ComScope    com(ComThreading::Single, "launcher");
            SHELLEXECUTEINFOW info{};
            info.cbSize = sizeof(info);
            // Zone checks stay on, so a program downloaded from the internet still gets its warning.
            info.fMask  = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
            info.lpVerb = L"open";
            info.lpFile = program.c_str();
            if (!parameters.empty()) {
                info.lpParameters = parameters.c_str();
            }
            info.nShow = SW_SHOWNORMAL;
            if (!ShellExecuteExW(&info)) {
                ThrowLastError("ShellExecuteExW");
            }
        } catch (const Win32Error& error) {
            Log(LogLevel::Warning, "launcher", "launch_failed",
                {{.key = "reason", .value = error.what()}, {.key = "program", .value = ToUtf8(program)}, {.key = "arguments", .value = ToUtf8(parameters)}});
        }
    }

    using Bstr = UniqueReleased<BSTR, &SysFreeString>;

    Bstr MakeBstr(const std::wstring& text) {
        Bstr made(SysAllocStringLen(text.data(), static_cast<UINT>(text.size())));
        if (made == nullptr) {
            ThrowIfFailed(E_OUTOFMEMORY, "SysAllocStringLen");
        }
        return made;
    }

    VARIANT StringVariant(const Bstr& text) {
        VARIANT variant{};
        variant.vt      = VT_BSTR;
        variant.bstrVal = text.get();
        return variant;
    }

    // The desktop window's shell dispatch, which runs in Explorer, found as in
    // https://devblogs.microsoft.com/oldnewthing/20131118-00/?p=2643.
    Microsoft::WRL::ComPtr<IShellDispatch2> DesktopShell() {
        Microsoft::WRL::ComPtr<IShellWindows> windows;
        ThrowIfFailed(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows)), "CoCreateInstance(ShellWindows)");
        VARIANT location{};
        location.vt   = VT_I4;
        location.lVal = CSIDL_DESKTOP;
        VARIANT                           root{};
        long                              window = 0;
        Microsoft::WRL::ComPtr<IDispatch> desktop;
        const HRESULT                     found = windows->FindWindowSW(&location, &root, SWC_DESKTOP, &window, SWFO_NEEDDISPATCH, &desktop);
        ThrowIfFailed(found, "IShellWindows::FindWindowSW(desktop)");
        // S_FALSE is success with no window.
        if (found != S_OK || desktop == nullptr) {
            throw ShellLaunchError("the shell has no desktop window registered to start a program from");
        }
        Microsoft::WRL::ComPtr<IServiceProvider> provider;
        ThrowIfFailed(desktop.As(&provider), "QueryInterface(IServiceProvider)");
        Microsoft::WRL::ComPtr<IShellBrowser> browser;
        ThrowIfFailed(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser)), "IServiceProvider::QueryService(SID_STopLevelBrowser)");
        Microsoft::WRL::ComPtr<IShellView> view;
        ThrowIfFailed(browser->QueryActiveShellView(&view), "IShellBrowser::QueryActiveShellView");
        Microsoft::WRL::ComPtr<IDispatch> background;
        ThrowIfFailed(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background)), "IShellView::GetItemObject(SVGIO_BACKGROUND)");
        Microsoft::WRL::ComPtr<IShellFolderViewDual> folderView;
        ThrowIfFailed(background.As(&folderView), "QueryInterface(IShellFolderViewDual)");
        Microsoft::WRL::ComPtr<IDispatch> application;
        ThrowIfFailed(folderView->get_Application(&application), "IShellFolderViewDual::get_Application");
        Microsoft::WRL::ComPtr<IShellDispatch2> shell;
        ThrowIfFailed(application.As(&shell), "QueryInterface(IShellDispatch2)");
        return shell;
    }

} // namespace

Launcher::Launcher() : _queue(kMaxQueuedLaunches, [] { return WorkQueue<Command>::Process(Start); }) {}

void Launcher::Launch(const Command& command) {
    if (!_queue.Push(command)) {
        Log(LogLevel::Warning, "launcher", "launch_dropped", {{.key = "reason", .value = "queue full"}, {.key = "program", .value = ToUtf8(command.program.wstring())}});
    }
}

std::uint32_t ShellIntegrityLevel(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    HWND       shell    = GetShellWindow();
    if (shell == nullptr) {
        Log(LogLevel::Warning, "shell", "shell_missing", {{.key = "wait_ms", .value = std::to_string(timeout.count())}});
    }
    while (shell == nullptr) {
        if (std::chrono::steady_clock::now() >= deadline) {
            throw ShellLaunchError(std::format("no shell window appeared within {} ms", timeout.count()));
        }
        std::this_thread::sleep_for(kShellPoll);
        shell = GetShellWindow();
    }
    DWORD processId = 0;
    if (GetWindowThreadProcessId(shell, &processId) == 0) {
        ThrowLastError("GetWindowThreadProcessId(shell window)");
    }
    const UniqueHandle process = OwnHandle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId), "OpenProcess(shell)");
    HANDLE             raw     = nullptr;
    if (!OpenProcessToken(process.get(), TOKEN_QUERY, &raw)) {
        ThrowLastError("OpenProcessToken(shell)");
    }
    const UniqueHandle token(raw);
    return IntegrityLevel(token.get());
}

void StartThroughShell(const std::filesystem::path& program, const std::vector<std::wstring>& arguments) {
    const ComScope                                com(ComThreading::Single, "shell launch");
    const Microsoft::WRL::ComPtr<IShellDispatch2> shell     = DesktopShell();
    const Bstr                                    file      = MakeBstr(program.wstring());
    const Bstr                                    line      = MakeBstr(JoinArguments(arguments));
    const Bstr                                    directory = MakeBstr(program.parent_path().wstring());
    const Bstr                                    verb      = MakeBstr(L"open");
    VARIANT                                       show{};
    show.vt   = VT_I4;
    show.lVal = SW_SHOWNORMAL;
    ThrowIfFailed(shell->ShellExecute(file.get(), StringVariant(line), StringVariant(directory), StringVariant(verb), show), "IShellDispatch2::ShellExecute");
}
