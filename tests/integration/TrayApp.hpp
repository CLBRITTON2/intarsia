#pragma once

#include "tray/NotifyIconProtocol.hpp"

#include <windows.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace integration {

    // An app with one tray icon of a solid color. It sends the bar's tray window what shell32 sends the taskbar for
    // Shell_NotifyIcon and Shell_NotifyIconGetRect, and records what the tray posts to its callback, which arrives while
    // Eventually pumps this thread's messages.
    class TrayApp {
      public:
        TrayApp(std::uint32_t id, std::uint32_t version);
        ~TrayApp();
        TrayApp(const TrayApp&)            = delete;
        TrayApp& operator=(const TrayApp&) = delete;

        // Each returns whether the tray took the message. Add also sets the icon's version.
        bool Add(std::uint32_t rgb) const;
        bool Modify(std::uint32_t rgb) const;
        bool Delete() const;
        // Destroys the owner window without deleting the icon, as a killed app leaves it.
        void CloseOwner();
        // The icon's rect on screen as the tray answers it, or nullopt while it answers 0.
        std::optional<RECT>                  IconRect() const;
        const std::vector<TrayInputMessage>& Received() const;

      private:
        bool                          Send(ULONG_PTR kind, void* payload, size_t size, LRESULT& answer) const;
        bool                          SendNotify(std::uint32_t command, std::uint32_t flags, std::optional<std::uint32_t> rgb) const;

        HWND                          _tray;
        HWND                          _owner;
        std::uint32_t                 _id;
        std::uint32_t                 _version;
        std::vector<TrayInputMessage> _received;
    };

} // namespace integration
