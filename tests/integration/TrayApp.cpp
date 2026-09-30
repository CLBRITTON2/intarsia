#include "TrayApp.hpp"

#include "Harness.hpp"
#include "platform/Errors.hpp"

#include <shellapi.h>

#include <array>
#include <cstddef>
#include <format>
#include <stdexcept>
#include <string>

namespace integration {

    namespace {

        constexpr wchar_t       kOwnerClass[] = L"IntarsiaIntegrationTrayApp";
        constexpr UINT          kCallback     = WM_APP + 9;
        constexpr std::uint32_t kIconSide     = 16;
        constexpr std::uint32_t kAnswerMs     = 5000;

        std::uint32_t           Handle32(const void* handle) {
            return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(handle));
        }

        LRESULT CALLBACK OwnerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
            if (message == kCallback) {
                auto* received = reinterpret_cast<std::vector<TrayInputMessage>*>(GetWindowLongPtrW(window, GWLP_USERDATA)); // NOLINT(performance-no-int-to-ptr)
                if (received != nullptr) {
                    received->push_back(TrayInputMessage{.message = message, .wParam = wParam, .lParam = lParam});
                }
                return 0;
            }
            return DefWindowProcW(window, message, wParam, lParam);
        }

        class OwnedIcon {
          public:
            explicit OwnedIcon(std::uint32_t rgb) {
                const std::vector<std::uint32_t> pixels(size_t{kIconSide} * kIconSide, rgb | 0xFF000000U);
                const std::array<std::byte, 32>  mask{};
                HBITMAP                          color     = CreateBitmap(kIconSide, kIconSide, 1, 32, pixels.data());
                HBITMAP                          maskImage = CreateBitmap(kIconSide, kIconSide, 1, 1, mask.data());
                ICONINFO                         info{.fIcon = TRUE, .xHotspot = 0, .yHotspot = 0, .hbmMask = maskImage, .hbmColor = color};
                _icon             = color != nullptr && maskImage != nullptr ? CreateIconIndirect(&info) : nullptr;
                const DWORD error = GetLastError();
                DeleteObject(color);
                DeleteObject(maskImage);
                if (_icon == nullptr) {
                    throw std::runtime_error(std::format("CreateIconIndirect failed with {}", error));
                }
            }
            ~OwnedIcon() {
                DestroyIcon(_icon);
            }
            OwnedIcon(const OwnedIcon&)            = delete;
            OwnedIcon& operator=(const OwnedIcon&) = delete;

            HICON      Get() const {
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

        std::int16_t Low16(LRESULT value) {
            return static_cast<std::int16_t>(LOWORD(static_cast<DWORD>(value)));
        }

        std::int16_t High16(LRESULT value) {
            return static_cast<std::int16_t>(HIWORD(static_cast<DWORD>(value)));
        }

    } // namespace

    TrayApp::TrayApp(std::uint32_t id, std::uint32_t version) : _tray(FindWindowW(std::wstring(kTrayClass).c_str(), nullptr)), _owner(nullptr), _id(id), _version(version) {
        if (_tray == nullptr) {
            throw std::runtime_error("no window of the bar's tray class exists");
        }
        const WNDCLASSEXW ownerClass{.cbSize        = sizeof(WNDCLASSEXW),
                                     .style         = 0,
                                     .lpfnWndProc   = OwnerProc,
                                     .cbClsExtra    = 0,
                                     .cbWndExtra    = 0,
                                     .hInstance     = GetModuleHandleW(nullptr),
                                     .hIcon         = nullptr,
                                     .hCursor       = nullptr,
                                     .hbrBackground = nullptr,
                                     .lpszMenuName  = nullptr,
                                     .lpszClassName = kOwnerClass,
                                     .hIconSm       = nullptr};
        // A second app registers the class again, which fails harmlessly.
        RegisterClassExW(&ownerClass);
        _owner = CreateWindowExW(0, kOwnerClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (_owner == nullptr) {
            ThrowLastError("CreateWindowExW(tray app owner)");
        }
        SetWindowLongPtrW(_owner, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&_received));
    }

    TrayApp::~TrayApp() {
        if (_owner != nullptr) {
            DestroyWindow(_owner);
        }
    }

    bool TrayApp::Add(std::uint32_t rgb) const {
        return SendNotify(NIM_ADD, NIF_MESSAGE | NIF_ICON, rgb) && SendNotify(NIM_SETVERSION, 0, std::nullopt);
    }

    bool TrayApp::Modify(std::uint32_t rgb) const {
        return SendNotify(NIM_MODIFY, NIF_ICON, rgb);
    }

    bool TrayApp::Delete() const {
        return SendNotify(NIM_DELETE, 0, std::nullopt);
    }

    void TrayApp::CloseOwner() {
        if (!DestroyWindow(_owner)) {
            ThrowLastError("DestroyWindow(tray app owner)");
        }
        Log("tray_owner_closed", std::format("id={} owner={:#x}", _id, Handle32(_owner)));
        _owner = nullptr;
    }

    std::optional<RECT> TrayApp::IconRect() const {
        NotifyIconIdentifier32 query{.signature = kTrayNotifySignature,
                                     .part      = static_cast<std::uint32_t>(IconRectPart::Size),
                                     .size      = sizeof(NOTIFYICONIDENTIFIER),
                                     .padding   = 0,
                                     .window    = Handle32(_owner),
                                     .id        = _id,
                                     .guid      = GUID{}};
        LRESULT                size = 0;
        if (!Send(kIconRectCopyData, &query, sizeof(query), size) || size == 0) {
            return std::nullopt;
        }
        query.part       = static_cast<std::uint32_t>(IconRectPart::Position);
        LRESULT position = 0;
        if (!Send(kIconRectCopyData, &query, sizeof(query), position)) {
            return std::nullopt;
        }
        return RECT{.left = Low16(position), .top = High16(position), .right = Low16(position) + Low16(size), .bottom = High16(position) + High16(size)};
    }

    const std::vector<TrayInputMessage>& TrayApp::Received() const {
        return _received;
    }

    // Whether the tray answered in time. Throws when the send fails for any other reason than a timeout.
    bool TrayApp::Send(ULONG_PTR kind, void* payload, size_t size, LRESULT& answer) const {
        COPYDATASTRUCT data{.dwData = kind, .cbData = static_cast<DWORD>(size), .lpData = payload};
        DWORD_PTR      result = 0;
        if (SendMessageTimeoutW(_tray, WM_COPYDATA, reinterpret_cast<WPARAM>(_owner), reinterpret_cast<LPARAM>(&data), SMTO_NORMAL, kAnswerMs, &result) == 0) {
            const DWORD error = GetLastError();
            if (error == ERROR_TIMEOUT) {
                return false;
            }
            throw Win32Error("SendMessageTimeoutW(WM_COPYDATA to the tray)", HRESULT_FROM_WIN32(error));
        }
        answer = static_cast<LRESULT>(result);
        return true;
    }

    // The icon handle lives until the send returns, since the tray reads its pixels while the sender waits.
    bool TrayApp::SendNotify(std::uint32_t command, std::uint32_t flags, std::optional<std::uint32_t> rgb) const {
        std::optional<OwnedIcon> icon;
        if (rgb.has_value()) {
            icon.emplace(*rgb);
        }
        TrayNotifyData message{};
        message.signature            = kTrayNotifySignature;
        message.command              = command;
        message.data.size            = sizeof(NotifyIconData32);
        message.data.window          = Handle32(_owner);
        message.data.id              = _id;
        message.data.flags           = flags;
        message.data.callbackMessage = kCallback;
        message.data.icon            = icon.has_value() ? Handle32(icon->Get()) : 0;
        message.data.version         = _version;
        LRESULT    answer            = FALSE;
        const bool answered          = Send(kNotifyIconCopyData, &message, sizeof(message), answer);
        const bool taken             = answered && answer != FALSE;
        Log("tray_sent",
            std::format("command={} id={} version={} color={} taken={}", CommandName(command), _id, _version, rgb.has_value() ? std::format("#{:06x}", *rgb) : std::string("none"),
                        taken));
        return taken;
    }

} // namespace integration
