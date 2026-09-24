#pragma once

#include "appbar.h"
#include "graphics.h"

#include <windows.h>

#include <exception>

struct BarPlacement {
    RECT monitorRect;
    UINT dpi;
    Edge edge;
};

// Process-wide state the window procedures report into, since exceptions cannot unwind through user32.
struct AppState {
    int                liveBars;
    std::exception_ptr error;
};

class BarWindow {
  public:
    BarWindow(HINSTANCE instance, const GraphicsDevice& graphics, const BarPlacement& placement, AppState& state);
    ~BarWindow();
    BarWindow(const BarWindow&)             = delete;
    BarWindow&  operator=(const BarWindow&) = delete;

    static void RegisterWindowClass(HINSTANCE instance);

  private:
    static LRESULT CALLBACK                      WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT                                      HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    void                                         Dock();
    void                                         CreateSwapChain(UINT width, UINT height);
    void                                         ResizeSwapChain(UINT width, UINT height);
    void                                         BindRenderTarget();
    void                                         Render();
    void                                         ScheduleClockTick();

    const GraphicsDevice&                        graphics_;
    AppState&                                    state_;
    BarPlacement                                 placement_;

    Microsoft::WRL::ComPtr<IDXGISwapChain1>      swapChain_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext>   context_;
    Microsoft::WRL::ComPtr<IDCompositionTarget>  compositionTarget_;
    Microsoft::WRL::ComPtr<IDCompositionVisual>  visual_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>    textFormat_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> backgroundBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> foregroundBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> iconBrush_;

    // Declared last so every member above is constructed before CreateWindowExW starts sending messages.
    HWND window_;
};
