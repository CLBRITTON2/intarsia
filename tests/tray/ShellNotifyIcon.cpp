#include "tray/ShellNotifyIcon.hpp"

#include "platform/AppBar.hpp"
#include "platform/Win32Cast.hpp"
#include "tray/NotifyIconProtocol.hpp"

#include <shellapi.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

    // A class no app sends to, so the test tray never takes the desktop's icons.
    constexpr wchar_t       kTestClass[] = L"IntarsiaTestTray";
    constexpr std::uint32_t kChanged     = WM_APP + 1;
    constexpr std::uint32_t kFailed      = WM_APP + 2;
    constexpr std::uint32_t kCallback    = WM_APP + 9;

    HWND                    MessageWindow() {
        return CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    }

    std::uint32_t Handle32(const void* handle) {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(handle));
    }

    std::vector<std::byte> AddPayload(HWND owner, std::uint32_t id, HICON icon) {
        TrayNotifyData data{};
        data.signature            = kTrayNotifySignature;
        data.command              = NIM_ADD;
        data.data.size            = sizeof(NotifyIconData32);
        data.data.window          = Handle32(owner);
        data.data.id              = id;
        data.data.flags           = NIF_MESSAGE | NIF_TIP | (icon != nullptr ? NIF_ICON : 0);
        data.data.callbackMessage = kCallback;
        data.data.icon            = Handle32(icon);
        std::memcpy(data.data.tip.data(), L"tip", sizeof(L"tip"));
        std::vector<std::byte> bytes(sizeof(data));
        std::memcpy(bytes.data(), &data, sizeof(data));
        return bytes;
    }

    LRESULT Send(HWND tray, ULONG_PTR kind, std::vector<std::byte>& bytes) {
        COPYDATASTRUCT data{.dwData = kind, .cbData = static_cast<DWORD>(bytes.size()), .lpData = bytes.data()};
        DWORD_PTR      result = 0;
        EXPECT_NE(SendMessageTimeoutW(tray, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_NORMAL, 5000, &result), 0);
        return static_cast<LRESULT>(result);
    }

    bool Eventually(const std::function<bool()>& condition, std::chrono::milliseconds limit) {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < deadline) {
            if (condition()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return condition();
    }

    class ShellNotifyIconTray : public testing::Test {
      public:
        ShellNotifyIconTray() : _target(MessageWindow()), _owner(MessageWindow()), _changes(_target, kChanged), _tray(_changes, kTestClass, _target, kFailed) {}
        ~ShellNotifyIconTray() override {
            DestroyWindow(_owner);
            DestroyWindow(_target);
        }
        ShellNotifyIconTray(const ShellNotifyIconTray&)            = delete;
        ShellNotifyIconTray& operator=(const ShellNotifyIconTray&) = delete;

      protected:
        HWND            _target;
        HWND            _owner;
        ChangeSignal    _changes;
        ShellNotifyIcon _tray;
        HWND            _window = FindWindowW(kTestClass, nullptr);
    };

    // Stores the last icon or icon rect message shell32 sends the taskbar window.
    LRESULT CALLBACK CaptureCopyData(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(IntToPointer<CREATESTRUCTW>(lParam)->lpCreateParams));
        }
        auto* captured = IntToPointer<std::vector<std::byte>>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_COPYDATA && captured != nullptr) {
            const auto* data = IntToPointer<const COPYDATASTRUCT>(lParam);
            if (data->dwData == kNotifyIconCopyData || data->dwData == kIconRectCopyData) {
                const auto* bytes = static_cast<const std::byte*>(data->lpData);
                captured->assign(bytes, bytes + data->cbData);
            }
            // A size of 1 by 0 to an icon rect query, so shell32 goes on to ask for the corner.
            return TRUE;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    // A monitor above the primary has negative coordinates.
    constexpr RECT   kAbovePrimary{.left = 100, .top = -2100, .right = 130, .bottom = -2070};

    // Answers every icon rect query with kAbovePrimary, as the tray answers one.
    LRESULT CALLBACK AnswerAbovePrimary(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_COPYDATA) {
            const auto* data = IntToPointer<const COPYDATASTRUCT>(lParam);
            if (data->dwData == kIconRectCopyData) {
                return IconRectAnswer(ParseIconRectQuery(std::span(static_cast<const std::byte*>(data->lpData), data->cbData)).part, kAbovePrimary);
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    // Runs call on a desktop of the test's own, where no Explorer runs, with a taskbar window of the given procedure.
    void RunWithTaskbar(WNDPROC procedure, void* state, const std::function<void(HWND taskbar)>& call) {
        std::thread([procedure, state, &call] {
            HDESK desktop = CreateDesktopW(L"intarsia-tray-test", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
            if (desktop == nullptr || !SetThreadDesktop(desktop)) {
                return;
            }
            HINSTANCE   instance = GetModuleHandleW(nullptr);
            WNDCLASSEXW registration{};
            registration.cbSize        = sizeof(registration);
            registration.lpfnWndProc   = procedure;
            registration.hInstance     = instance;
            registration.lpszClassName = kTaskbarClass;
            RegisterClassExW(&registration);
            HWND taskbar = CreateWindowExW(WS_EX_TOOLWINDOW, kTaskbarClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, state);
            call(taskbar);
            DestroyWindow(taskbar);
            UnregisterClassW(kTaskbarClass, instance);
            CloseDesktop(desktop);
        }).join();
    }

    // Runs call on a desktop of the test's own, where no Explorer runs, with a taskbar window that captures what shell32
    // sends it.
    std::vector<std::byte> CaptureFromShell32(const std::function<void(HWND taskbar)>& call) {
        std::vector<std::byte> captured;
        RunWithTaskbar(CaptureCopyData, &captured, call);
        return captured;
    }

} // namespace

TEST(ShellNotifyIcon, Shell32SendsTheLayoutTheParserReads) {
    bool                         added    = false;
    const std::vector<std::byte> captured = CaptureFromShell32([&added](HWND taskbar) {
        NOTIFYICONDATAW icon{};
        icon.cbSize           = NOTIFYICONDATAW_V2_SIZE;
        icon.hWnd             = taskbar;
        icon.uID              = 42;
        icon.uFlags           = NIF_MESSAGE | NIF_TIP | NIF_STATE;
        icon.uCallbackMessage = kCallback;
        icon.dwState          = NIS_HIDDEN;
        icon.dwStateMask      = NIS_HIDDEN;
        std::memcpy(icon.szTip, L"from shell32", sizeof(L"from shell32"));
        added = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
    });

    ASSERT_TRUE(added);
    const NotifyIconMessage message = ParseNotifyIconMessage(captured);
    EXPECT_EQ(message.command, static_cast<std::uint32_t>(NIM_ADD));
    EXPECT_EQ(message.id, 42u);
    EXPECT_EQ(message.flags, static_cast<std::uint32_t>(NIF_MESSAGE | NIF_TIP | NIF_STATE));
    EXPECT_EQ(message.callbackMessage, kCallback);
    EXPECT_EQ(message.tip, L"from shell32");
    EXPECT_EQ(message.state, static_cast<std::uint32_t>(NIS_HIDDEN));
    EXPECT_EQ(message.stateMask, static_cast<std::uint32_t>(NIS_HIDDEN));
}

TEST(ShellNotifyIcon, Shell32SendsTheIconRectQueryTheParserReads) {
    HRESULT                      result   = E_FAIL;
    std::uintptr_t               owner    = 0;
    const std::vector<std::byte> captured = CaptureFromShell32([&result, &owner](HWND taskbar) {
        owner = reinterpret_cast<std::uintptr_t>(taskbar);
        NOTIFYICONIDENTIFIER icon{};
        icon.cbSize = sizeof(icon);
        icon.hWnd   = taskbar;
        icon.uID    = 42;
        RECT rect{};
        result = Shell_NotifyIconGetRect(&icon, &rect);
    });

    EXPECT_EQ(result, S_OK);
    const IconRectQuery query = ParseIconRectQuery(captured);
    // The size came first, so the corner is the last thing asked.
    EXPECT_EQ(query.part, IconRectPart::Position);
    EXPECT_EQ(static_cast<std::uintptr_t>(query.owner), owner);
    EXPECT_EQ(query.id, 42u);
    EXPECT_EQ(query.guid, GUID{});
}

TEST(ShellNotifyIcon, Shell32ReadsAnIconRectAboveThePrimaryMonitor) {
    HRESULT result = E_FAIL;
    RECT    rect{};
    RunWithTaskbar(AnswerAbovePrimary, nullptr, [&result, &rect](HWND taskbar) {
        NOTIFYICONIDENTIFIER icon{};
        icon.cbSize = sizeof(icon);
        icon.hWnd   = taskbar;
        icon.uID    = 42;
        result      = Shell_NotifyIconGetRect(&icon, &rect);
    });

    EXPECT_EQ(result, S_OK);
    EXPECT_EQ(rect.left, kAbovePrimary.left);
    EXPECT_EQ(rect.top, kAbovePrimary.top);
    EXPECT_EQ(rect.right, kAbovePrimary.right);
    EXPECT_EQ(rect.bottom, kAbovePrimary.bottom);
}

TEST_F(ShellNotifyIconTray, AnAddedIconShowsWithItsPixelsAndTooltip) {
    ASSERT_NE(_window, nullptr);
    std::vector<std::byte> add = AddPayload(_owner, 1, LoadIconW(nullptr, IDI_APPLICATION));
    EXPECT_EQ(Send(_window, kNotifyIconCopyData, add), TRUE);

    const std::vector<TrayIcon> icons = _tray.Icons();
    ASSERT_EQ(icons.size(), 1u);
    EXPECT_EQ(icons[0].owner, reinterpret_cast<std::intptr_t>(_owner));
    EXPECT_EQ(icons[0].tooltip, L"tip");
    ASSERT_NE(icons[0].pixels, nullptr);
    EXPECT_GT(icons[0].pixels->width, 0u);
}

TEST_F(ShellNotifyIconTray, AnIconHandleThatDoesNotReadLeavesTheIconWithoutPixels) {
    std::vector<std::byte> add = AddPayload(_owner, 1, IntToPointer<std::remove_pointer_t<HICON>>(0x7FFF1234));
    EXPECT_EQ(Send(_window, kNotifyIconCopyData, add), TRUE);
    ASSERT_EQ(_tray.Icons().size(), 1u);
    EXPECT_EQ(_tray.Icons()[0].pixels, nullptr);
}

TEST_F(ShellNotifyIconTray, MalformedMessagesAreRefusedAndTheTrayKeepsWorking) {
    std::vector<std::byte> tooShort(100);
    EXPECT_EQ(Send(_window, kNotifyIconCopyData, tooShort), FALSE);
    std::vector<std::byte> wrongSignature = AddPayload(_owner, 1, nullptr);
    wrongSignature[0]                     = std::byte{0};
    EXPECT_EQ(Send(_window, kNotifyIconCopyData, wrongSignature), FALSE);
    std::vector<std::byte> empty;
    EXPECT_EQ(Send(_window, kNotifyIconCopyData, empty), FALSE);
    EXPECT_TRUE(_tray.Icons().empty());

    std::vector<std::byte> add = AddPayload(_owner, 1, nullptr);
    EXPECT_EQ(Send(_window, kNotifyIconCopyData, add), TRUE);
    EXPECT_EQ(_tray.Icons().size(), 1u);
}

TEST_F(ShellNotifyIconTray, AppBarMessagesAreNotTrayIcons) {
    // With no Explorer taskbar of this class to forward to, the answer is Explorer's absence.
    std::vector<std::byte> appBar(64);
    EXPECT_EQ(Send(_window, kAppBarCopyData, appBar), FALSE);
    EXPECT_TRUE(_tray.Icons().empty());
}

TEST_F(ShellNotifyIconTray, AnIconRectQueryIsAnsweredWithWhereABarShowsTheIcon) {
    std::vector<std::byte> add = AddPayload(_owner, 4, nullptr);
    Send(_window, kNotifyIconCopyData, add);
    const std::uint8_t view = 0;
    _tray.ShowRects(&view, {TrayIconRect{.owner = reinterpret_cast<std::intptr_t>(_owner), .id = 4, .rect = RECT{.left = -40, .top = 10, .right = -10, .bottom = 50}}});

    NotifyIconIdentifier32 raw{.signature = kTrayNotifySignature, .part = 2, .size = 32, .padding = 0, .window = Handle32(_owner), .id = 4, .guid = GUID{}};
    std::vector<std::byte> size(sizeof(raw));
    std::memcpy(size.data(), &raw, sizeof(raw));
    EXPECT_EQ(Send(_window, kIconRectCopyData, size), MAKELONG(30, 40));
    raw.part = 1;
    std::vector<std::byte> position(sizeof(raw));
    std::memcpy(position.data(), &raw, sizeof(raw));
    EXPECT_EQ(static_cast<std::int16_t>(LOWORD(Send(_window, kIconRectCopyData, position))), -40);

    // Once no bar shows it, the answer is none, which shell32 gives the app as E_FAIL.
    _tray.ShowRects(&view, {});
    EXPECT_EQ(Send(_window, kIconRectCopyData, size), 0);
    std::vector<std::byte> malformed(8);
    EXPECT_EQ(Send(_window, kIconRectCopyData, malformed), 0);
}

TEST_F(ShellNotifyIconTray, AFloodOfIconsStopsAtTheCap) {
    for (std::uint32_t id = 0; id < kMaxTrayIcons + 50; ++id) {
        std::vector<std::byte> add = AddPayload(_owner, id, nullptr);
        EXPECT_EQ(Send(_window, kNotifyIconCopyData, add), id < kMaxTrayIcons ? TRUE : FALSE);
    }
    EXPECT_EQ(_tray.Icons().size(), kMaxTrayIcons);
}

TEST_F(ShellNotifyIconTray, IconsOfAClosedWindowAreDropped) {
    HWND                   closing = MessageWindow();
    std::vector<std::byte> gone    = AddPayload(closing, 1, nullptr);
    std::vector<std::byte> stays   = AddPayload(_owner, 2, nullptr);
    Send(_window, kNotifyIconCopyData, gone);
    Send(_window, kNotifyIconCopyData, stays);
    ASSERT_EQ(_tray.Icons().size(), 2u);
    DestroyWindow(closing);
    EXPECT_TRUE(Eventually([this] { return _tray.Icons().size() == 1; }, std::chrono::seconds(7)));
    EXPECT_EQ(_tray.Icons()[0].id, 2u);
}

TEST_F(ShellNotifyIconTray, PressPostsTheClickToTheIconsOwner) {
    std::vector<std::byte> add = AddPayload(_owner, 3, nullptr);
    Send(_window, kNotifyIconCopyData, add);
    _tray.Press(_tray.Icons().at(0), TrayButton::Left);

    std::vector<LPARAM> events;
    MSG                 message{};
    while (PeekMessageW(&message, _owner, kCallback, kCallback, PM_REMOVE)) {
        EXPECT_EQ(message.wParam, 3u);
        events.push_back(message.lParam);
    }
    EXPECT_EQ(events, (std::vector<LPARAM>{WM_LBUTTONDOWN, WM_LBUTTONUP}));
}

TEST_F(ShellNotifyIconTray, StaysFirstOfItsClassAndOutlivesWmClose) {
    // Another window of the class rises above it, as Explorer's taskbar does when it activates.
    HWND rival = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kTestClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ASSERT_NE(rival, nullptr);
    ASSERT_TRUE(SetWindowPos(rival, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
    EXPECT_TRUE(Eventually([this] { return FindWindowW(kTestClass, nullptr) == _window; }, std::chrono::seconds(2)));
    DestroyWindow(rival);

    SendMessageW(_window, WM_CLOSE, 0, 0);
    EXPECT_TRUE(IsWindow(_window));
}
