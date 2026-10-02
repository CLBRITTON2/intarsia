#pragma once

#include "bar/FrameStats.hpp"
#include "bar/Layout.hpp"
#include "bar/Menu.hpp"
#include "bar/MenuLister.hpp"
#include "bar/Sections.hpp"
#include "bar/Tooltip.hpp"
#include "config/Config.hpp"
#include "modules/AnimatedValue.hpp"
#include "modules/IModule.hpp"
#include "modules/ModuleFactory.hpp"
#include "platform/AppBar.hpp"
#include "platform/FrameClock.hpp"
#include "platform/Process.hpp"
#include "render/Axis.hpp"
#include "render/BrushCache.hpp"
#include "render/Graphics.hpp"
#include "render/RenderContext.hpp"
#include "render/Surface.hpp"

#include <windows.h>

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

enum class ModuleInput : std::uint8_t {
    LeftClick,
    RightClick,
    MiddleClick,
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
    // graphics is the device on output's adapter.
    Bar(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, const ModuleSources& sources, Launcher& launcher, const BarPlacement& placement,
        const MonitorOutput& output, AppBarRoute route, AppState& state, HWND control, std::uint32_t deviceLostMessage);
    ~Bar();
    Bar(const Bar&)                   = delete;
    Bar&        operator=(const Bar&) = delete;

    static void RegisterWindowClass(HINSTANCE instance);

    // Lays out and draws every module from its current data, then schedules the next self-driven change.
    void Render();

    // Render, drawing only when some module's content changed: an unchanged frame costs a present for nothing.
    void Refresh();

    // Draws the modules as last laid out, for a change of colors alone, such as the hover moving, then schedules the
    // next change, so a fade the change started runs on the frame clock.
    void Redraw();

    // True once drawing failed because the graphics device was lost. The bar draws nothing more.
    bool DeviceLost() const;

    // True once a frame reached the screen, which shows the device worked.
    bool HasPresented() const;

  private:
    template <typename Owner>
    friend LRESULT CALLBACK OwnedWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    // A lost device from any message is reported instead of ending the process.
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT Dispatch(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    // Everything the constructor does once the window exists.
    void Build(HINSTANCE instance, const MonitorOutput& output);
    void Dock();
    void PlaceInZOrder(HWND insertAfter);
    // What a module lays out and draws with: the bar's fonts and the module's own colors.
    RenderContext ModuleContext(const ModuleStyle& style) const;
    // Every module's context, made again whenever the surface's size changes, since each spans the bar's thickness.
    void                 BuildContexts();
    const RenderContext& ContextFor(const ModuleStyle& style) const;
    float                BoxCrossLength() const;
    // Whether any module's content changed.
    bool LayOutModules();
    // A group's length takes in the revealed share of its members'.
    std::vector<float> BoxLengths(const std::vector<ModuleEntry>& entries) const;
    GroupLayout        ArrangeMembers(const ModuleEntry& entry, const std::vector<float>& memberLengths) const;
    void               Draw();
    // Where a module's box was last drawn, and where its content starts, for routing clicks to it.
    struct HitBox {
        IModule*      module;
        D2D1_RECT_F   bounds;
        D2D1_POINT_2F contentOrigin;
    };
    // One entry as placed for a frame: its box and, for a group, its toggle's span, its members' boxes in its members'
    // order, and the span the fold uncovers of them, outside of which they neither draw nor take the cursor.
    struct PlacedEntry {
        const ModuleEntry*  entry;
        HitBox              box;
        D2D1_RECT_F         toggle;
        std::vector<HitBox> members;
        D2D1_RECT_F         shown;
    };

    HitBox                   PlaceBox(const ModuleEntry& entry, float start, float length) const;
    HitBox                   PlaceInRoom(const ModuleEntry& entry, float start, float length) const;
    std::vector<PlacedEntry> PlaceSection(const std::vector<ModuleEntry>& entries, const std::vector<float>& lengths, const SectionLayout& layout) const;
    // The section's box, inside the bar's margin.
    D2D1_RECT_F                    SectionBounds(const SectionLayout& layout) const;
    std::vector<D2D1_ROUNDED_RECT> SectionBackdrops(const std::vector<const SectionLayout*>& layouts) const;
    void                           DrawSection(const std::vector<PlacedEntry>& entries, const SectionLayout& layout);
    // The module's box (its background, or fading to its hover under the cursor), then the module.
    void DrawModule(const HitBox& box, const ModuleStyle& style);
    // The group's box, its hover fading in over the toggle's span alone under the cursor, the toggle, then its members.
    void DrawGroup(const PlacedEntry& placed);
    // Fades every box toward its fill for where the cursor now is.
    void FollowPointer();
    bool BoxFading() const;
    void ScheduleNextChange();
    // Stops drawing and posts to control, once.
    void ReportDeviceLost(const DeviceLostError& error);
    // The box under a point in client pixels, or null between modules.
    const HitBox* BoxAt(POINT client) const;
    // A point in client pixels as DIPs from the box's content origin, as modules take input.
    D2D1_POINT_2F ContentPoint(POINT client, const HitBox& box) const;
    // Runs the module's action for the input, or for a click or scroll without one, lets the module handle it. Any input
    // closes the menu, and a menu action starts listing unless it is the click toggling its own module's menu closed.
    void HandleInput(POINT client, ModuleInput input);
    // Hovers the module and item under the cursor, restarting hover tracking and hiding any tooltip whenever either
    // changes. Returns whether one did, and the bar needs drawing.
    bool Point(POINT client);
    // A cursor move: hovers what it is over and draws the bar if that changed.
    void PointAt(POINT client);
    // Hides the tooltip and waits for the cursor to rest again before showing the next one.
    void RestartHover();
    void ShowTooltip();
    // Where a popup beside a span of the bar goes, the span in DIPs from the bar's top left.
    TooltipAnchor AnchorFor(const D2D1_RECT_F& span) const;
    // Shows the finished listing beside the module that asked for it, unless another click has moved on since.
    void ShowMenu();

    // The module whose menu action the shown or pending menu belongs to.
    struct MenuOwner {
        IModule* module;
        Command  onSelect;
    };

    const GraphicsDevice&       _graphics;
    const Config&               _config;
    const Axis                  _axis;
    Launcher&                   _launcher;
    std::unique_ptr<FrameClock> _frames; // paced by this bar's monitor, null until the window exists
    bool                        _animating = false;
    std::optional<FrameStats>   _animationFrames; // null while no animation runs
    IWindowManager&             _windowManager;   // takes the window_manager actions
    AppState&                   _state;
    HWND                        _control;
    std::uint32_t               _deviceLostMessage;
    bool                        _deviceLost = false;
    bool                        _presented  = false;
    BarPlacement                _placement;
    AppBarRoute                 _route;
    Sections<ModuleEntry>       _modules;
    std::vector<HitBox>         _hitBoxes;          // a group's members before the group, since BoxAt takes the first match
    IModule*                    _pointed = nullptr; // the module under the cursor, null when there is none
    std::optional<POINT>        _cursor;            // client pixels where the cursor last moved, null once it left
    std::uint32_t               _taskbarCreated;
    std::optional<MenuOwner>    _menuOwner; // null once any other input came after the menu's

    // Null until the constructor has docked the window and knows its size.
    std::unique_ptr<Surface>                              _surface;
    std::unique_ptr<Tooltip>                              _tooltip;
    std::unique_ptr<Menu>                                 _menu;
    std::unique_ptr<MenuLister>                           _lister; // posts to the window, so it stops before the window goes
    std::unique_ptr<BrushCache>                           _brushes;
    std::unordered_map<const ModuleStyle*, RenderContext> _contexts; // by the style of the entry it belongs to
    std::unordered_map<const IModule*, ColorFade>         _boxFades; // every module's box fill, members included
    Microsoft::WRL::ComPtr<IDWriteTextFormat>             _textFormat;

    // Declared last so every member above is constructed before CreateWindowExW starts sending messages.
    HWND _window;
};
