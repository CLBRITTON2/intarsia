#pragma once

#include "bar/Sections.hpp"
#include "bar/Tooltip.hpp"
#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/ModuleFactory.hpp"
#include "platform/Process.hpp"
#include "render/Graphics.hpp"
#include "render/RenderContext.hpp"
#include "render/Surface.hpp"

#include <windows.h>

#include <exception>
#include <memory>
#include <vector>

enum class ModuleInput {
    LeftClick,
    RightClick,
    ScrollUp,
    ScrollDown
};

struct BarPlacement {
    HMONITOR monitor;
    RECT     monitorRect;
    UINT     dpi;

    bool     operator==(const BarPlacement& other) const;
};

// Process-wide state the window procedures report into, since exceptions cannot unwind through user32.
struct AppState {
    std::exception_ptr error;
};

// One appbar window docked to one edge of one monitor, drawing its own module instances. When the graphics device is
// lost it stops drawing and posts deviceLostMessage to control, since only App can replace the device.
class Bar {
  public:
    Bar(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, const ModuleSources& sources, Launcher& launcher, const BarPlacement& placement, AppState& state,
        HWND control, UINT deviceLostMessage);
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

  private:
    template <typename Owner>
    friend LRESULT CALLBACK OwnedWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    // A lost device from any message is reported instead of ending the process.
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT Dispatch(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    // Everything the constructor does once the window exists.
    void          Build(HINSTANCE instance);
    void          Dock();
    void          PlaceInZOrder(HWND insertAfter);
    RenderContext FrameContext() const;
    // Whether any module's content changed.
    bool LayOutModules();
    void Draw();
    // Where a module was last drawn, for routing clicks to it.
    struct HitBox {
        IModule*    module;
        D2D1_RECT_F bounds;
    };

    std::vector<HitBox> DrawSection(const RenderContext& render, const std::vector<std::unique_ptr<IModule>>& modules, const std::vector<D2D1_SIZE_F>& sizes,
                                    const std::vector<float>& offsets, D2D1_SIZE_F barSize) const;
    void                ScheduleNextChange();
    // Stops drawing and posts to control, once.
    void ReportDeviceLost(const DeviceLostError& error);
    // The box under a point in client pixels, or null between modules.
    const HitBox* BoxAt(POINT client) const;
    // Runs the module's action for the input, or for a left click without one, lets the module handle it.
    void HandleInput(POINT client, ModuleInput input);
    // Restarts hover tracking whenever the cursor moves onto a different module, hiding any tooltip.
    void                               PointAt(POINT client);
    void                               ShowTooltip();

    const GraphicsDevice&              _graphics;
    const Config&                      _config;
    Launcher&                          _launcher;
    IWindowManager&                    _windowManager; // takes the window_manager actions
    AppState&                          _state;
    HWND                               _control;
    UINT                               _deviceLostMessage;
    bool                               _deviceLost = false;
    bool                               _presented  = false;
    BarPlacement                       _placement;
    Sections<std::unique_ptr<IModule>> _modules;
    std::vector<HitBox>                _hitBoxes;
    IModule*                           _pointed = nullptr; // the module under the cursor, null when there is none

    // Null until the constructor has docked the window and knows its size.
    std::unique_ptr<Surface>                     _surface;
    std::unique_ptr<Tooltip>                     _tooltip;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>    _textFormat;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> _backgroundBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> _borderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> _foregroundBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> _accentBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> _alertBrush;

    // Declared last so every member above is constructed before CreateWindowExW starts sending messages.
    HWND _window;
};
