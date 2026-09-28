#include "tray/NotifyIconProtocol.hpp"

#include <shellapi.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

    // Windows 11 sends this many bytes for every call, the layout followed by bytes nothing reads.
    constexpr size_t kSentBytes = 1484;
    // {6B29FC40-CA47-1067-B31D-00DD010662DA}
    constexpr GUID kGuid{0x6B29FC40, 0xCA47, 0x1067, {0xB3, 0x1D, 0x00, 0xDD, 0x01, 0x06, 0x62, 0xDA}};

    TrayNotifyData Data(std::uint32_t command, std::uint32_t owner, std::uint32_t id, std::uint32_t flags) {
        TrayNotifyData data{};
        data.signature   = kTrayNotifySignature;
        data.command     = command;
        data.data.size   = sizeof(NotifyIconData32);
        data.data.window = owner;
        data.data.id     = id;
        data.data.flags  = flags;
        return data;
    }

    std::vector<std::byte> Bytes(const TrayNotifyData& data, size_t size) {
        std::vector<std::byte> bytes(size);
        std::memcpy(bytes.data(), &data, std::min(size, sizeof(data)));
        return bytes;
    }

    NotifyIconMessage Message(std::uint32_t command, std::uint32_t owner, std::uint32_t id, std::uint32_t flags) {
        return ParseNotifyIconMessage(Bytes(Data(command, owner, id, flags), kSentBytes));
    }

    std::shared_ptr<const IconPixels> Pixels(std::uint8_t value) {
        return std::make_shared<const IconPixels>(IconPixels{.width = 1, .height = 1, .pbgra = {value, value, value, 255}});
    }

    // The icons after a message the tray must take. A refusal fails the test and leaves no icons.
    std::vector<TrayIcon> Applied(const std::vector<TrayIcon>& icons, const NotifyIconMessage& message, std::shared_ptr<const IconPixels> pixels) {
        std::optional<std::vector<TrayIcon>> applied = ApplyNotifyIcon(icons, message, std::move(pixels));
        if (!applied.has_value()) {
            ADD_FAILURE() << "the tray refused command " << message.command;
            return {};
        }
        return *applied;
    }

    TrayIcon Icon(std::intptr_t owner, std::uint32_t id) {
        return TrayIcon{
            .owner = owner, .id = id, .guid = std::nullopt, .callbackMessage = WM_APP, .version = 0, .tooltip = {}, .standardTooltip = false, .pixels = nullptr, .hidden = false};
    }

    std::vector<std::byte> QueryBytes(std::uint32_t part, std::uint32_t owner, std::uint32_t id, const GUID& guid) {
        const NotifyIconIdentifier32 raw{.signature = kTrayNotifySignature, .part = part, .size = 32, .padding = 0, .window = owner, .id = id, .guid = guid};
        std::vector<std::byte>       bytes(sizeof(raw));
        std::memcpy(bytes.data(), &raw, sizeof(raw));
        return bytes;
    }

    IconRectQuery Query(std::intptr_t owner, std::uint32_t id, const GUID& guid) {
        return IconRectQuery{.part = IconRectPart::Size, .owner = owner, .id = id, .guid = guid};
    }

    TrayIconRect Shown(std::intptr_t owner, std::uint32_t id, RECT rect) {
        return TrayIconRect{.owner = owner, .id = id, .rect = rect};
    }

    bool SameRect(const std::optional<RECT>& rect, const RECT& expected) {
        return rect.has_value() && rect->left == expected.left && rect->top == expected.top && rect->right == expected.right && rect->bottom == expected.bottom;
    }

} // namespace

TEST(NotifyIconProtocol, ParseReadsTheFieldsShell32Sends) {
    TrayNotifyData data       = Data(NIM_MODIFY, 0x0003A0D9, 7, NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_STATE);
    data.data.callbackMessage = WM_APP + 9;
    data.data.icon            = 0x00010033;
    data.data.state           = NIS_HIDDEN;
    data.data.stateMask       = NIS_HIDDEN;
    data.data.version         = NOTIFYICON_VERSION_4;
    std::memcpy(data.data.tip.data(), L"hello", sizeof(L"hello"));

    const NotifyIconMessage message = ParseNotifyIconMessage(Bytes(data, kSentBytes));
    EXPECT_EQ(message.command, static_cast<std::uint32_t>(NIM_MODIFY));
    EXPECT_EQ(message.owner, 0x0003A0D9);
    EXPECT_EQ(message.id, 7u);
    EXPECT_EQ(message.callbackMessage, static_cast<std::uint32_t>(WM_APP + 9));
    EXPECT_EQ(message.icon, 0x00010033);
    EXPECT_EQ(message.tip, L"hello");
    EXPECT_EQ(message.state, static_cast<std::uint32_t>(NIS_HIDDEN));
    EXPECT_EQ(message.version, static_cast<std::uint32_t>(NOTIFYICON_VERSION_4));
}

TEST(NotifyIconProtocol, ParseSignExtendsHandlesAsWindowsDoes) {
    TrayNotifyData data             = Data(NIM_ADD, 0xFFFF1234, 1, 0);
    data.data.icon                  = 0x80000001;
    const NotifyIconMessage message = ParseNotifyIconMessage(Bytes(data, sizeof(data)));
    EXPECT_EQ(message.owner, static_cast<std::intptr_t>(static_cast<std::int32_t>(0xFFFF1234)));
    EXPECT_EQ(message.icon, static_cast<std::intptr_t>(static_cast<std::int32_t>(0x80000001)));
}

TEST(NotifyIconProtocol, ATipWithNoNullStopsAtItsBuffer) {
    TrayNotifyData data = Data(NIM_ADD, 1, 1, NIF_TIP);
    data.data.tip.fill(L'x');
    data.data.state = 0xFFFFFFFF;
    EXPECT_EQ(ParseNotifyIconMessage(Bytes(data, kSentBytes)).tip, std::wstring(128, L'x'));
}

TEST(NotifyIconProtocol, ParseRefusesShortPayloadsAndWrongSignatures) {
    const TrayNotifyData data = Data(NIM_ADD, 1, 1, 0);
    EXPECT_THROW(ParseNotifyIconMessage(Bytes(data, sizeof(data) - 1)), TrayMessageError);
    EXPECT_THROW(ParseNotifyIconMessage({}), TrayMessageError);
    TrayNotifyData wrong = data;
    wrong.signature      = 0x12345678;
    EXPECT_THROW(ParseNotifyIconMessage(Bytes(wrong, kSentBytes)), TrayMessageError);
}

TEST(NotifyIconProtocol, AddThenModifyThenDelete) {
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

    const std::optional<std::vector<TrayIcon>> deleted = ApplyNotifyIcon(modified, Message(NIM_DELETE, 5, 1, 0), nullptr);
    EXPECT_EQ(deleted, std::vector<TrayIcon>{});
}

TEST(NotifyIconProtocol, AnIconTheTrayDoesNotHaveIsRefused) {
    const std::vector<TrayIcon> icons{Icon(5, 1)};
    for (const std::uint32_t command : {NIM_MODIFY, NIM_DELETE, NIM_SETVERSION, NIM_SETFOCUS}) {
        EXPECT_FALSE(ApplyNotifyIcon(icons, Message(command, 5, 2, 0), nullptr).has_value()) << command;
        EXPECT_FALSE(ApplyNotifyIcon(icons, Message(command, 6, 1, 0), nullptr).has_value()) << command;
    }
    EXPECT_FALSE(ApplyNotifyIcon(icons, Message(99, 5, 1, 0), nullptr).has_value());
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
    EXPECT_FALSE(ApplyNotifyIcon(icons, Message(NIM_ADD, 5, 1000, 0), nullptr).has_value());
    EXPECT_TRUE(ApplyNotifyIcon(icons, Message(NIM_ADD, 5, 0, 0), nullptr).has_value());
    EXPECT_TRUE(ApplyNotifyIcon(icons, Message(NIM_MODIFY, 5, 3, 0), nullptr).has_value());
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

TEST(NotifyIconProtocol, OnlyVersionFourIconsWithoutTheStandardTooltipDrawTheirOwnPopup) {
    TrayIcon icon = Icon(5, 1);
    EXPECT_FALSE(DrawsOwnPopup(icon));
    icon.version = NOTIFYICON_VERSION_4;
    EXPECT_TRUE(DrawsOwnPopup(icon));
    icon.standardTooltip = true;
    EXPECT_FALSE(DrawsOwnPopup(icon));
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
    EXPECT_EQ(DrawableTooltip(std::wstring{L'a', L'\t', L'b', static_cast<wchar_t>(0x202E), L'c'}), L"a�b�c");
    EXPECT_EQ(DrawableTooltip(std::wstring{L'x', static_cast<wchar_t>(0xD800), L'y'}), L"x�y");
    EXPECT_EQ(DrawableTooltip(L""), L"");
}

TEST(NotifyIconProtocol, VersionZeroPostsTheIdAndTheMouseMessage) {
    const std::vector<TrayInputMessage> messages = TrayInputMessages(Icon(5, 7), ClickEvents(TrayButton::Left, 0), POINT{.x = 10, .y = 20});
    EXPECT_EQ(messages, (std::vector<TrayInputMessage>{{.message = WM_APP, .wParam = 7, .lParam = WM_LBUTTONDOWN}, {.message = WM_APP, .wParam = 7, .lParam = WM_LBUTTONUP}}));
}

TEST(NotifyIconProtocol, VersionThreeAddsSelectAndContextMenu) {
    EXPECT_EQ(ClickEvents(TrayButton::Left, NOTIFYICON_VERSION), (std::vector<std::uint32_t>{WM_LBUTTONDOWN, WM_LBUTTONUP, NIN_SELECT}));
    EXPECT_EQ(ClickEvents(TrayButton::Right, NOTIFYICON_VERSION), (std::vector<std::uint32_t>{WM_RBUTTONDOWN, WM_RBUTTONUP, WM_CONTEXTMENU}));
}

TEST(NotifyIconProtocol, AMiddleClickIsItsButtonAloneInEveryVersion) {
    EXPECT_EQ(ClickEvents(TrayButton::Middle, 0), (std::vector<std::uint32_t>{WM_MBUTTONDOWN, WM_MBUTTONUP}));
    EXPECT_EQ(ClickEvents(TrayButton::Middle, NOTIFYICON_VERSION_4), (std::vector<std::uint32_t>{WM_MBUTTONDOWN, WM_MBUTTONUP}));
}

TEST(NotifyIconProtocol, TheSecondClickOfADoubleClickIsTheDoubleClickAndUp) {
    EXPECT_EQ(DoubleClickEvents(TrayButton::Left), (std::vector<std::uint32_t>{WM_LBUTTONDBLCLK, WM_LBUTTONUP}));
    EXPECT_EQ(DoubleClickEvents(TrayButton::Right), (std::vector<std::uint32_t>{WM_RBUTTONDBLCLK, WM_RBUTTONUP}));
    EXPECT_EQ(DoubleClickEvents(TrayButton::Middle), (std::vector<std::uint32_t>{WM_MBUTTONDBLCLK, WM_MBUTTONUP}));
}

TEST(NotifyIconProtocol, ADoubleClickIsTheSameButtonOnTheSameIconSoonAndNear) {
    const auto      start = std::chrono::steady_clock::now();
    const TrayClick first{.owner = 5, .id = 7, .button = TrayButton::Left, .cursor = POINT{.x = 100, .y = 10}, .time = start};
    const auto      second = [&first](std::chrono::milliseconds after, POINT cursor) {
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

TEST(NotifyIconProtocol, AMoveTravelsInTheIconsVersionForm) {
    TrayIcon icon = Icon(5, 7);
    EXPECT_EQ(TrayInputMessages(icon, {WM_MOUSEMOVE}, POINT{.x = 3, .y = 4}), (std::vector<TrayInputMessage>{{.message = WM_APP, .wParam = 7, .lParam = WM_MOUSEMOVE}}));
    icon.version = NOTIFYICON_VERSION_4;
    EXPECT_EQ(TrayInputMessages(icon, {WM_MOUSEMOVE}, POINT{.x = 3, .y = 4}),
              (std::vector<TrayInputMessage>{{.message = WM_APP, .wParam = MAKEWPARAM(3, 4), .lParam = MAKELPARAM(WM_MOUSEMOVE, 7)}}));
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
        EXPECT_EQ(HIWORD(message.lParam), 7);
    }
    EXPECT_EQ(LOWORD(messages[2].lParam), WM_CONTEXTMENU);
}

TEST(NotifyIconProtocol, ACallbackBelowWmUserPostsNothing) {
    TrayIcon icon        = Icon(5, 7);
    icon.callbackMessage = WM_CLOSE;
    EXPECT_TRUE(TrayInputMessages(icon, ClickEvents(TrayButton::Left, 0), POINT{.x = 0, .y = 0}).empty());
    icon.callbackMessage = 0;
    EXPECT_TRUE(TrayInputMessages(icon, {WM_MOUSEMOVE}, POINT{.x = 0, .y = 0}).empty());
}

TEST(NotifyIconProtocol, ParseIconRectQueryReadsThePartAndTheIcon) {
    const IconRectQuery query = ParseIconRectQuery(QueryBytes(2, 0xFFFF8001, 42, kGuid));
    EXPECT_EQ(query.part, IconRectPart::Size);
    EXPECT_EQ(query.owner, -32767);
    EXPECT_EQ(query.id, 42u);
    EXPECT_EQ(query.guid, kGuid);
    EXPECT_EQ(ParseIconRectQuery(QueryBytes(1, 5, 7, GUID{})).part, IconRectPart::Position);
}

TEST(NotifyIconProtocol, ParseIconRectQueryRefusesWhatIsNotAQuery) {
    std::vector<std::byte> tooShort = QueryBytes(2, 5, 7, GUID{});
    tooShort.pop_back();
    EXPECT_THROW(ParseIconRectQuery(tooShort), TrayMessageError);
    std::vector<std::byte> wrongSignature = QueryBytes(2, 5, 7, GUID{});
    wrongSignature[0]                     = std::byte{0};
    EXPECT_THROW(ParseIconRectQuery(wrongSignature), TrayMessageError);
    EXPECT_THROW(ParseIconRectQuery(QueryBytes(0, 5, 7, GUID{})), TrayMessageError);
    EXPECT_THROW(ParseIconRectQuery(QueryBytes(3, 5, 7, GUID{})), TrayMessageError);
    EXPECT_THROW(ParseIconRectQuery(QueryBytes(0x101, 5, 7, GUID{})), TrayMessageError);
}

TEST(NotifyIconProtocol, AnIconShownOnTwoBarsAnswersWithTheOneNearestTheCursor) {
    const std::vector<TrayIcon>     icons{Icon(5, 7)};
    const RECT                      left{.left = -1000, .top = 0, .right = -970, .bottom = 30};
    const RECT                      right{.left = 1500, .top = 0, .right = 1530, .bottom = 30};
    const std::vector<TrayIconRect> rects{Shown(5, 7, left), Shown(5, 8, RECT{.left = 0, .top = 0, .right = 30, .bottom = 30}), Shown(5, 7, right)};
    EXPECT_TRUE(SameRect(QueriedIconRect(Query(5, 7, GUID{}), icons, rects, POINT{.x = 1510, .y = 10}), right));
    EXPECT_TRUE(SameRect(QueriedIconRect(Query(5, 7, GUID{}), icons, rects, POINT{.x = -500, .y = 400}), left));
}

TEST(NotifyIconProtocol, AnIconWithAGuidIsFoundByIt) {
    TrayIcon icon = Icon(5, 7);
    icon.guid     = kGuid;
    const RECT rect{.left = 10, .top = 0, .right = 40, .bottom = 30};
    EXPECT_TRUE(SameRect(QueriedIconRect(Query(0, 0, kGuid), {icon}, {Shown(5, 7, rect)}, POINT{.x = 0, .y = 0}), rect));
}

TEST(NotifyIconProtocol, AnIconNoBarShowsHasNoRect) {
    const std::vector<TrayIconRect> rects{Shown(5, 8, RECT{.left = 0, .top = 0, .right = 30, .bottom = 30})};
    // Folded away, an icon the tray never had, and a GUID no icon has.
    EXPECT_FALSE(QueriedIconRect(Query(5, 7, GUID{}), {Icon(5, 7)}, rects, POINT{.x = 0, .y = 0}).has_value());
    EXPECT_FALSE(QueriedIconRect(Query(5, 8, GUID{}), {Icon(5, 7)}, rects, POINT{.x = 0, .y = 0}).has_value());
    EXPECT_FALSE(QueriedIconRect(Query(5, 8, kGuid), {Icon(5, 8)}, rects, POINT{.x = 0, .y = 0}).has_value());
}

TEST(NotifyIconProtocol, IconRectAnswerPacksTheCornerOrTheSize) {
    const RECT    rect{.left = -30, .top = 400, .right = 70, .bottom = 600};
    const LRESULT position = IconRectAnswer(IconRectPart::Position, rect);
    EXPECT_EQ(static_cast<std::int16_t>(LOWORD(position)), -30);
    EXPECT_EQ(HIWORD(position), 400);
    const LRESULT size = IconRectAnswer(IconRectPart::Size, rect);
    EXPECT_EQ(LOWORD(size), 100);
    EXPECT_EQ(HIWORD(size), 200);
}
