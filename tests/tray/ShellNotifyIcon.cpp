#include "tray/ShellNotifyIcon.hpp"

#include "platform/AppBar.hpp"
#include "platform/Text.hpp"
#include "platform/Win32Window.hpp"
#include "support/MessageWindow.hpp"
#include "support/Rect.hpp"
#include "support/TrayPayload.hpp"

#include <shellapi.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

    // Windows 11 sends this many bytes for every call, the layout followed by bytes nothing reads.
    constexpr size_t kSentBytes = 1484;
    // {6B29FC40-CA47-1067-B31D-00DD010662DA}
    constexpr GUID              kGuid{0x6B29FC40, 0xCA47, 0x1067, {0xB3, 0x1D, 0x00, 0xDD, 0x01, 0x06, 0x62, 0xDA}};
    constexpr std::wstring_view kAppImage   = L"C:\\Program Files\\App\\app.exe";
    constexpr std::wstring_view kOtherImage = L"C:\\Users\\someone\\other.exe";

    std::vector<std::byte>      Bytes(const TrayNotifyData& data, size_t size) {
        std::vector<std::byte> bytes(size);
        std::memcpy(bytes.data(), &data, std::min(size, sizeof(data)));
        return bytes;
    }

    NotifyIconMessage Message(std::uint32_t command, std::uint32_t owner, std::uint32_t id, std::uint32_t flags) {
        return ParseNotifyIconMessage(Bytes(NotifyData(command, owner, id, flags, 0), kSentBytes));
    }

    std::shared_ptr<const IconPixels> Pixels(std::uint8_t value) {
        return std::make_shared<const IconPixels>(IconPixels{
            .width  = 1,
            .height = 1,
            .pbgra  = {value, value, value, 255},
        });
    }

    // The icons after a message from kAppImage the tray must take. A refusal fails the test and leaves no icons.
    std::vector<TrayIcon> Applied(const std::vector<TrayIcon>& icons, const NotifyIconMessage& message, std::shared_ptr<const IconPixels> pixels) {
        std::optional<std::vector<TrayIcon>> applied = ApplyNotifyIcon(icons, message, kAppImage, std::move(pixels));
        if (!applied.has_value()) {
            ADD_FAILURE() << "the tray refused command " << message.command;
            return {};
        }
        return *applied;
    }

    TrayIcon Icon(std::intptr_t owner, std::uint32_t id) {
        return TrayIcon{
            .owner           = owner,
            .id              = id,
            .guid            = std::nullopt,
            .ownerImage      = {},
            .callbackMessage = WM_APP,
            .version         = 0,
            .tooltip         = {},
            .standardTooltip = false,
            .pixels          = nullptr,
            .hidden          = false,
        };
    }

    std::vector<std::byte> QueryBytes(std::uint32_t part, std::uint32_t owner, std::uint32_t id, const GUID& guid) {
        const NotifyIconIdentifier32 raw{
            .signature = kTrayNotifySignature,
            .part      = part,
            .size      = 32,
            .padding   = 0,
            .window    = owner,
            .id        = id,
            .guid      = guid,
        };
        std::vector<std::byte> bytes(sizeof(raw));
        std::memcpy(bytes.data(), &raw, sizeof(raw));
        return bytes;
    }

    IconRectQuery Query(std::intptr_t owner, std::uint32_t id, const GUID& guid) {
        return IconRectQuery{
            .part  = IconRectPart::Size,
            .owner = owner,
            .id    = id,
            .guid  = guid,
        };
    }

    TrayIconRect Shown(std::intptr_t owner, std::uint32_t id, RECT rect) {
        return TrayIconRect{
            .owner = owner,
            .id    = id,
            .rect  = rect,
        };
    }

    // A class no app sends to, so the test tray never takes the desktop's icons.
    constexpr wchar_t       kTestClass[] = L"IntarsiaTestTray";
    constexpr std::uint32_t kChanged     = WM_APP + 1;
    constexpr std::uint32_t kFailed      = WM_APP + 2;
    constexpr std::uint32_t kCallback    = WM_APP + 9;

    std::uint32_t           AddFlags(HICON icon) {
        if (icon == nullptr) {
            return NIF_MESSAGE | NIF_TIP;
        }
        return NIF_MESSAGE | NIF_TIP | NIF_ICON;
    }

    std::vector<std::byte> AddPayload(HWND owner, std::uint32_t id, HICON icon) {
        TrayNotifyData data = NotifyData(NIM_ADD, Handle32(owner), id, AddFlags(icon), kCallback);
        data.data.icon      = Handle32(icon);
        std::memcpy(data.data.tip.data(), L"tip", sizeof(L"tip"));
        return Bytes(data, sizeof(data));
    }

    LRESULT Send(HWND tray, ULONG_PTR kind, std::vector<std::byte>& bytes) {
        COPYDATASTRUCT data{
            .dwData = kind,
            .cbData = static_cast<DWORD>(bytes.size()),
            .lpData = bytes.data(),
        };
        DWORD_PTR result = 0;
        EXPECT_NE(SendMessageTimeoutW(tray, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_NORMAL, 5000, &result), 0);
        return static_cast<LRESULT>(result);
    }

    class ShellNotifyIconTray : public testing::Test {
      public:
        ShellNotifyIconTray() : _target(kChanged), _tray(_target.signal, kTestClass, _target.window.Get(), kFailed) {}

      protected:
        ChangeTarget    _target;
        MessageWindow   _owner;
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

    constexpr RECT Rect(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom) {
        return RECT{
            .left   = left,
            .top    = top,
            .right  = right,
            .bottom = bottom,
        };
    }

    // A monitor above the primary has negative coordinates.
    constexpr RECT kAbovePrimary = Rect(100, -2100, 130, -2070);

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

TEST(NotifyIconProtocol, ParseSignExtendsHandlesAsWindowsDoes) {
    TrayNotifyData data             = NotifyData(NIM_ADD, 0xFFFF1234, 1, 0, 0);
    data.data.icon                  = 0x80000001;
    const NotifyIconMessage message = ParseNotifyIconMessage(Bytes(data, sizeof(data)));
    EXPECT_EQ(message.owner, static_cast<std::intptr_t>(static_cast<std::int32_t>(0xFFFF1234)));
    EXPECT_EQ(message.icon, static_cast<std::intptr_t>(static_cast<std::int32_t>(0x80000001)));
}

TEST(NotifyIconProtocol, ATipWithNoNullStopsAtItsBuffer) {
    TrayNotifyData data = NotifyData(NIM_ADD, 1, 1, NIF_TIP, 0);
    data.data.tip.fill(L'x');
    data.data.state = 0xFFFFFFFF;
    EXPECT_EQ(ParseNotifyIconMessage(Bytes(data, kSentBytes)).tip, std::wstring(128, L'x'));
}

TEST(NotifyIconProtocol, AddModifyAndDeleteEachChangeTheTraysIcons) {
    NotifyIconMessage add             = Message(NIM_ADD, 5, 1, NIF_MESSAGE | NIF_TIP | NIF_ICON);
    add.callbackMessage               = WM_APP + 1;
    add.tip                           = L"first";
    const std::vector<TrayIcon> added = Applied({}, add, Pixels(1));
    ASSERT_EQ(added.size(), 1u);
    EXPECT_EQ(added[0].callbackMessage, static_cast<std::uint32_t>(WM_APP + 1));
    EXPECT_EQ(added[0].tooltip, L"first");
    ASSERT_NE(added[0].pixels, nullptr);

    // Only the tip is set, so the callback and pixels stay.
    NotifyIconMessage modify             = Message(NIM_MODIFY, 5, 1, NIF_TIP);
    modify.tip                           = L"second";
    const std::vector<TrayIcon> modified = Applied(added, modify, nullptr);
    ASSERT_EQ(modified.size(), 1u);
    EXPECT_EQ(modified[0].tooltip, L"second");
    EXPECT_EQ(modified[0].callbackMessage, static_cast<std::uint32_t>(WM_APP + 1));
    EXPECT_EQ(modified[0].pixels, added[0].pixels);

    const std::optional<std::vector<TrayIcon>> deleted = ApplyNotifyIcon(modified, Message(NIM_DELETE, 5, 1, 0), kAppImage, nullptr);
    EXPECT_EQ(deleted, std::vector<TrayIcon>{});
}

TEST(NotifyIconProtocol, AnIconTheTrayDoesNotHaveIsRefused) {
    const std::vector<TrayIcon> icons{Icon(5, 1)};
    for (const std::uint32_t command : {NIM_MODIFY, NIM_DELETE, NIM_SETVERSION, NIM_SETFOCUS}) {
        EXPECT_FALSE(ApplyNotifyIcon(icons, Message(command, 5, 2, 0), kAppImage, nullptr).has_value()) << command;
        EXPECT_FALSE(ApplyNotifyIcon(icons, Message(command, 6, 1, 0), kAppImage, nullptr).has_value()) << command;
    }
    EXPECT_FALSE(ApplyNotifyIcon(icons, Message(99, 5, 1, 0), kAppImage, nullptr).has_value());
}

TEST(NotifyIconProtocol, AddingAnIconAgainUpdatesIt) {
    NotifyIconMessage add             = Message(NIM_ADD, 5, 1, NIF_TIP);
    add.tip                           = L"again";
    const std::vector<TrayIcon> icons = Applied({Icon(5, 1)}, add, nullptr);
    ASSERT_EQ(icons.size(), 1u);
    EXPECT_EQ(icons[0].tooltip, L"again");
}

TEST(NotifyIconProtocol, AFullTrayRefusesNewIconsButNotChanges) {
    std::vector<TrayIcon> icons;
    icons.reserve(kMaxTrayIcons);
    for (std::uint32_t id = 0; id < kMaxTrayIcons; ++id) {
        icons.push_back(Icon(5, id));
    }
    EXPECT_FALSE(ApplyNotifyIcon(icons, Message(NIM_ADD, 5, 1000, 0), kAppImage, nullptr).has_value());
    EXPECT_TRUE(ApplyNotifyIcon(icons, Message(NIM_ADD, 5, 0, 0), kAppImage, nullptr).has_value());
    EXPECT_TRUE(ApplyNotifyIcon(icons, Message(NIM_MODIFY, 5, 3, 0), kAppImage, nullptr).has_value());
}

TEST(NotifyIconProtocol, AGuidIdentifiesAnIconWhateverItsOwner) {
    NotifyIconMessage add             = Message(NIM_ADD, 5, 1, NIF_GUID);
    add.guid                          = kGuid;
    const std::vector<TrayIcon> added = Applied({}, add, nullptr);

    // The app's window was recreated: the GUID still finds the icon, which follows the new window.
    NotifyIconMessage modify             = Message(NIM_MODIFY, 9, 4, NIF_GUID | NIF_TIP);
    modify.guid                          = kGuid;
    modify.tip                           = L"moved";
    const std::vector<TrayIcon> modified = Applied(added, modify, nullptr);
    ASSERT_EQ(modified.size(), 1u);
    EXPECT_EQ(modified[0].owner, 9);
    EXPECT_EQ(modified[0].tooltip, L"moved");
}

TEST(NotifyIconProtocol, AGuidStaysWithTheExecutableThatGaveIt) {
    NotifyIconMessage add             = Message(NIM_ADD, 5, 1, NIF_GUID | NIF_MESSAGE);
    add.guid                          = kGuid;
    const std::vector<TrayIcon> added = Applied({}, add, nullptr);

    // Another app naming the GUID from its own window would get the icon's clicks, or remove it.
    for (const std::uint32_t command : {NIM_ADD, NIM_MODIFY, NIM_DELETE, NIM_SETVERSION}) {
        NotifyIconMessage takeover = Message(command, 9, 1, NIF_GUID | NIF_MESSAGE);
        takeover.guid              = kGuid;
        EXPECT_FALSE(ApplyNotifyIcon(added, takeover, kOtherImage, nullptr).has_value()) << command;
    }

    // Paths compare as Windows compares them, ignoring case.
    NotifyIconMessage modify = Message(NIM_MODIFY, 9, 1, NIF_GUID);
    modify.guid              = kGuid;
    EXPECT_TRUE(ApplyNotifyIcon(added, modify, L"c:\\program files\\app\\APP.EXE", nullptr).has_value());
}

TEST(NotifyIconProtocol, StateChangesOnlyTheBitsItsMaskNames) {
    NotifyIconMessage hide             = Message(NIM_MODIFY, 5, 1, NIF_STATE);
    hide.state                         = NIS_HIDDEN;
    hide.stateMask                     = NIS_HIDDEN;
    const std::vector<TrayIcon> hidden = Applied({Icon(5, 1)}, hide, nullptr);
    ASSERT_EQ(hidden.size(), 1u);
    EXPECT_TRUE(hidden[0].hidden);

    NotifyIconMessage unmasked        = Message(NIM_MODIFY, 5, 1, NIF_STATE);
    unmasked.state                    = 0;
    unmasked.stateMask                = NIS_SHAREDICON;
    const std::vector<TrayIcon> still = Applied(hidden, unmasked, nullptr);
    ASSERT_EQ(still.size(), 1u);
    EXPECT_TRUE(still[0].hidden);
}

TEST(NotifyIconProtocol, SetVersionIsKeptPerIcon) {
    NotifyIconMessage version         = Message(NIM_SETVERSION, 5, 1, 0);
    version.version                   = NOTIFYICON_VERSION_4;
    const std::vector<TrayIcon> icons = Applied({Icon(5, 1), Icon(5, 2)}, version, nullptr);
    ASSERT_EQ(icons.size(), 2u);
    EXPECT_EQ(icons[0].version, static_cast<std::uint32_t>(NOTIFYICON_VERSION_4));
    EXPECT_EQ(icons[1].version, 0u);
}

TEST(NotifyIconProtocol, ShowTipIsReadOnAddAndOnModifiesThatSetTheTip) {
    const std::vector<TrayIcon> added = Applied({}, Message(NIM_ADD, 5, 1, NIF_TIP | NIF_SHOWTIP), nullptr);
    ASSERT_EQ(added.size(), 1u);
    EXPECT_TRUE(added[0].standardTooltip);
    // Swapping only the icon keeps the standard tooltip.
    const std::vector<TrayIcon> iconOnly = Applied(added, Message(NIM_MODIFY, 5, 1, NIF_ICON), nullptr);
    ASSERT_EQ(iconOnly.size(), 1u);
    EXPECT_TRUE(iconOnly[0].standardTooltip);
    const std::vector<TrayIcon> ownTip = Applied(added, Message(NIM_MODIFY, 5, 1, NIF_TIP), nullptr);
    ASSERT_EQ(ownTip.size(), 1u);
    EXPECT_FALSE(ownTip[0].standardTooltip);
}

TEST(NotifyIconProtocol, TheSamePixelsKeepTheirPointer) {
    TrayIcon icon                    = Icon(5, 1);
    icon.pixels                      = Pixels(7);
    const std::vector<TrayIcon> same = Applied({icon}, Message(NIM_MODIFY, 5, 1, NIF_ICON), Pixels(7));
    ASSERT_EQ(same.size(), 1u);
    EXPECT_EQ(same[0].pixels, icon.pixels);
    const std::vector<TrayIcon> other = Applied({icon}, Message(NIM_MODIFY, 5, 1, NIF_ICON), Pixels(8));
    ASSERT_EQ(other.size(), 1u);
    EXPECT_NE(other[0].pixels, icon.pixels);
    // An icon that did not read clears the old one.
    const std::vector<TrayIcon> cleared = Applied({icon}, Message(NIM_MODIFY, 5, 1, NIF_ICON), nullptr);
    ASSERT_EQ(cleared.size(), 1u);
    EXPECT_EQ(cleared[0].pixels, nullptr);
}

TEST(NotifyIconProtocol, DrawableTooltipKeepsLineBreaksAndReplacesTheUnsafe) {
    EXPECT_EQ(DrawableTooltip(L"a\r\nb"), L"a\nb");
    // The override is spelled as a number: MSVC refuses an unterminated one in a literal (C5255).
    EXPECT_EQ(DrawableTooltip(std::wstring{L'a', static_cast<wchar_t>(0x202E), static_cast<wchar_t>(0xD800), L'b'}),
              (std::wstring{L'a', kReplacementCharacter, kReplacementCharacter, L'b'}));
}

TEST(NotifyIconProtocol, VersionZeroPostsTheIdAndTheMouseMessage) {
    const std::vector<TrayInputMessage> messages = TrayInputMessages(Icon(5, 7), ClickEvents(TrayButton::Left, 0), POINT{.x = 10, .y = 20});
    EXPECT_EQ(messages,
              (std::vector<TrayInputMessage>{{
                                                 .message = WM_APP,
                                                 .wParam  = 7,
                                                 .lParam  = WM_LBUTTONDOWN,
                                             },
                                             {
                                                 .message = WM_APP,
                                                 .wParam  = 7,
                                                 .lParam  = WM_LBUTTONUP,
                                             }}));
}

TEST(NotifyIconProtocol, VersionThreeAddsSelectAndContextMenu) {
    EXPECT_EQ(ClickEvents(TrayButton::Left, NOTIFYICON_VERSION), (std::vector<std::uint32_t>{WM_LBUTTONDOWN, WM_LBUTTONUP, NIN_SELECT}));
    EXPECT_EQ(ClickEvents(TrayButton::Right, NOTIFYICON_VERSION), (std::vector<std::uint32_t>{WM_RBUTTONDOWN, WM_RBUTTONUP, WM_CONTEXTMENU}));
}

TEST(NotifyIconProtocol, AMiddleClickIsItsButtonAloneEvenInVersionFour) {
    EXPECT_EQ(ClickEvents(TrayButton::Middle, NOTIFYICON_VERSION_4), (std::vector<std::uint32_t>{WM_MBUTTONDOWN, WM_MBUTTONUP}));
}

TEST(NotifyIconProtocol, TheSecondClickOfADoubleClickIsTheDoubleClickAndUp) {
    EXPECT_EQ(DoubleClickEvents(TrayButton::Left), (std::vector<std::uint32_t>{WM_LBUTTONDBLCLK, WM_LBUTTONUP}));
    EXPECT_EQ(DoubleClickEvents(TrayButton::Right), (std::vector<std::uint32_t>{WM_RBUTTONDBLCLK, WM_RBUTTONUP}));
    EXPECT_EQ(DoubleClickEvents(TrayButton::Middle), (std::vector<std::uint32_t>{WM_MBUTTONDBLCLK, WM_MBUTTONUP}));
}

TEST(NotifyIconProtocol, ADoubleClickIsTheSameButtonOnTheSameIconSoonAndNear) {
    const auto      start = std::chrono::steady_clock::now();
    const TrayClick first{
        .owner  = 5,
        .id     = 7,
        .button = TrayButton::Left,
        .cursor = POINT{.x = 100, .y = 10},
        .time   = start,
    };
    const auto second = [&first](std::chrono::milliseconds after, POINT cursor) {
        TrayClick click = first;
        click.cursor    = cursor;
        click.time      = first.time + after;
        return click;
    };
    const std::chrono::milliseconds doubleTime(500);
    const SIZE                      area{.cx = 4, .cy = 4};
    EXPECT_TRUE(IsDoubleClick(first, second(std::chrono::milliseconds(200), POINT{.x = 102, .y = 8}), doubleTime, area));
    EXPECT_FALSE(IsDoubleClick(std::nullopt, first, doubleTime, area));
    EXPECT_FALSE(IsDoubleClick(first, second(std::chrono::milliseconds(501), POINT{.x = 100, .y = 10}), doubleTime, area));
    EXPECT_FALSE(IsDoubleClick(first, second(std::chrono::milliseconds(200), POINT{.x = 103, .y = 10}), doubleTime, area));
    TrayClick other = second(std::chrono::milliseconds(200), first.cursor);
    other.id        = 8;
    EXPECT_FALSE(IsDoubleClick(first, other, doubleTime, area));
    other        = second(std::chrono::milliseconds(200), first.cursor);
    other.button = TrayButton::Right;
    EXPECT_FALSE(IsDoubleClick(first, other, doubleTime, area));
}

TEST(NotifyIconProtocol, VersionFourPostsTheCursorAndPacksTheIdWithTheEvent) {
    TrayIcon icon = Icon(5, 7);
    icon.version  = NOTIFYICON_VERSION_4;
    // A monitor left of the primary has negative coordinates, which travel as 16-bit two's complement.
    const std::vector<TrayInputMessage> messages = TrayInputMessages(icon, ClickEvents(TrayButton::Right, icon.version), POINT{.x = -100, .y = 40});
    ASSERT_EQ(messages.size(), 3u);
    for (const TrayInputMessage& message : messages) {
        EXPECT_EQ(static_cast<std::int16_t>(LOWORD(message.wParam)), -100);
        EXPECT_EQ(HIWORD(message.wParam), 40);
    }
    EXPECT_EQ(messages[2].lParam, MAKELPARAM(WM_CONTEXTMENU, 7));
}

TEST(NotifyIconProtocol, ACallbackBelowWmUserPostsNothing) {
    TrayIcon icon        = Icon(5, 7);
    icon.callbackMessage = WM_CLOSE;
    EXPECT_TRUE(TrayInputMessages(icon, ClickEvents(TrayButton::Left, 0), POINT{.x = 0, .y = 0}).empty());
    icon.callbackMessage = 0;
    EXPECT_TRUE(TrayInputMessages(icon, {WM_MOUSEMOVE}, POINT{.x = 0, .y = 0}).empty());
}

TEST(NotifyIconProtocol, ParseIconRectQueryRefusesWhatIsNotAQuery) {
    std::vector<std::byte> tooShort = QueryBytes(2, 5, 7, GUID{});
    tooShort.pop_back();
    EXPECT_THROW(ParseIconRectQuery(tooShort), TrayMessageError);
    std::vector<std::byte> wrongSignature = QueryBytes(2, 5, 7, GUID{});
    wrongSignature[0]                     = std::byte{0};
    EXPECT_THROW(ParseIconRectQuery(wrongSignature), TrayMessageError);
    EXPECT_THROW(ParseIconRectQuery(QueryBytes(0, 5, 7, GUID{})), TrayMessageError);
}

TEST(NotifyIconProtocol, AnIconShownOnTwoBarsAnswersWithTheOneNearestTheCursor) {
    const RECT                      leftBarIconRect  = Rect(-1000, 0, -970, 30);
    const RECT                      rightBarIconRect = Rect(1500, 0, 1530, 30);
    const std::vector<TrayIcon>     icons{Icon(5, 7)};
    const std::vector<TrayIconRect> rects{Shown(5, 7, leftBarIconRect), Shown(5, 8, Rect(0, 0, 30, 30)), Shown(5, 7, rightBarIconRect)};
    EXPECT_EQ(QueriedIconRect(Query(5, 7, GUID{}), icons, rects, POINT{.x = 1510, .y = 10}), rightBarIconRect);
    EXPECT_EQ(QueriedIconRect(Query(5, 7, GUID{}), icons, rects, POINT{.x = -500, .y = 400}), leftBarIconRect);
}

TEST(NotifyIconProtocol, AnIconWithAGuidIsFoundByIt) {
    TrayIcon icon       = Icon(5, 7);
    icon.guid           = kGuid;
    const RECT iconRect = Rect(10, 0, 40, 30);
    EXPECT_EQ(QueriedIconRect(Query(0, 0, kGuid), {icon}, {Shown(5, 7, iconRect)}, POINT{.x = 0, .y = 0}), iconRect);
}

TEST(NotifyIconProtocol, AnIconNoBarShowsHasNoRect) {
    const std::vector<TrayIconRect> rects{Shown(5, 8, Rect(0, 0, 30, 30))};
    // Folded away, an icon the tray never had, and a GUID no icon has.
    EXPECT_FALSE(QueriedIconRect(Query(5, 7, GUID{}), {Icon(5, 7)}, rects, POINT{.x = 0, .y = 0}).has_value());
    EXPECT_FALSE(QueriedIconRect(Query(5, 8, GUID{}), {Icon(5, 7)}, rects, POINT{.x = 0, .y = 0}).has_value());
    EXPECT_FALSE(QueriedIconRect(Query(5, 8, kGuid), {Icon(5, 8)}, rects, POINT{.x = 0, .y = 0}).has_value());
}

TEST(ShellNotifyIcon, Shell32SendsTheLayoutTheParserReads) {
    HICON                        iconHandle = LoadIconW(nullptr, IDI_APPLICATION);
    bool                         added      = false;
    const std::vector<std::byte> captured   = CaptureFromShell32([&added, iconHandle](HWND taskbar) {
        NOTIFYICONDATAW icon{};
        icon.cbSize           = NOTIFYICONDATAW_V2_SIZE;
        icon.hWnd             = taskbar;
        icon.uID              = 42;
        icon.uFlags           = NIF_MESSAGE | NIF_TIP | NIF_STATE | NIF_ICON;
        icon.uCallbackMessage = kCallback;
        icon.hIcon            = iconHandle;
        icon.dwState          = NIS_HIDDEN;
        icon.dwStateMask      = NIS_HIDDEN;
        std::memcpy(icon.szTip, L"from shell32", sizeof(L"from shell32"));
        added = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
    });

    ASSERT_TRUE(added);
    const NotifyIconMessage message = ParseNotifyIconMessage(captured);
    EXPECT_EQ(message.command, static_cast<std::uint32_t>(NIM_ADD));
    EXPECT_EQ(message.id, 42u);
    EXPECT_EQ(message.flags, static_cast<std::uint32_t>(NIF_MESSAGE | NIF_TIP | NIF_STATE | NIF_ICON));
    EXPECT_EQ(message.callbackMessage, kCallback);
    EXPECT_EQ(message.icon, static_cast<std::intptr_t>(static_cast<std::int32_t>(Handle32(iconHandle))));
    EXPECT_EQ(message.tip, L"from shell32");
    EXPECT_EQ(message.state, static_cast<std::uint32_t>(NIS_HIDDEN));
    EXPECT_EQ(message.stateMask, static_cast<std::uint32_t>(NIS_HIDDEN));
}

TEST(ShellNotifyIcon, Shell32SendsTheVersionTheParserReads) {
    bool                         set      = false;
    const std::vector<std::byte> captured = CaptureFromShell32([&set](HWND taskbar) {
        NOTIFYICONDATAW icon{};
        icon.cbSize   = NOTIFYICONDATAW_V2_SIZE;
        icon.hWnd     = taskbar;
        icon.uID      = 42;
        icon.uVersion = NOTIFYICON_VERSION_4;
        set           = Shell_NotifyIconW(NIM_SETVERSION, &icon) != FALSE;
    });

    ASSERT_TRUE(set);
    const NotifyIconMessage message = ParseNotifyIconMessage(captured);
    EXPECT_EQ(message.command, static_cast<std::uint32_t>(NIM_SETVERSION));
    EXPECT_EQ(message.id, 42u);
    EXPECT_EQ(message.version, static_cast<std::uint32_t>(NOTIFYICON_VERSION_4));
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
    EXPECT_EQ(rect, kAbovePrimary);
}

TEST_F(ShellNotifyIconTray, AnIconHandleThatDoesNotReadLeavesTheIconWithoutPixels) {
    std::vector<std::byte> add = AddPayload(_owner.Get(), 1, ToIcon(0x7FFF1234));
    EXPECT_EQ(Send(_window, kNotifyIconCopyData, add), TRUE);
    ASSERT_EQ(_tray.Icons().size(), 1u);
    EXPECT_EQ(_tray.Icons()[0].pixels, nullptr);
}

TEST_F(ShellNotifyIconTray, MalformedMessagesAreRefusedAndTheTrayKeepsWorking) {
    std::vector<std::byte> tooShort(100);
    EXPECT_EQ(Send(_window, kNotifyIconCopyData, tooShort), FALSE);
    std::vector<std::byte> wrongSignature = AddPayload(_owner.Get(), 1, nullptr);
    wrongSignature[0]                     = std::byte{0};
    EXPECT_EQ(Send(_window, kNotifyIconCopyData, wrongSignature), FALSE);
    // An icon rect query too short to read has no rect, which shell32 gives the app as E_FAIL.
    std::vector<std::byte> shortQuery(8);
    EXPECT_EQ(Send(_window, kIconRectCopyData, shortQuery), 0);
    EXPECT_TRUE(_tray.Icons().empty());

    std::vector<std::byte> add = AddPayload(_owner.Get(), 1, nullptr);
    EXPECT_EQ(Send(_window, kNotifyIconCopyData, add), TRUE);
    EXPECT_EQ(_tray.Icons().size(), 1u);
}

TEST_F(ShellNotifyIconTray, AppBarMessagesAreNotTrayIcons) {
    // An icon's payload under the appbar kind goes only to Explorer, and with no taskbar of this class the answer is its
    // absence.
    std::vector<std::byte> appBar = AddPayload(_owner.Get(), 1, nullptr);
    EXPECT_EQ(Send(_window, kAppBarCopyData, appBar), FALSE);
    EXPECT_TRUE(_tray.Icons().empty());
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
