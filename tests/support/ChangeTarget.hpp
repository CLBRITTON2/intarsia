#pragma once

#include "platform/ChangeSignal.hpp"
#include "support/MessageWindow.hpp"

#include <cstdint>

// A message window and a ChangeSignal posting message to it, for code under test that announces changes.
struct ChangeTarget {
    explicit ChangeTarget(std::uint32_t message) : signal(window.Get(), message) {}

    MessageWindow window;
    ChangeSignal  signal;
};
