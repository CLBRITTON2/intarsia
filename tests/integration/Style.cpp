#include "Harness.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <string_view>

namespace {

    constexpr std::string_view kConfig = R"(
return {
    bar = { edge = "top", layer = "top", space = "share", thickness = 40 },
    window_manager = "glazewm",
    modules_left = { "clock" },
    modules_center = {},
    modules_right = {},
    layouts = {},
    modules = {
        clock = { type = "clock", format = "{time:%H:%M:%S}", icon = "", interval = 1 },
    },
}
)";

    constexpr std::uint32_t    kSectionColor = 0xC03A2B;

    constexpr std::string_view kStyle = R"(
* { font-family: "Segoe UI"; font-size: 13px; color: #f2f2f2; }
section { margin: 4px; padding: 3px; background: #c03a2b; }
)";

} // namespace

TEST(Style, DrawsTheSectionBackgroundOnScreen) {
    const integration::RunningBar bar("style", kConfig, kStyle);

    // A section 20 px high or more, around a clock's text, so hundreds of pixels at the least.
    size_t drawn = 0;
    EXPECT_TRUE(integration::Eventually(
        [&] {
            drawn = integration::CountPixels(integration::CaptureScreenOver(bar.PrimaryBar()), kSectionColor);
            return drawn >= 400;
        },
        std::chrono::seconds(5)))
        << drawn << " pixels of the section color";
    integration::Log("pixels_counted", std::format("color=#{:06x} count={}", kSectionColor, drawn));
}
