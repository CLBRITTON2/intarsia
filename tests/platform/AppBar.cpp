#include "platform/AppBar.hpp"

#include "platform/Win32Window.hpp"
#include "support/Rect.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace {

    RECT Rect(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom) {
        return RECT{
            .left = left,
            .top = top,
            .right = right,
            .bottom = bottom,
        };
    }

} // namespace

TEST(AppBar, DockedRectTakesAStripAlongEachEdge) {
    const RECT monitorRect = Rect(0, 0, 1920, 1080);
    struct Case {
        Edge edge;
        RECT expected;
    };
    const Case cases[] = {
        {.edge = Edge::Top, .expected = Rect(0, 0, 1920, 54)},
        {.edge = Edge::Bottom, .expected = Rect(0, 1026, 1920, 1080)},
        {.edge = Edge::Left, .expected = Rect(0, 0, 54, 1080)},
        {.edge = Edge::Right, .expected = Rect(1866, 0, 1920, 1080)},
    };
    for (const Case& test : cases) {
        SCOPED_TRACE(static_cast<std::int32_t>(test.edge));
        EXPECT_EQ(DockedRect(monitorRect, test.edge, 54), test.expected);
    }
    // A secondary monitor keeps its offset from the primary.
    EXPECT_EQ(DockedRect(Rect(1920, -200, 4480, 1240), Edge::Top, 81), Rect(1920, -200, 4480, -119));
}

// An ABM_SETPOS for a 3440x54 strip at the top, byte for byte as shell32 sent it to the taskbar window.
TEST(AppBar, AppBarRequestIsWhatShell32Sends) {
    constexpr std::array<std::uint32_t, 16> kSent{0x28, 0x002b0848, 0, ABE_TOP, 0, 0, 3440, 54, 0, 0, ABM_SETPOS, 0, 0x669b7a70, 0x19f, 0x8dbc, 0};
    APPBARDATA data{};
    data.cbSize = sizeof(data);
    data.hWnd = ToWindow(0x002b0848);
    data.uEdge = ABE_TOP;
    data.rc = Rect(0, 0, 3440, 54);
    const AppBarCommand request = AppBarRequest(ABM_SETPOS, data, 0x19f669b7a70, 0x8dbc);
    ASSERT_EQ(sizeof(request), sizeof(kSent));
    EXPECT_EQ(std::memcmp(&request, kSent.data(), sizeof(request)), 0);
}
