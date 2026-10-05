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
