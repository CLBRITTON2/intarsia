#pragma once

#include "bar/Sections.hpp"
#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/ModuleFactory.hpp"
#include "platform/Process.hpp"
#include "render/Graphics.hpp"
#include "render/RenderContext.hpp"

#include <windows.h>

#include <exception>
#include <memory>

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

// One appbar window docked to one edge of one monitor, drawing its own module instances.
class Bar {
  public:
    Bar(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, const ModuleSources& sources, Launcher& launcher, const BarPlacement& placement, AppState& state);
    ~Bar();
    Bar(const Bar&)                   = delete;
    Bar&        operator=(const Bar&) = delete;

    static void RegisterWindowClass(HINSTANCE instance);

    // Lays out and draws every module from its current data, then schedules the next self-driven change.
    void Render();

  private:
    template <typename Owner>
    friend LRESULT CALLBACK OwnedWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT                 HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    void                    Dock();
    void                    CreateSwapChain(UINT width, UINT height);
    void                    ResizeSwapChain(UINT width, UINT height);
    void                    BindRenderTarget();
    RenderContext           FrameContext() const;
    // Where a module was last drawn, for routing clicks to it.
    struct HitBox {
        IModule*    module;
        D2D1_RECT_F bounds;
    };

    std::vector<HitBox> DrawSection(const RenderContext& render, const std::vector<std::unique_ptr<IModule>>& modules, const std::vector<D2D1_SIZE_F>& sizes,
                                    const std::vector<float>& offsets, D2D1_SIZE_F barSize) const;
    void                ScheduleNextChange();
    // Runs the module's command for the input, or for a left click without one, lets the module handle it.
    void                                         HandleInput(POINT client, ModuleInput input);

    const GraphicsDevice&                        _graphics;
    const Config&                                _config;
    Launcher&                                    _launcher;
    AppState&                                    _state;
    BarPlacement                                 _placement;
    Sections<std::unique_ptr<IModule>>           _modules;
    std::vector<HitBox>                          _hitBoxes;

    Microsoft::WRL::ComPtr<IDXGISwapChain1>      _swapChain;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext>   _context;
    Microsoft::WRL::ComPtr<IDCompositionTarget>  _compositionTarget;
    Microsoft::WRL::ComPtr<IDCompositionVisual>  _visual;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>    _textFormat;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> _backgroundBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> _borderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> _foregroundBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> _accentBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> _alertBrush;

    // Declared last so every member above is constructed before CreateWindowExW starts sending messages.
    HWND _window;
};
