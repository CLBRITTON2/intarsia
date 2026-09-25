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

// The command an input runs, or nullopt when the config binds none to it.
const std::optional<Command>& CommandFor(const ModuleActions& actions, ModuleInput input);

struct BarPlacement {
    HMONITOR monitor;
    RECT     monitorRect;
    UINT     dpi;
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
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
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

    const GraphicsDevice&                        graphics_;
    const Config&                                config_;
    Launcher&                                    launcher_;
    AppState&                                    state_;
    BarPlacement                                 placement_;
    Sections<std::unique_ptr<IModule>>           modules_;
    std::vector<HitBox>                          hitBoxes_;

    Microsoft::WRL::ComPtr<IDXGISwapChain1>      swapChain_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext>   context_;
    Microsoft::WRL::ComPtr<IDCompositionTarget>  compositionTarget_;
    Microsoft::WRL::ComPtr<IDCompositionVisual>  visual_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>    textFormat_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> backgroundBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> foregroundBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> alertBrush_;

    // Declared last so every member above is constructed before CreateWindowExW starts sending messages.
    HWND window_;
};
