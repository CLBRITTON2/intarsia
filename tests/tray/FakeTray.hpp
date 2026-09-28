#pragma once

#include "tray/ITray.hpp"

#include <utility>
#include <vector>

// Icons a test sets directly, recording every press and move.
class FakeTray final : public ITray {
  public:
    struct Pressed {
        std::uint32_t id;
        TrayButton    button;
    };

    std::vector<TrayIcon>      icons;
    std::vector<Pressed>       presses;
    std::vector<std::uint32_t> moves; // the id of each icon moved over

    std::vector<TrayIcon>      Icons() const override {
        return icons;
    }

    void Press(const TrayIcon& icon, TrayButton button) override {
        presses.push_back(Pressed{.id = icon.id, .button = button});
    }

    void Move(const TrayIcon& icon) override {
        moves.push_back(icon.id);
    }
};
