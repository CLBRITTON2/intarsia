#pragma once

#include "bar/Canvas.hpp"
#include "bar/MenuLayout.hpp"
#include "bar/MenuLister.hpp"
#include "bar/Tooltip.hpp"
#include "config/Config.hpp"
#include "platform/Launcher.hpp"
#include "platform/WindowProc.hpp"
#include "render/Graphics.hpp"

#include <d2d1_1.h>
#include <dwrite.h>
#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

// A popup listing a menu command's entries beside the module that opened it. It takes focus while shown, so the arrow
// keys, Enter and Esc work, and hides once another window takes it. Picking an entry runs on_select with the entry's
// id as its last argument. Each entry, and each separator, takes one line, cut with an ellipsis at max-width, and
// entries past max-height scroll by wheel, arrow keys and the scroll bar's thumb. An entry's tooltip shows in the bar's
// tooltip once the cursor rests on it.
class Menu {
  public:
    // onDeviceLost receives a lost device met while drawing from the menu's own messages.
    Menu(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, IDWriteTextFormat* textFormat, Launcher& launcher, WindowProcError& windowProcError,
         Tooltip& tooltip, std::function<void(const DeviceLostError&)> onDeviceLost);
    Menu(const Menu&)                  = delete;
    Menu&       operator=(const Menu&) = delete;

    static void RegisterWindowClass(HINSTANCE instance);

    // A failure shows as one row of its reason that cannot be picked. Throws DeviceLostError when the graphics device is
    // gone.
    void Show(const MenuListing& listing, const Command& onSelect, const TooltipAnchor& anchor);
    void Hide();
    bool Shown() const;

  private:
    template <typename Owner>
    friend LRESULT CALLBACK OwnedWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT                 HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT                 Dispatch(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    MenuRow                 LayOutRow(std::wstring_view text, const ItemStyle& style, std::optional<size_t> entry, float maxWidthDips, IDWriteInlineObject* ellipsis) const;
    void                    Draw();
    void                    Highlight(std::optional<size_t> entry);
    void                    Pick(size_t entry);
    void                    Scroll(const MenuScroll& scroll);
    void                    Wheel(std::int16_t delta);
    void                    Step(MenuStep step);
    // Starts dragging the thumb from point, which first jumps the thumb's middle there when point misses it.
    void GrabThumb(D2D1_POINT_2F point);
    // Hides the entry's tooltip and waits for the cursor to rest again before showing the next one.
    void                                        RestartHover();
    void                                        ShowTooltip();

    const GraphicsDevice&                       _graphics;
    const Config&                               _config;
    IDWriteTextFormat*                          _textFormat;
    Launcher&                                   _launcher;
    WindowProcError&                            _windowProcError;
    Tooltip&                                    _tooltip;
    std::function<void(const DeviceLostError&)> _onDeviceLost;
    bool                                        _shown = false;
    std::uint32_t                               _dpi   = USER_DEFAULT_SCREEN_DPI;
    RECT                                        _rect{}; // the shown menu, in screen pixels
    RECT                                        _monitorRect{};
    std::vector<MenuEntry>                      _entries;
    std::vector<MenuRow>                        _rows;
    std::optional<size_t>                       _highlight; // the entry under the cursor or picked with the arrow keys
    Command                                     _onSelect;
    MenuScroll                                  _scroll{};
    D2D1_RECT_F                                 _gutter{}; // beside the viewport, where the track takes clicks
    D2D1_RECT_F                                 _track{};
    std::optional<float>                        _thumbGrabDips; // while dragging the thumb, how far below its top the cursor holds it
    // Only while shown: Show creates it at the rows' size and Hide releases it.
    std::unique_ptr<Canvas> _canvas;

    // Declared last so every member above is constructed before CreateWindowExW starts sending messages.
    UniqueOwnedWindow _window;
};
