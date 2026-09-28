#include "modules/Audio.hpp"

#include "../audio/FakeAudio.hpp"
#include "TextOnlyRender.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

    AudioConfig TestConfig() {
        return AudioConfig{.format      = "{volume}%",
                           .formatMuted = "muted",
                           .tooltip     = "{device} at {volume}%",
                           .icon        = L"V",
                           .iconMuted   = L"M",
                           .stepPercent = 5,
                           .actions     = {.click = std::nullopt, .rightClick = std::nullopt, .middleClick = std::nullopt, .scrollUp = std::nullopt, .scrollDown = std::nullopt}};
    }

    AudioOutput Speakers(float volume, bool muted) {
        return AudioOutput{.device = L"Speakers", .volume = volume, .muted = muted};
    }

} // namespace

TEST(Audio, FormatsTheVolumeOrTheMutedTextWithTheirIcons) {
    EXPECT_EQ(FormatAudio(TestConfig(), Speakers(0.42f, false)), (ModuleText{.icon = L"V", .text = L"42%", .state = TextState::Normal}));
    EXPECT_EQ(FormatAudio(TestConfig(), Speakers(0.42f, true)), (ModuleText{.icon = L"M", .text = L"muted", .state = TextState::Normal}));
    EXPECT_EQ(FormatAudio(TestConfig(), std::nullopt), (ModuleText{.icon = L"", .text = L"", .state = TextState::Normal}));
}

TEST(Audio, FormatsNameTheDeviceAndTakeASpec) {
    AudioConfig config = TestConfig();
    config.format      = "{device}: {volume:>3}%";
    EXPECT_EQ(FormatAudio(config, Speakers(0.5f, false)).text, L"Speakers:  50%");
}

TEST(Audio, AStepLandsOnAWholePercentWithinTheRange) {
    EXPECT_FLOAT_EQ(SteppedVolume(0.47f, 5, ScrollDirection::Up), 0.52f);
    EXPECT_FLOAT_EQ(SteppedVolume(0.47f, 5, ScrollDirection::Down), 0.42f);
    EXPECT_FLOAT_EQ(SteppedVolume(0.98f, 5, ScrollDirection::Up), 1.0f);
    EXPECT_FLOAT_EQ(SteppedVolume(0.02f, 5, ScrollDirection::Down), 0.0f);
}

TEST(Audio, ClickTogglesMuteAndScrollStepsTheVolume) {
    FakeAudio audio;
    audio.output = Speakers(0.5f, false);
    Audio module(TestConfig(), audio);
    module.Click(D2D1::Point2F(0.0f, 0.0f));
    module.Scroll(ScrollDirection::Up);
    module.Scroll(ScrollDirection::Down);
    EXPECT_EQ(audio.mutes, std::vector<bool>{true});
    EXPECT_EQ(audio.volumes, (std::vector<float>{0.55f, 0.45f}));

    audio.output = Speakers(0.5f, true);
    module.Click(D2D1::Point2F(0.0f, 0.0f));
    EXPECT_EQ(audio.mutes, (std::vector<bool>{true, false}));
}

TEST(Audio, WithNoDeviceNothingIsShownOrChanged) {
    FakeAudio            audio;
    const TextOnlyRender render;
    Audio                module(TestConfig(), audio);
    EXPECT_TRUE(module.Layout(render.Context()));
    EXPECT_FLOAT_EQ(module.Size().width, 0.0f);
    module.Click(D2D1::Point2F(0.0f, 0.0f));
    module.Scroll(ScrollDirection::Up);
    EXPECT_TRUE(audio.mutes.empty());
    EXPECT_TRUE(audio.volumes.empty());
    EXPECT_FALSE(module.TooltipText().has_value());
}

TEST(Audio, LayoutReportsAChangeOnlyWhenTheShownTextChanges) {
    FakeAudio            audio;
    const TextOnlyRender render;
    Audio                module(TestConfig(), audio);
    audio.output = Speakers(0.5f, false);
    EXPECT_TRUE(module.Layout(render.Context()));
    EXPECT_GT(module.Size().width, 0.0f);
    EXPECT_FALSE(module.Layout(render.Context()));
    // Under half a percent rounds to the same text.
    audio.output = Speakers(0.501f, false);
    EXPECT_FALSE(module.Layout(render.Context()));
    audio.output = Speakers(0.5f, true);
    EXPECT_TRUE(module.Layout(render.Context()));
}

TEST(Audio, TooltipNamesTheDevice) {
    FakeAudio audio;
    audio.output = Speakers(0.3f, false);
    Audio module(TestConfig(), audio);
    EXPECT_EQ(module.TooltipText(), L"Speakers at 30%");
}

TEST(Audio, ABadFormatIsAConfigError) {
    FakeAudio   audio;
    AudioConfig config = TestConfig();
    config.formatMuted = "{level}";
    EXPECT_THAT([&] { return Audio(config, audio); },
                testing::ThrowsMessage<ConfigError>(
                    testing::HasSubstr("audio format_muted '{level}' is not a valid format: {level} is not a value this format has. It can use {volume}, {device}")));
}
