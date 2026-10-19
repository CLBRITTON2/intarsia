#include "platform/Monitors.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

    constexpr size_t kSerialDescriptorOffset = 54 + (18 * 2);

    // A base block for DEL41A8 with numeric serial 0 and no text serial.
    std::vector<std::uint8_t> BaseBlock() {
        std::vector<std::uint8_t> edid(128, 0);
        constexpr std::array<std::uint8_t, 8> kHeader{0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
        std::ranges::copy(kHeader, edid.begin());
        edid[8] = 0x10;
        edid[9] = 0xAC;
        edid[10] = 0xA8;
        edid[11] = 0x41;
        // A detailed timing descriptor first, as real monitors have, so the tag search must skip it.
        edid[54] = 0x01;
        edid[55] = 0x1D;
        return edid;
    }

    std::vector<std::uint8_t> WithTextSerial(std::vector<std::uint8_t> edid, std::string_view text) {
        edid[kSerialDescriptorOffset + 3] = 0xFF;
        std::string padded(text);
        padded.push_back('\n');
        padded.resize(13, ' ');
        std::ranges::copy(padded, edid.begin() + static_cast<std::ptrdiff_t>(kSerialDescriptorOffset + 5));
        return edid;
    }

    std::vector<std::uint8_t> WithNumericSerial(std::vector<std::uint8_t> edid, std::uint32_t serial) {
        edid[12] = static_cast<std::uint8_t>(serial & 0xFFU);
        edid[13] = static_cast<std::uint8_t>((serial >> 8) & 0xFFU);
        edid[14] = static_cast<std::uint8_t>((serial >> 16) & 0xFFU);
        edid[15] = static_cast<std::uint8_t>((serial >> 24) & 0xFFU);
        return edid;
    }

} // namespace

TEST(Edid, ReadsTheModelAndTextSerial) {
    const EdidIdentity identity = ParseEdid(WithTextSerial(WithNumericSerial(BaseBlock(), 12345), "7XYZ123"));
    EXPECT_EQ(identity, (EdidIdentity{.model = "DEL41A8", .serial = "7XYZ123"}));
    EXPECT_EQ(MonitorId(identity), "DEL41A8-7XYZ123");
}

TEST(Edid, FallsBackToTheNumericSerial) {
    const EdidIdentity identity = ParseEdid(WithNumericSerial(BaseBlock(), 16843009));
    EXPECT_EQ(identity, (EdidIdentity{.model = "DEL41A8", .serial = "16843009"}));
}

TEST(Edid, LeavesTheSerialOutWhenTheMonitorHasNone) {
    const EdidIdentity identity = ParseEdid(BaseBlock());
    EXPECT_EQ(identity, (EdidIdentity{.model = "DEL41A8", .serial = std::nullopt}));
    EXPECT_EQ(MonitorId(identity), "DEL41A8");
}

TEST(Edid, TreatsABlankOrUnprintableTextSerialAsNone) {
    EXPECT_EQ(ParseEdid(WithTextSerial(BaseBlock(), "   ")).serial, std::nullopt);
    EXPECT_EQ(ParseEdid(WithTextSerial(WithNumericSerial(BaseBlock(), 42), "AB\x1B[2J")).serial, std::optional<std::string>("42"));
}

// An id the reader makes that the config refused could be listed by intarsia-cli monitors and still never match.
TEST(Edid, MakesOnlyIdsAConfigAccepts) {
    const std::string longestText = MonitorId(ParseEdid(WithTextSerial(BaseBlock(), "ABCDEFGHIJ 12")));
    EXPECT_EQ(longestText, "DEL41A8-ABCDEFGHIJ 12");
    EXPECT_TRUE(IsMonitorId(longestText)) << longestText;
    const std::string largestNumber = MonitorId(ParseEdid(WithNumericSerial(BaseBlock(), 0xFFFFFFFFU)));
    EXPECT_EQ(largestNumber, "DEL41A8-4294967295");
    EXPECT_TRUE(IsMonitorId(largestNumber)) << largestNumber;
    const std::string noSerial = MonitorId(ParseEdid(BaseBlock()));
    EXPECT_TRUE(IsMonitorId(noSerial)) << noSerial;
}

TEST(Edid, RefusesAShortBlockABadHeaderAndABadLetter) {
    const std::vector<std::uint8_t> edid = BaseBlock();
    EXPECT_THROW(ParseEdid(std::span(edid).first(127)), EdidError);
    std::vector<std::uint8_t> badHeader = edid;
    badHeader[0] = 0xFF;
    EXPECT_THROW(ParseEdid(badHeader), EdidError);
    std::vector<std::uint8_t> badLetter = edid;
    badLetter[8] = 0x00;
    EXPECT_THROW(ParseEdid(badLetter), EdidError);
}

TEST(Edid, RecognizesAMonitorId) {
    EXPECT_TRUE(IsMonitorId("DEL41A8"));
    EXPECT_TRUE(IsMonitorId("DEL41A8-7XYZ123"));
    EXPECT_TRUE(IsMonitorId("BOE0B4D-SN 12"));
    EXPECT_FALSE(IsMonitorId("del41a8"));
    EXPECT_FALSE(IsMonitorId("DEL41a8"));
    EXPECT_FALSE(IsMonitorId("DEL41A"));
    EXPECT_FALSE(IsMonitorId("DEL41A8-"));
    EXPECT_FALSE(IsMonitorId("DEL41A8_7XYZ"));
    EXPECT_FALSE(IsMonitorId("DEL41A8- 7XYZ"));
    EXPECT_FALSE(IsMonitorId("DEL41A8-12345678901234"));
    EXPECT_FALSE(IsMonitorId("primary"));
}
