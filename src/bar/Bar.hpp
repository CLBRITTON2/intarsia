#pragma once

#include "bar/Layout.hpp"
#include "bar/Sections.hpp"
#include "bar/Tooltip.hpp"
#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/ModuleFactory.hpp"
#include "platform/FrameClock.hpp"
#include "platform/Process.hpp"
#include "render/BrushCache.hpp"
#include "render/Graphics.hpp"
#include "render/RenderContext.hpp"
#include "render/Surface.hpp"

#include <windows.h>

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <vector>

enum class ModuleInput : std::uint8_t {
    LeftClick,
    RightClick,
    ScrollUp,
    ScrollDown
};

struct BarPlacement {
    HMONITOR      monitor;
    RECT          monitorRect;
    std::uint32_t dpi;

    bool          operator==(const BarPlacement& other) const;
};

// Process-wide state the window procedures report into, since exceptions cannot unwind through user32.
struct AppState {
    std::exception_ptr error;
};

// One appbar window docked to one edge of one monitor, drawing its own module instances. When the graphics device is
// lost it stops drawing and posts deviceLostMessage to control, since only App can replace the device.
class Bar {
  public:
    Bar(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, const ModuleSources& sources, Launcher& launcher, FrameClock& frames,
        const BarPlacement& placement, AppState& state, HWND control, std::uint32_t deviceLostMessage);
    ~Bar();
    Bar(const Bar&)                   = delete;
    Bar&        operator=(const Bar&) = delete;

    static void RegisterWindowClass(HINSTANCE instance);

    // Lays out and draws every module from its current data, then schedules the next self-driven change.
    void Render();

    // Render, drawing only when some module's content changed: an unchanged frame costs a present for nothing.
    void Refresh();

    // True once drawing failed because the graphics device was lost. The bar draws nothing more.
    bool DeviceLost() const;

    // True once a frame reached the screen, which shows the device worked.
    bool HasPresented() const;

    // True while a module animates, so the bar wants every display frame from the frame clock.
    bool Animating() const;

  private:
    template <typename Owner>
    friend LRESULT CALLBACK OwnedWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    // A lost device from any message is reported instead of ending the process.
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT Dispatch(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    // Everything the constructor does once the window exists.
    void Build(HINSTANCE instance);
    void Dock();
    void PlaceInZOrder(HWND insertAfter);
    // What a module lays out and draws with: the bar's fonts and the module's own colors.
    RenderContext ModuleContext(const ModuleStyle& style) const;
    float         BoxCrossLength() const;
    // Whether any module's content changed.
    bool               LayOutModules();
    std::vector<float> BoxLengths(const std::vector<std::unique_ptr<IModule>>& modules, const std::vector<ModuleStyle>& styles) const;
    void               Draw();
    // Where a module's box was last drawn, and where its content starts, for routing clicks to it.
    struct HitBox {
        IModule*      module;
        D2D1_RECT_F   bounds;
        D2D1_POINT_2F contentOrigin;
    };

    std::vector<HitBox> PlaceSection(const std::vector<std::unique_ptr<IModule>>& modules, const std::vector<ModuleStyle>& styles, const std::vector<float>& lengths,
                                     const SectionLayout& layout) const;
    void                DrawSection(const std::vector<HitBox>& boxes, const std::vector<ModuleStyle>& styles, const SectionLayout& layout);
    void                ScheduleNextChange();
    // Stops drawing and posts to control, once.
    void ReportDeviceLost(const DeviceLostError& error);
    // The box under a point in client pixels, or null between modules.
    const HitBox* BoxAt(POINT client) const;
    // A point in client pixels as DIPs from the box's content origin, as modules take input.
    D2D1_POINT_2F ContentPoint(POINT client, const HitBox& box) const;
    // Runs the module's action for the input, or for a click or scroll without one, lets the module handle it.
    void HandleInput(POINT client, ModuleInput input);
    // Hovers the module and item under the cursor, restarting hover tracking and hiding any tooltip whenever either
    // changes. Returns whether one did, and the bar needs drawing.
    bool Point(POINT client);
    // A cursor move: hovers what it is over and draws the bar if that changed.
    void PointAt(POINT client);
    // Hides the tooltip and waits for the cursor to rest again before showing the next one.
    void                               RestartHover();
    void                               ShowTooltip();

    const GraphicsDevice&              _graphics;
    const Config&                      _config;
    Launcher&                          _launcher;
    FrameClock&                        _frames;
    bool                               _animating = false;
    IWindowManager&                    _windowManager; // takes the window_manager actions
    AppState&                          _state;
    HWND                               _control;
    std::uint32_t                      _deviceLostMessage;
    bool                               _deviceLost = false;
    bool                               _presented  = false;
    BarPlacement                       _placement;
    Sections<std::unique_ptr<IModule>> _modules;
    Sections<ModuleStyle>              _styles; // each module's, in the same places as _modules
    std::vector<HitBox>                _hitBoxes;
    IModule*                           _pointed = nullptr; // the module under the cursor, null when there is none
    std::optional<POINT>               _cursor;            // client pixels where the cursor last moved, null once it left

    // Null until the constructor has docked the window and knows its size.
    std::unique_ptr<Surface>                  _surface;
    std::unique_ptr<Tooltip>                  _tooltip;
    std::unique_ptr<BrushCache>               _brushes;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> _textFormat;

    // Declared last so every member above is constructed before CreateWindowExW starts sending messages.
    HWND _window;
};
