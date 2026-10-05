#include "MonitorTable.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

TEST(MonitorTable, ListsEachTargetUnderItsColumns) {
    const BarConfig bar{
        .edge          = Edge::Top,
        .layer         = Layer::Top,
        .space         = Space::Reserve,
        .thicknessDips = 32.0F,
        .monitors      = {MonitorWithId{.id = "DEL41A8-7XYZ123"}},
    };
    const MonitorDescription laptop{
        .monitor = nullptr,
        .primary = true,
        .targets = {DisplayTarget{.id = "BOE0B4D", .name = std::nullopt, .connection = "internal", .internal = true}},
    };
    const MonitorDescription desk{
        .monitor = nullptr,
        .primary = false,
        .targets = {DisplayTarget{.id = "DEL41A8-7XYZ123", .name = "DELL U2720Q", .connection = "DisplayPort", .internal = false}},
    };
    const MonitorDescription pathless{.monitor = nullptr, .primary = false, .targets = {}};
    EXPECT_EQ(MonitorTable({laptop, desk, pathless}, bar),
              "id               name         connection   primary  bar\n"
              "BOE0B4D          none         internal     yes      no\n"
              "DEL41A8-7XYZ123  DELL U2720Q  DisplayPort  no       yes\n"
              "none             none         none         no       no\n");
}

TEST(MonitorTable, ReplacesControlCharactersInAName) {
    const BarConfig bar{
        .edge          = Edge::Top,
        .layer         = Layer::Top,
        .space         = Space::Reserve,
        .thicknessDips = 32.0F,
        .monitors      = {MonitorWithId{.id = "DEL41A8"}},
    };
    // An OSC sequence that would set the terminal's title.
    const MonitorDescription hostile{
        .monitor = nullptr,
        .primary = true,
        .targets = {DisplayTarget{.id = "DEL41A8", .name = "EVIL\x1B]0;owned\x07", .connection = "HDMI", .internal = false}},
    };
    const std::string table = MonitorTable({hostile}, bar);
    EXPECT_EQ(table.find('\x1B'), std::string::npos) << table;
    EXPECT_EQ(table.find('\x07'), std::string::npos) << table;
    EXPECT_NE(table.find("EVIL\xEF\xBF\xBD]0;owned\xEF\xBF\xBD  HDMI"), std::string::npos) << table;
}
