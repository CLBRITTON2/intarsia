#include "platform/AppBar.hpp"

#include "platform/Win32Cast.hpp"
#include "support/Rect.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace {

    const RECT kMonitor{0, 0, 1920, 1080};

} // namespace

TEST(AppBar, DockedRectTakesAStripAlongEachEdge) {
    EXPECT_EQ(DockedRect(kMonitor, Edge::Top, 54), (RECT{0, 0, 1920, 54}));
    EXPECT_EQ(DockedRect(kMonitor, Edge::Bottom, 54), (RECT{0, 1026, 1920, 1080}));
    EXPECT_EQ(DockedRect(kMonitor, Edge::Left, 54), (RECT{0, 0, 54, 1080}));
    EXPECT_EQ(DockedRect(kMonitor, Edge::Right, 54), (RECT{1866, 0, 1920, 1080}));
    // A secondary monitor keeps its offset from the primary.
    const RECT secondary{1920, -200, 4480, 1240};
    EXPECT_EQ(DockedRect(secondary, Edge::Top, 81), (RECT{1920, -200, 4480, -119}));
}

// An ABM_SETPOS for a 3440x54 strip at the top, byte for byte as shell32 sent it to the taskbar window.
TEST(AppBar, AppBarRequestIsWhatShell32Sends) {
    constexpr std::array<std::uint32_t, 16> kSent{0x28, 0x002b0848, 0, ABE_TOP, 0, 0, 3440, 54, 0, 0, ABM_SETPOS, 0, 0x669b7a70, 0x19f, 0x8dbc, 0};
    APPBARDATA                              data{};
    data.cbSize                 = sizeof(data);
    data.hWnd                   = ToWindow(0x002b0848);
    data.uEdge                  = ABE_TOP;
    data.rc                     = RECT{0, 0, 3440, 54};
    const AppBarCommand request = AppBarRequest(ABM_SETPOS, data, 0x19f669b7a70, 0x8dbc);
    ASSERT_EQ(sizeof(request), sizeof(kSent));
    EXPECT_EQ(std::memcmp(&request, kSent.data(), sizeof(request)), 0);
}
