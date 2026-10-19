#pragma once

#include "bar/Layout.hpp"
#include "bar/Menu.hpp"
#include "bar/Tooltip.hpp"
#include "config/Config.hpp"
#include "modules/AnimatedValue.hpp"
#include "modules/IModule.hpp"
#include "modules/ModuleFactory.hpp"
#include "platform/AppBar.hpp"
#include "platform/Launcher.hpp"
#include "platform/Monitors.hpp"
#include "platform/Win32Window.hpp"
#include "render/Axis.hpp"
#include "render/Box.hpp"
#include "render/FrameClock.hpp"
#include "render/Graphics.hpp"

#include <windows.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

// The monitor a bar docks to, where it is on the desktop and its scale.
struct BarPlacement {
    HMONITOR monitor;
    RECT monitorRect;
    std::uint32_t dpi;

    bool operator==(const BarPlacement& other) const;
};

// Each monitor's placement, in the order given. Throws MonitorGoneError for a monitor that went while it was read.
std::vector<BarPlacement> PlacementsFor(const std::vector<HMONITOR>& monitors);

// Whether the monitors now differ from placements and descriptions, as a monitor added, removed, moved, resized,
// rescaled or swapped for another makes them.
bool MonitorsDiffer(const std::vector<BarPlacement>& placements, const std::vector<MonitorDescription>& descriptions);

// One appbar window docked to one edge of one monitor, drawing its own module instances. When the graphics device is
// lost it stops drawing and posts deviceLostMessage to control, since only App can replace the device.
class Bar {
  public:
    Bar(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, const ModuleSources& sources, Launcher& launcher, const BarPlacement& placement,
        AppBarRoute route, WindowProcError& windowProcError, HWND control, std::uint32_t deviceLostMessage);
    ~Bar();
    Bar(const Bar&) = delete;
    Bar& operator=(const Bar&) = delete;

    static void RegisterWindowClass(HINSTANCE instance);

    // True once drawing failed because the graphics device was lost. The bar draws nothing more.
    bool DeviceLost() const;

    // True once a frame reached the screen, which shows the device worked.
    bool HasPresented() const;

    // Lays out and draws every module from its current data, then schedules the next self-driven change.
    void Render();

    // Render, drawing only when some module's content changed: an unchanged frame costs a present for nothing.
    void Refresh();

    // Draws the modules as last laid out, for a change of colors alone, such as the hover moving, then schedules the
    // next change, so a fade the change started runs on the frame clock.
    void Redraw();

    // Blurs the monitor's wallpaper again, as after the desktop's changed, and draws the bar with it.
    void ReloadWallpaper();

  private:
    template <typename Owner>
    friend LRESULT CALLBACK OwnedWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    // Everything the constructor does once the window exists.
    void Build(HINSTANCE instance);
    // Stops drawing and posts to control, once.
    void ReportDeviceLost(const DeviceLostError& error);

    // A lost device from any message is reported instead of ending the process.
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT Dispatch(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    void Dock();
    void PlaceInZOrder(HWND insertAfter);
    // The canvas at the window's client size and the bar's DPI, drawn again.
    void ResizeCanvas();

    // Sets the time the next frame draws at, in the bar and in every module's context.
    void StampFrameTime();
    std::chrono::steady_clock::time_point NextFrameTime() const;
    void ScheduleNextChange();
    // How long until the bar next changes by itself: 0 while a box fades, else the earliest module change, null when
    // none is coming.
    std::optional<std::uint32_t> NextChangeWait() const;
    bool BoxFading() const;

    // Every module's context, made again whenever the surface's size changes, since each spans the bar's thickness.
    void BuildContexts();
    // What a module lays out and draws with: the bar's fonts and the module's own colors.
    RenderContext ModuleContext(const ModuleStyle& style) const;

    // Whether any module's content changed.
    bool LayOutModules();
    BarGeometry Geometry() const;
    // Places every module's box for the frame, tells each module where its content lands on screen, and hovers what
    // now lies under a cursor that has not moved.
    PlacedBar PlaceModules();

    void Draw();
    void Paint(const PlacedBar& placed);
    void DrawSection(const std::vector<PlacedEntry>& entries, const SectionLayout& layout);
    // The group's box, its hover fading in over the toggle's span alone under the cursor, the toggle, then its members.
    void DrawGroup(const PlacedEntry& placed);
    // The module's box (its background, or fading to its hover under the cursor), then the module.
    void DrawModule(const HitBox& box, const ModuleStyle& style);
    // The box of each section that holds a module.
    std::vector<D2D1_RECT_F> SectionBoxes(const std::vector<const SectionLayout*>& layouts) const;
    RECT WindowRect() const;

    // The box under a point in client pixels, or null between modules.
    const HitBox* BoxAt(POINT client) const;
    // A point in client pixels as DIPs from the box's content origin, as modules take input.
    D2D1_POINT_2F ContentPoint(POINT client, const HitBox& box) const;
    // Runs the module's action for the input, or for a click or scroll without one, lets the module handle it. Any input
    // closes the menu, and a menu action starts listing unless it is the click toggling its own module's menu closed.
    void HandleInput(POINT client, ModuleInput input);
    // A cursor move: hovers what it is over and draws the bar if that changed.
    void MoveCursor(POINT client);
    // Hovers the module and item under the cursor, restarting hover tracking and hiding any tooltip whenever either
    // changes. Returns whether one did, and the bar needs drawing.
    bool HoverAt(POINT client);
    // Fades every box toward its fill for where the cursor now is.
    void FadeBoxesToHover();
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
        Command onSelect;
    };

    const GraphicsDevice& _graphics;
    const Config& _config;
    const Axis _axis;
    Launcher& _launcher;
    std::unique_ptr<FrameClock> _frames; // paced by this bar's monitor, null until the window exists
    bool _animating = false;
    std::chrono::steady_clock::time_point _frameTime; // the RenderContext::frameTime of the frame last drawn
    std::optional<FrameStats> _animationFrames;       // null while no animation runs
    IWindowManager& _windowManager;                   // takes the window_manager actions
    WindowProcError& _windowProcError;
    HWND _control;
    std::uint32_t _deviceLostMessage;
    bool _deviceLost = false;
    bool _presented = false;
    BarPlacement _placement;
    AppBarRoute _route;
    Sections<ModuleEntry> _modules;
    std::vector<HitBox> _hitBoxes; // a group's members before the group, since BoxAt takes the first match
    IModule* _hovered = nullptr;   // the module under the cursor, null when there is none
    std::optional<POINT> _cursor;  // client pixels where the cursor last moved, null once it left
    std::uint32_t _taskbarCreated;
    std::optional<MenuOwner> _menuOwner; // null once any other input came after the menu's

    // Before the tooltip and menu, which hold it.
    std::vector<WallpaperBlur> _wallpapers;
    // Null until the constructor has docked the window and knows its size.
    std::unique_ptr<Canvas> _canvas;
    std::unique_ptr<Tooltip> _tooltip;
    std::unique_ptr<Menu> _menu;
    std::unique_ptr<MenuLister> _lister;                             // posts to the window, so it stops before the window goes
    std::unordered_map<const ModuleStyle*, RenderContext> _contexts; // by the style of the entry it belongs to
    std::unordered_map<const IModule*, ColorFade> _boxFades;         // every module's box fill, members included
    Microsoft::WRL::ComPtr<IDWriteTextFormat> _textFormat;

    // Declared last so every member above is constructed before CreateWindowExW starts sending messages.
    HWND _window;
};
