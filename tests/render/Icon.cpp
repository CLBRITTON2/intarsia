#include "render/Icon.hpp"

#include "platform/Errors.hpp"
#include "platform/Threads.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

    // A blank icon of the given size, which the caller destroys.
    HICON BlankIcon(std::int32_t side) {
        HBITMAP color = CreateBitmap(side, side, 1, 32, nullptr);
        HBITMAP mask = CreateBitmap(side, side, 1, 1, nullptr);
        ICONINFO info{
            .fIcon = TRUE,
            .xHotspot = 0,
            .yHotspot = 0,
            .hbmMask = mask,
            .hbmColor = color,
        };
        HICON icon = CreateIconIndirect(&info);
        DeleteObject(color);
        DeleteObject(mask);
        return icon;
    }

    // A monochrome icon of the given size: no color bitmap, and a mask twice as tall holding both masks.
    HICON MonochromeIcon(std::int32_t side) {
        HBITMAP mask = CreateBitmap(side, side * 2, 1, 1, nullptr);
        ICONINFO info{
            .fIcon = TRUE,
            .xHotspot = 0,
            .yHotspot = 0,
            .hbmMask = mask,
            .hbmColor = nullptr,
        };
        HICON icon = CreateIconIndirect(&info);
        DeleteObject(mask);
        return icon;
    }

    // A COM apartment and a WIC factory for the test's thread, as a thread that reads icons holds them.
    class IconReading : public testing::Test {
      protected:
        IWICImagingFactory& Imaging() const {
            return *_imaging.Get();
        }

      private:
        ComScope _com{ComThreading::Multi, "icon test"};
        Microsoft::WRL::ComPtr<IWICImagingFactory> _imaging = CreateImagingFactory();
    };

} // namespace

TEST_F(IconReading, IconToPixelsGivesPremultipliedPixelsAtTheIconSize) {
    const IconPixels pixels = IconToPixels(Imaging(), LoadIconW(nullptr, IDI_APPLICATION));
    EXPECT_GT(pixels.width, 0u);
    EXPECT_GT(pixels.height, 0u);
    EXPECT_EQ(pixels.pbgra.size(), static_cast<size_t>(pixels.width) * pixels.height * 4);
}

// Any process can hand the tray an icon of any size, and each would be held in full by every bar.
TEST_F(IconReading, IconsLargerThanTheCapAreRefused) {
    HICON largest = BlankIcon(static_cast<std::int32_t>(kMaxIconSide));
    HICON tooBig = BlankIcon(static_cast<std::int32_t>(kMaxIconSide) + 1);
    ASSERT_NE(largest, nullptr);
    ASSERT_NE(tooBig, nullptr);
    EXPECT_EQ(IconToPixels(Imaging(), largest).width, kMaxIconSide);
    EXPECT_THROW(IconToPixels(Imaging(), tooBig), Win32Error);
    DestroyIcon(largest);
    DestroyIcon(tooBig);
}

TEST_F(IconReading, MonochromeIconsAreMeasuredByHalfTheirMask) {
    HICON largest = MonochromeIcon(static_cast<std::int32_t>(kMaxIconSide));
    HICON tooBig = MonochromeIcon(static_cast<std::int32_t>(kMaxIconSide) + 1);
    ASSERT_NE(largest, nullptr);
    ASSERT_NE(tooBig, nullptr);
    const IconPixels pixels = IconToPixels(Imaging(), largest);
    EXPECT_EQ(pixels.width, kMaxIconSide);
    EXPECT_EQ(pixels.height, kMaxIconSide);
    EXPECT_THROW(IconToPixels(Imaging(), tooBig), Win32Error);
    DestroyIcon(largest);
    DestroyIcon(tooBig);
}

TEST_F(IconReading, AHandleThatIsNoIconIsRefused) {
    EXPECT_THROW(IconToPixels(Imaging(), nullptr), Win32Error);
}
