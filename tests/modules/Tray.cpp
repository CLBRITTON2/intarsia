#include "modules/Tray.hpp"

#include "../tray/FakeTray.hpp"
#include "TestStyle.hpp"
#include "TextOnlyRender.hpp"
#include "render/BrushCache.hpp"
#include "render/CompositionThread.hpp"
#include "render/Graphics.hpp"
#include "support/FirstAdapter.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace {

    const TrayConfig kConfig{.icon = L">"};

    TrayIcon         Icon(std::uint32_t id, std::shared_ptr<const IconPixels> pixels, bool hidden) {
        return TrayIcon{.owner           = 1,
                                .id              = id,
                                .guid            = std::nullopt,
                                .ownerImage      = {},
                                .callbackMessage = WM_APP,
                                .version         = 0,
                                .tooltip         = L"icon",
                                .standardTooltip = false,
                                .pixels          = std::move(pixels),
                                .hidden          = hidden};
    }

    std::shared_ptr<const IconPixels> Pixels() {
        return std::make_shared<const IconPixels>(IconPixels{.width = 2, .height = 2, .pbgra = std::vector<std::uint8_t>(16, 255)});
    }

    // Bitmaps need a device, and the toggle's text needs DirectWrite.
    class TrayModule : public testing::Test {
      public:
        TrayModule() : _graphics(CreateGraphicsDevice(FirstAdapter().Get())) {
            ThrowIfFailed(_graphics.d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &_context), "CreateDeviceContext");
            ThrowIfFailed(_context->CreateBitmap(D2D1::SizeU(200, 64), nullptr, 0,
                                                 D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                                                 &_target),
                          "CreateBitmap(target)");
            _context->SetTarget(_target.Get());
            _brushes           = std::make_unique<BrushCache>(_context.Get());
            _render            = _text.Context();
            _render.context    = _context.Get();
            _render.brushes    = _brushes.get();
            _render.foreground = _brushes->Keep(D2D1::ColorF(0xFFFFFF));
            _render.accent     = _brushes->Keep(D2D1::ColorF(0x00FFFF));
            _tray.icons        = {Icon(1, Pixels(), false), Icon(2, Pixels(), true), Icon(3, nullptr, false), Icon(4, Pixels(), false)};
        }

      protected:
        // The middle of the module, across and along.
        D2D1_POINT_2F At(const Tray& module, float x) const {
            return D2D1::Point2F(x, module.Size().height / 2.0f);
        }

        CompositionThread                          _compositionThread;
        GraphicsDevice                             _graphics;
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> _context;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1>       _target;
        std::unique_ptr<BrushCache>                _brushes;
        TextOnlyRender                             _text;
        RenderContext                              _render{};
        FakeTray                                   _tray;
    };

} // namespace

TEST(Tray, ShownIconsSkipsHiddenIconsAndIconsWithoutPixels) {
    const std::vector<TrayIcon> shown = ShownIcons({Icon(1, Pixels(), false), Icon(2, Pixels(), true), Icon(3, nullptr, false)});
    ASSERT_EQ(shown.size(), 1u);
    EXPECT_EQ(shown[0].id, 1u);
}

TEST_F(TrayModule, TheToggleTakesItsOwnStyleAndTheIconsKeepTheItemStyle) {
    ModuleStyle style = TestModuleStyle();
    Tray        plain(kConfig, _tray, style);
    EXPECT_TRUE(plain.Layout(_render));
    EXPECT_FALSE(plain.Layout(_render));
    style.toggle.padding.xDips = 10.0f;
    style.toggle.margin.xDips  = 1.0f;
    Tray padded(kConfig, _tray, style);
    padded.Layout(_render);
    // 6 more padding and 4 less margin on either side.
    const float folded = padded.Size().width;
    EXPECT_FLOAT_EQ(folded, plain.Size().width + 4.0f);

    padded.Click(At(padded, folded / 2.0f));
    EXPECT_TRUE(padded.Layout(_render));
    // Icons 1 and 4, each a 20 icon padded 4 across with 5 of margin on either side.
    EXPECT_NEAR(padded.Size().width, folded + (2.0f * 38.0f), 1.0f);
    EXPECT_TRUE(_tray.presses.empty());
    _context->BeginDraw();
    padded.Draw(_render, D2D1::Point2F(0.0f, 0.0f));
    EXPECT_TRUE(SUCCEEDED(_context->EndDraw()));

    // The toggle is last, and its own margin is where the icons end, so a click just inside its box folds the tray.
    padded.Click(At(padded, padded.Size().width - 2.0f));
    EXPECT_TRUE(padded.Layout(_render));
    EXPECT_FLOAT_EQ(padded.Size().width, folded);
}

TEST_F(TrayModule, UnfoldsOverItsTransitionAndAsksForEveryFrameMeanwhile) {
    ModuleStyle style     = TestModuleStyle();
    style.transition      = Transition{.duration = std::chrono::milliseconds(300), .timing = TimingFunction::Linear};
    style.icon.transition = Transition{.duration = std::chrono::milliseconds(300), .timing = TimingFunction::Linear};
    Tray module(kConfig, _tray, style);
    module.Layout(_render);
    const float folded = module.Size().width;
    EXPECT_EQ(module.MillisecondsUntilChange(), std::nullopt);

    module.Click(At(module, folded / 2.0f));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    EXPECT_TRUE(module.Layout(_render));
    EXPECT_EQ(module.MillisecondsUntilChange(), 0u);
    const float midway = module.Size().width;
    EXPECT_GT(midway, folded);
    EXPECT_LT(midway, folded + (2.0f * 38.0f));
    _context->BeginDraw();
    module.Draw(_render, D2D1::Point2F(0.0f, 0.0f));
    EXPECT_TRUE(SUCCEEDED(_context->EndDraw()));

    // The toggle moved right with the box's edge, and a click on it there folds the tray back from where it is.
    module.Click(At(module, midway - 6.0f));
    EXPECT_TRUE(module.Layout(_render));
    const float folding = module.Size().width;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_TRUE(module.Layout(_render));
    EXPECT_LT(module.Size().width, folding);
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    module.Layout(_render);
    EXPECT_FLOAT_EQ(module.Size().width, folded);
    EXPECT_EQ(module.MillisecondsUntilChange(), std::nullopt);
    EXPECT_FALSE(module.Layout(_render));
}

TEST_F(TrayModule, ClicksReachTheIconsApp) {
    Tray module(kConfig, _tray, TestModuleStyle());
    module.Layout(_render);
    module.Click(At(module, module.Size().width / 2.0f));
    module.Layout(_render);

    // Icons at 5 to 33 and 43 to 71.
    module.Click(At(module, 19.0f));
    module.RightClick(At(module, 57.0f));
    module.RightClick(At(module, 38.0f));
    ASSERT_EQ(_tray.presses.size(), 2u);
    EXPECT_EQ(_tray.presses[0].id, 1u);
    EXPECT_EQ(_tray.presses[0].button, TrayButton::Left);
    EXPECT_EQ(_tray.presses[1].id, 4u);
    EXPECT_EQ(_tray.presses[1].button, TrayButton::Right);

    // A right click on the toggle does nothing.
    module.RightClick(At(module, module.Size().width - 6.0f));
    EXPECT_EQ(_tray.presses.size(), 2u);

    module.MiddleClick(At(module, 19.0f));
    module.MiddleClick(At(module, module.Size().width - 6.0f));
    ASSERT_EQ(_tray.presses.size(), 3u);
    EXPECT_EQ(_tray.presses[2].id, 1u);
    EXPECT_EQ(_tray.presses[2].button, TrayButton::Middle);
}

TEST_F(TrayModule, DownAVerticalBarTheIconsUnfoldDownwardAndAreClickedWhereTheyShow) {
    RenderContext vertical = _render;
    vertical.axis          = Axis::Vertical;
    Tray module(kConfig, _tray, TestModuleStyle());
    module.Layout(vertical);
    const D2D1_SIZE_F folded = module.Size();
    module.Click(D2D1::Point2F(folded.width / 2.0f, folded.height / 2.0f));
    module.Layout(vertical);
    // Icons 1 and 4, each a 20 icon padded 2 down with no margin there, at 0 to 24 and 24 to 48.
    EXPECT_FLOAT_EQ(module.Size().height, folded.height + 48.0f);
    module.Click(D2D1::Point2F(module.Size().width / 2.0f, 36.0f));
    ASSERT_EQ(_tray.presses.size(), 1u);
    EXPECT_EQ(_tray.presses[0].id, 4u);
    _context->BeginDraw();
    module.Draw(vertical, D2D1::Point2F(0.0f, 0.0f));
    EXPECT_TRUE(SUCCEEDED(_context->EndDraw()));

    // The toggle is last, below the icons.
    module.Click(D2D1::Point2F(module.Size().width / 2.0f, module.Size().height - 2.0f));
    module.Layout(vertical);
    EXPECT_FLOAT_EQ(module.Size().height, folded.height);
}

TEST_F(TrayModule, MovesOverAnIconReachItsAppAndMovesElsewhereDoNot) {
    Tray module(kConfig, _tray, TestModuleStyle());
    module.Layout(_render);
    module.Click(At(module, module.Size().width / 2.0f));
    module.Layout(_render);

    module.Hover(At(module, 19.0f));
    module.Hover(At(module, 57.0f));
    module.Hover(At(module, 38.0f));
    module.Hover(At(module, module.Size().width - 6.0f));
    module.Hover(std::nullopt);
    EXPECT_EQ(_tray.moves, (std::vector<std::uint32_t>{1u, 4u}));
}

TEST_F(TrayModule, TheIconUnderTheCursorGivesTheTooltip) {
    Tray module(kConfig, _tray, TestModuleStyle());
    module.Layout(_render);
    EXPECT_TRUE(module.Hover(At(module, module.Size().width / 2.0f)));
    EXPECT_EQ(module.TooltipText(), std::nullopt);
    module.Click(At(module, module.Size().width / 2.0f));
    module.Layout(_render);

    _tray.icons[3].tooltip = L"fourth";
    module.Layout(_render);
    EXPECT_TRUE(module.Hover(At(module, 57.0f)));
    EXPECT_EQ(module.TooltipText(), L"fourth");
    // The tooltip sits against the second icon's box, not the whole module.
    const D2D1_RECT_F item = module.TooltipItem().value_or(D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(item.left, 43.0f);
    EXPECT_FLOAT_EQ(item.right, 71.0f);
    EXPECT_FALSE(module.Hover(At(module, 60.0f)));
    EXPECT_TRUE(module.Hover(std::nullopt));
    EXPECT_EQ(module.TooltipText(), std::nullopt);
}

TEST_F(TrayModule, AnIconWhoseAppDrawsItsOwnPopupOpensItOnRestAndClosesItOnLeaving) {
    _tray.icons[0].version = 4;
    Tray module(kConfig, _tray, TestModuleStyle());
    module.Layout(_render);
    module.Click(At(module, module.Size().width / 2.0f));
    module.Layout(_render);

    module.Hover(At(module, 19.0f));
    EXPECT_EQ(module.TooltipText(), std::nullopt);
    // Resting again opens nothing more.
    EXPECT_EQ(module.TooltipText(), std::nullopt);
    EXPECT_EQ(_tray.opened, std::vector<std::uint32_t>{1u});
    module.Hover(At(module, 20.0f));
    EXPECT_TRUE(_tray.closed.empty());

    // The next icon has the bar's tooltip, and moving to it closes the popup.
    module.Hover(At(module, 57.0f));
    EXPECT_EQ(_tray.closed, std::vector<std::uint32_t>{1u});
    EXPECT_EQ(module.TooltipText(), L"icon");

    module.Hover(At(module, 19.0f));
    module.TooltipText();
    module.Hover(std::nullopt);
    EXPECT_EQ(_tray.opened, (std::vector<std::uint32_t>{1u, 1u}));
    EXPECT_EQ(_tray.closed, (std::vector<std::uint32_t>{1u, 1u}));
}

TEST_F(TrayModule, TellsTheTrayWhereItsUncoveredIconsAreOnScreen) {
    const ModulePlacement placement{.window = RECT{.left = 1000, .top = 20, .right = 3000, .bottom = 80}, .contentOrigin = D2D1::Point2F(100.0f, 4.0f), .dpi = 192};
    {
        Tray module(kConfig, _tray, TestModuleStyle());
        module.Layout(_render);
        module.Place(placement);
        EXPECT_TRUE(_tray.rects.empty());

        module.Click(At(module, module.Size().width / 2.0f));
        module.Layout(_render);
        module.Place(placement);
        // Icons at 5 to 33 and 43 to 71 DIPs from the content, at twice the pixels.
        ASSERT_EQ(_tray.rects.size(), 2u);
        EXPECT_EQ(_tray.rects[0].id, 1u);
        EXPECT_EQ(_tray.rects[0].rect.left, 1000 + 210);
        EXPECT_EQ(_tray.rects[0].rect.right, 1000 + 266);
        EXPECT_EQ(_tray.rects[1].id, 4u);
        EXPECT_EQ(_tray.rects[1].rect.left, 1000 + 286);
    }
    EXPECT_TRUE(_tray.rects.empty());
}

TEST_F(TrayModule, TellsTheTrayOnlyWhenTheIconsMove) {
    const ModulePlacement placement{.window = RECT{.left = 1000, .top = 20, .right = 3000, .bottom = 80}, .contentOrigin = D2D1::Point2F(100.0f, 4.0f), .dpi = 96};
    Tray                  module(kConfig, _tray, TestModuleStyle());
    module.Layout(_render);
    module.Click(At(module, module.Size().width / 2.0f));
    module.Layout(_render);
    module.Place(placement);
    ASSERT_FALSE(_tray.rects.empty());
    const size_t calls = _tray.showCalls;
    module.Place(placement);
    EXPECT_EQ(_tray.showCalls, calls);
    module.Place(ModulePlacement{.window = placement.window, .contentOrigin = D2D1::Point2F(120.0f, 4.0f), .dpi = 96});
    EXPECT_EQ(_tray.showCalls, calls + 1);
}

TEST_F(TrayModule, IconsThatChangeWhileFoldedShowOnTheNextUnfold) {
    Tray module(kConfig, _tray, TestModuleStyle());
    module.Layout(_render);
    const float folded = module.Size().width;
    module.Click(At(module, folded / 2.0f));
    module.Layout(_render);
    module.Click(At(module, module.Size().width - (folded / 2.0f)));
    EXPECT_TRUE(module.Layout(_render));
    EXPECT_FLOAT_EQ(module.Size().width, folded);

    _tray.icons[1].hidden = false;
    _tray.icons[0].pixels = Pixels();
    EXPECT_FALSE(module.Layout(_render));
    EXPECT_FLOAT_EQ(module.Size().width, folded);
    module.Place(ModulePlacement{.window = RECT{.left = 0, .top = 0, .right = 2000, .bottom = 60}, .contentOrigin = D2D1::Point2F(0.0f, 0.0f), .dpi = 96});
    EXPECT_TRUE(_tray.rects.empty());

    module.Click(At(module, folded / 2.0f));
    EXPECT_TRUE(module.Layout(_render));
    EXPECT_NEAR(module.Size().width, folded + (3.0f * 38.0f), 1.0f);
    _context->BeginDraw();
    module.Draw(_render, D2D1::Point2F(0.0f, 0.0f));
    EXPECT_TRUE(SUCCEEDED(_context->EndDraw()));
}

TEST_F(TrayModule, NewIconsAndNewPixelsAreChangesButATooltipIsNot) {
    Tray module(kConfig, _tray, TestModuleStyle());
    module.Layout(_render);
    module.Click(At(module, module.Size().width / 2.0f));
    module.Layout(_render);

    _tray.icons[0].tooltip = L"changed";
    EXPECT_FALSE(module.Layout(_render));
    _tray.icons[0].pixels = Pixels();
    EXPECT_TRUE(module.Layout(_render));
    _tray.icons[1].hidden = false;
    EXPECT_TRUE(module.Layout(_render));
    EXPECT_FALSE(module.Layout(_render));
}
