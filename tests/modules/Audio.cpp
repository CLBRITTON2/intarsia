#include "modules/Audio.hpp"

#include "TextOnlyRender.hpp"
#include "audio/IAudio.hpp"
#include "support/ThrowsWith.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace {

    // A default device a test sets directly, recording the changes modules ask for.
    class FakeAudio final : public IAudio {
      public:
        std::optional<AudioDevice> device;
        std::vector<float> volumes;
        std::vector<bool> mutes;

        std::optional<AudioDevice> Device() const override {
            return device;
        }
        void SetVolume(float volume) override {
            volumes.push_back(volume);
        }
        void SetMuted(bool muted) override {
            mutes.push_back(muted);
        }
    };

    AudioConfig TestConfig() {
        return AudioConfig{
            .flow = AudioFlow::Output,
            .format = "{volume}%",
            .formatMuted = "muted",
            .tooltip = "{device} at {volume}%",
            .icon = L"V",
            .iconMuted = L"M",
            .stepPercent = 5,
            .actions = kNoActions,
        };
    }

    AudioDevice Speakers(float volume, bool muted) {
        return AudioDevice{
            .name = L"Speakers",
            .volume = volume,
            .muted = muted,
        };
    }

} // namespace

TEST(Audio, FormatsTheVolumeOrTheMutedTextWithTheirIcons) {
    EXPECT_EQ(FormatAudio(TestConfig(), Speakers(0.42f, false)), NormalText(L"V", L"42%"));
    EXPECT_EQ(FormatAudio(TestConfig(), Speakers(0.42f, true)),
              (ModuleText{
                  .icon = L"M",
                  .text = L"muted",
                  .state = TextState::Muted,
                  .classIndex = 0,
              }));
    EXPECT_EQ(FormatAudio(TestConfig(), std::nullopt), NormalText(L"", L""));
}

TEST(Audio, AStepLandsOnAWholePercentWithinTheRange) {
    EXPECT_FLOAT_EQ(SteppedVolume(0.47f, 5, ScrollDirection::Up), 0.52f);
    EXPECT_FLOAT_EQ(SteppedVolume(0.47f, 5, ScrollDirection::Down), 0.42f);
    EXPECT_FLOAT_EQ(SteppedVolume(0.98f, 5, ScrollDirection::Up), 1.0f);
    EXPECT_FLOAT_EQ(SteppedVolume(0.02f, 5, ScrollDirection::Down), 0.0f);
}

TEST(Audio, ClickTogglesMuteAndScrollStepsTheVolume) {
    FakeAudio audio;
    audio.device = Speakers(0.5f, false);
    Audio module(TestConfig(), audio);
    module.Click(D2D1::Point2F(0.0f, 0.0f));
    module.Scroll(ScrollDirection::Up);
    module.Scroll(ScrollDirection::Down);
    EXPECT_EQ(audio.mutes, std::vector<bool>{true});
    EXPECT_EQ(audio.volumes, (std::vector<float>{0.55f, 0.45f}));

    audio.device = Speakers(0.5f, true);
    module.Click(D2D1::Point2F(0.0f, 0.0f));
    EXPECT_EQ(audio.mutes, (std::vector<bool>{true, false}));
}

TEST(Audio, WithNoDeviceNothingIsShownOrChanged) {
    FakeAudio audio;
    const TextOnlyRender render;
    Audio module(TestConfig(), audio);
    EXPECT_FALSE(module.Layout(render.Context()));
    EXPECT_FLOAT_EQ(module.Size().width, 0.0f);
    module.Click(D2D1::Point2F(0.0f, 0.0f));
    module.Scroll(ScrollDirection::Up);
    EXPECT_TRUE(audio.mutes.empty());
    EXPECT_TRUE(audio.volumes.empty());
    EXPECT_FALSE(module.TooltipText().has_value());
}

TEST(Audio, TooltipNamesTheDevice) {
    FakeAudio audio;
    audio.device = Speakers(0.3f, false);
    Audio module(TestConfig(), audio);
    EXPECT_EQ(module.TooltipText(), L"Speakers at 30%");
}

TEST(Audio, ABadFormatIsAConfigError) {
    FakeAudio audio;
    AudioConfig config = TestConfig();
    config.formatMuted = "{level}";
    EXPECT_THAT([&] { return Audio(config, audio); },
                ThrowsWith<ConfigError>("audio format_muted '{level}' is not a valid format: {level} is not a value this format has. It can use {volume}, {device}"));
}
