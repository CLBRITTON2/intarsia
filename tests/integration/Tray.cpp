#include "Harness.hpp"

#include "platform/Errors.hpp"
#include "support/TrayCopyData.hpp"
#include "support/TrayPayload.hpp"
#include "tray/ShellNotifyIcon.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <windows.h>

#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using testing::ElementsAre;

namespace {

    constexpr std::string_view kConfig = R"(
return {
    bar = { edge = "top", layer = "top", space = "share", thickness = 40, monitors = { "*" } },
    window_manager = "glazewm",
    modules_left = { "tray" },
    modules_center = {},
    modules_right = {},
    layouts = {},
    modules = { tray = { type = "tray", icon = "T" } },
}
)";

    constexpr std::uint32_t kToggleColor = 0x8E44AD;

    // Icons at 16 DIPs, the size the test icons are made at, and no transitions, so a fold is done on the next frame.
    constexpr std::string_view kStyle = R"(
bar {
  font: Segoe UI
  size: 13
  color: #e0def4
}
section {
  margin: 4
  padding: 3
  background: #191724
}
tray {
  icon {
    size: 16
  }
  item {
    padding: 4
    margin: 0 1
    toggle {
      background: #8e44ad
    }
  }
}
)";

    constexpr std::uint32_t kGreen = 0x27AE60;
    constexpr std::uint32_t kOrange = 0xF39C12;
    // The tray drops the icons of closed owners every 5 s.
    constexpr auto kSwept = std::chrono::seconds(7);

    constexpr wchar_t kOwnerClass[] = L"IntarsiaIntegrationTrayApp";

    LRESULT CALLBACK OwnerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == kTrayAppCallback) {
            auto* received = reinterpret_cast<std::vector<TrayInputMessage>*>(GetWindowLongPtrW(window, GWLP_USERDATA)); // NOLINT(performance-no-int-to-ptr)
            if (received != nullptr) {
                received->push_back(TrayInputMessage{
                    .message = message,
                    .wParam = wParam,
                    .lParam = lParam,
                });
            }
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    class OwnedIcon {
      public:
        explicit OwnedIcon(std::uint32_t rgb) : _icon(CreateSolidIcon(rgb)) {}
        ~OwnedIcon() {
            DestroyIcon(_icon);
        }
        OwnedIcon(const OwnedIcon&) = delete;
        OwnedIcon& operator=(const OwnedIcon&) = delete;

        HICON Get() const {
            return _icon;
        }

      private:
        HICON _icon;
    };

    const char* CommandName(std::uint32_t command) {
        switch (command) {
            case NIM_ADD: return "add";
            case NIM_MODIFY: return "modify";
            case NIM_DELETE: return "delete";
            case NIM_SETVERSION: return "set_version";
            default: return "unknown";
        }
    }

    // The icon color a send's log line names, none for a send without an icon.
    std::string ColorField(std::optional<std::uint32_t> rgb) {
        if (!rgb.has_value()) {
            return "none";
        }
        return std::format("#{:06x}", *rgb);
    }

    std::int16_t Low16(LRESULT value) {
        return static_cast<std::int16_t>(LOWORD(static_cast<DWORD>(value)));
    }

    std::int16_t High16(LRESULT value) {
        return static_cast<std::int16_t>(HIWORD(static_cast<DWORD>(value)));
    }

    // An app with one tray icon of a solid color. It sends the bar's tray window what shell32 sends the taskbar for
    // Shell_NotifyIcon and Shell_NotifyIconGetRect, and records what the tray posts to its callback, which arrives while
    // Eventually pumps this thread's messages.
    class TrayApp {
      public:
        TrayApp(std::uint32_t id, std::uint32_t version) : _tray(FindWindowW(std::wstring(integration::kTrayClass).c_str(), nullptr)), _id(id), _version(version) {
            if (_tray == nullptr) {
                throw std::runtime_error("no window of the bar's tray class exists");
            }
            const WNDCLASSEXW ownerClass{
                .cbSize = sizeof(WNDCLASSEXW),
                .style = 0,
                .lpfnWndProc = OwnerProc,
                .cbClsExtra = 0,
                .cbWndExtra = 0,
                .hInstance = GetModuleHandleW(nullptr),
                .hIcon = nullptr,
                .hCursor = nullptr,
                .hbrBackground = nullptr,
                .lpszMenuName = nullptr,
                .lpszClassName = kOwnerClass,
                .hIconSm = nullptr,
            };
            // A second app registers the class again, which fails harmlessly.
            RegisterClassExW(&ownerClass);
            _owner = CreateWindowExW(0, kOwnerClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (_owner == nullptr) {
                ThrowLastError("CreateWindowExW(tray app owner)");
            }
            SetWindowLongPtrW(_owner, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&_received));
        }
        ~TrayApp() {
            if (_owner != nullptr) {
                DestroyWindow(_owner);
            }
        }
        TrayApp(const TrayApp&) = delete;
        TrayApp& operator=(const TrayApp&) = delete;

        // Each returns whether the tray took the message. Add also sets the icon's version.
        bool Add(std::uint32_t rgb) const {
            return SendNotify(NIM_ADD, NIF_MESSAGE | NIF_ICON, rgb) && SendNotify(NIM_SETVERSION, 0, std::nullopt);
        }
        bool Modify(std::uint32_t rgb) const {
            return SendNotify(NIM_MODIFY, NIF_ICON, rgb);
        }
        bool Delete() const {
            return SendNotify(NIM_DELETE, 0, std::nullopt);
        }

        // Destroys the owner window without deleting the icon, as a killed app leaves it.
        void CloseOwner() {
            if (!DestroyWindow(_owner)) {
                ThrowLastError("DestroyWindow(tray app owner)");
            }
            integration::Log("tray_owner_closed", std::format("id={} owner={:#x}", _id, Handle32(_owner)));
            _owner = nullptr;
        }

        // The icon's rect on screen as the tray answers it, or nullopt while it answers 0.
        std::optional<RECT> IconRect() const {
            NotifyIconIdentifier32 query{
                .signature = kTrayNotifySignature,
                .part = static_cast<std::uint32_t>(IconRectPart::Size),
                .size = sizeof(NOTIFYICONIDENTIFIER),
                .padding = 0,
                .window = Handle32(_owner),
                .id = _id,
                .guid = GUID{},
            };
            const std::optional<LRESULT> size = SendTrayCopyData(_tray, _owner, kIconRectCopyData, &query, sizeof(query));
            if (!size.has_value() || *size == 0) {
                return std::nullopt;
            }
            query.part = static_cast<std::uint32_t>(IconRectPart::Position);
            const std::optional<LRESULT> position = SendTrayCopyData(_tray, _owner, kIconRectCopyData, &query, sizeof(query));
            if (!position.has_value()) {
                return std::nullopt;
            }
            return RECT{
                .left = Low16(*position),
                .top = High16(*position),
                .right = Low16(*position) + Low16(*size),
                .bottom = High16(*position) + High16(*size),
            };
        }

        const std::vector<TrayInputMessage>& Received() const {
            return _received;
        }

      private:
        // The icon handle lives until the send returns, since the tray reads its pixels while the sender waits.
        bool SendNotify(std::uint32_t command, std::uint32_t flags, std::optional<std::uint32_t> rgb) const {
            TrayNotifyData message = NotifyData(command, Handle32(_owner), _id, flags, kTrayAppCallback);
            message.data.version = _version;
            std::optional<OwnedIcon> icon;
            if (rgb.has_value()) {
                icon.emplace(*rgb);
                message.data.icon = Handle32(icon->Get());
            }
            const std::optional<LRESULT> answer = SendTrayCopyData(_tray, _owner, kNotifyIconCopyData, &message, sizeof(message));
            const bool taken = answer.has_value() && *answer != FALSE;
            integration::Log("tray_sent", std::format("command={} id={} version={} color={} taken={}", CommandName(command), _id, _version, ColorField(rgb), taken));
            return taken;
        }

        HWND _tray;
        HWND _owner = nullptr;
        std::uint32_t _id;
        std::uint32_t _version;
        std::vector<TrayInputMessage> _received;
    };

    // Clicks the tray's toggle, which the tray starts folded behind.
    void UnfoldTray(const integration::RunningBar& bar) {
        bar.ClickAt(bar.DrawnColumn(kToggleColor, integration::kLimit));
    }

    std::vector<LPARAM> LParams(const std::vector<TrayInputMessage>& messages, std::uint32_t id) {
        std::vector<LPARAM> lParams;
        for (const TrayInputMessage& message : messages) {
            integration::Log("tray_received", std::format("id={} message={:#x} wparam={:#x} lparam={:#x}", id, message.message, message.wParam, message.lParam));
            lParams.push_back(message.lParam);
        }
        return lParams;
    }

    std::vector<WPARAM> WParams(const std::vector<TrayInputMessage>& messages) {
        std::vector<WPARAM> wParams;
        std::ranges::transform(messages, std::back_inserter(wParams), &TrayInputMessage::wParam);
        return wParams;
    }

    LPARAM Version4Event(std::uint32_t event, std::uint32_t id) {
        return MAKELPARAM(static_cast<WORD>(event), static_cast<WORD>(id));
    }

} // namespace

TEST(Tray, PostsALeftAndARightClickOnAnIconToItsCallbackInTheFormOfItsVersion) {
    const integration::RunningBar bar("tray-click", kConfig, kStyle);
    const TrayApp current(1, NOTIFYICON_VERSION_4);
    const TrayApp legacy(2, 0);
    ASSERT_TRUE(current.Add(kGreen));
    ASSERT_TRUE(legacy.Add(kOrange));
    UnfoldTray(bar);

    bar.ClickAt(bar.DrawnColumn(kGreen, integration::kLimit));
    bar.ClickAt(bar.DrawnColumn(kOrange, integration::kLimit));
    integration::Eventually([&] { return current.Received().size() >= 3 && legacy.Received().size() >= 2; }, integration::kLimit);

    bar.ButtonUpAt(WM_RBUTTONUP, bar.DrawnColumn(kGreen, integration::kLimit));
    bar.ButtonUpAt(WM_RBUTTONUP, bar.DrawnColumn(kOrange, integration::kLimit));
    integration::Eventually([&] { return current.Received().size() >= 6 && legacy.Received().size() >= 4; }, integration::kLimit);

    EXPECT_THAT(LParams(current.Received(), 1),
                ElementsAre(Version4Event(WM_LBUTTONDOWN, 1), Version4Event(WM_LBUTTONUP, 1), Version4Event(NIN_SELECT, 1), Version4Event(WM_RBUTTONDOWN, 1),
                            Version4Event(WM_RBUTTONUP, 1), Version4Event(WM_CONTEXTMENU, 1)));
    EXPECT_THAT(LParams(legacy.Received(), 2), ElementsAre(WM_LBUTTONDOWN, WM_LBUTTONUP, WM_RBUTTONDOWN, WM_RBUTTONUP));
    EXPECT_THAT(WParams(legacy.Received()), ElementsAre(WPARAM{2}, WPARAM{2}, WPARAM{2}, WPARAM{2}));
}

TEST(Tray, AnswersTheRectOfAnIconOnTheBar) {
    const integration::RunningBar bar("tray-rect", kConfig, kStyle);
    const TrayApp app(1, NOTIFYICON_VERSION_4);
    ASSERT_TRUE(app.Add(kGreen));
    EXPECT_FALSE(app.IconRect().has_value()) << "the tray answered a rect for an icon no bar shows";

    UnfoldTray(bar);
    std::optional<double> column;
    ASSERT_TRUE(integration::Eventually(
        [&] {
            column = bar.MeanColumnOf(kGreen);
            return column.has_value() && app.IconRect().has_value();
        },
        integration::kLimit))
        << "the icon never drew with a rect";
    const RECT rect = app.IconRect().value_or(RECT{});
    const RECT barRect = integration::ScreenRect(bar.PrimaryBar());
    integration::Log("icon_rect",
                     std::format("left={} top={} right={} bottom={} bar_left={} bar_top={} bar_bottom={} icon_column={:.1f}", rect.left, rect.top, rect.right, rect.bottom,
                                 barRect.left, barRect.top, barRect.bottom, column.value_or(-1)));
    EXPECT_LE(rect.left - barRect.left, column.value_or(-1));
    EXPECT_GE(rect.right - barRect.left, column.value_or(-1));
    EXPECT_GE(rect.top, barRect.top);
    EXPECT_LE(rect.bottom, barRect.bottom);
    EXPECT_GT(rect.bottom, rect.top);
}

TEST(Tray, DrawsTheNewPixelsOfAModifiedIconThenRemovesItOnceDeleted) {
    const integration::RunningBar bar("tray-modify", kConfig, kStyle);
    const TrayApp app(1, NOTIFYICON_VERSION_4);
    ASSERT_TRUE(app.Add(kGreen));
    UnfoldTray(bar);
    ASSERT_TRUE(bar.Draws(kGreen, integration::kLimit));

    ASSERT_TRUE(app.Modify(kOrange));
    EXPECT_TRUE(bar.Draws(kOrange, integration::kLimit)) << "the modified icon's new color never drew";
    EXPECT_TRUE(bar.StopsDrawing(kGreen, integration::kLimit)) << "the icon's old color still drew";

    ASSERT_TRUE(app.Delete());
    EXPECT_TRUE(bar.StopsDrawing(kOrange, integration::kLimit)) << "the deleted icon still drew";
}

TEST(Tray, RemovesTheIconOfAClosedOwner) {
    const integration::RunningBar bar("tray-owner", kConfig, kStyle);
    TrayApp app(1, NOTIFYICON_VERSION_4);
    ASSERT_TRUE(app.Add(kGreen));
    UnfoldTray(bar);
    ASSERT_TRUE(bar.Draws(kGreen, integration::kLimit));

    app.CloseOwner();
    EXPECT_TRUE(bar.StopsDrawing(kGreen, kSwept)) << "the icon of a closed owner still drew";
}
