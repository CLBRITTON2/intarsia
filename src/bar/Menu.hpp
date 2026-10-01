#pragma once

#include "bar/MenuLister.hpp"
#include "bar/Tooltip.hpp"
#include "config/Config.hpp"
#include "platform/Process.hpp"
#include "platform/WindowProc.hpp"
#include "render/BrushCache.hpp"
#include "render/Graphics.hpp"
#include "render/Surface.hpp"

#include <dwrite.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

struct AppState;

// One row of a shown menu, in DIPs from the menu's top left.
struct MenuRow {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> text; // null for a separator
    D2D1_RECT_F                               box;
    D2D1_POINT_2F                             textOrigin;
    const ItemStyle*                          style;
    std::optional<size_t>                     entry; // the entry picking the row selects, null for a failure's row or a separator
};

enum class MenuStep : std::uint8_t {
    Up,
    Down
};

// How far a menu's rows have scrolled through the part of its box that shows them, in DIPs. Rows lay out as if
// unscrolled, from the viewport's top.
struct MenuScroll {
    D2D1_RECT_F viewport;    // the box inside its border and padding
    float       contentDips; // the rows' height, margins included
    float       offsetDips;  // how far the rows have moved up, 0 at the top
};

// scroll at offsetDips, kept between the top and the offset that brings the last row to the viewport's bottom.
MenuScroll ScrolledTo(const MenuScroll& scroll, float offsetDips);

// scroll moved the least that shows all of row, which lays out unscrolled.
MenuScroll ScrolledToShow(const MenuScroll& scroll, const D2D1_RECT_F& row);

// The scroll bar's thumb along track, sized by how much of the rows the viewport shows, or nullopt while they all fit.
std::optional<D2D1_RECT_F> ThumbRect(const MenuScroll& scroll, const D2D1_RECT_F& track);

// scroll at the offset that puts the thumb's top at thumbTopDips, as dragging it does.
MenuScroll ScrolledToThumb(const MenuScroll& scroll, const D2D1_RECT_F& track, float thumbTopDips);

// The entry whose row shows under point, or nullopt over the menu's box, a row scrolled out of view or a failure's row.
std::optional<size_t> EntryAt(const std::vector<MenuRow>& rows, const MenuScroll& scroll, D2D1_POINT_2F point);

// The entry an arrow key moves the highlight to among count entries, wrapping at either end. With none highlighted,
// Down goes to the first and Up to the last.
std::optional<size_t> StepHighlight(std::optional<size_t> current, size_t count, MenuStep step);

// Where an entry's tooltip goes: over the menu, centered on the row and against its side with more of the monitor
// below or above it.
TooltipAnchor RowTooltipAnchor(const RECT& row, const RECT& monitor, std::uint32_t dpi);

// A popup listing a menu command's entries beside the module that opened it. It takes focus while shown, so the arrow
// keys, Enter and Esc work, and hides once another window takes it. Picking an entry runs on_select with the entry's
// id as its last argument. Each entry, and each separator, takes one line, cut with an ellipsis at max-width, and entries past max-height
// scroll by wheel, arrow keys and the scroll bar's thumb. An entry's tooltip shows in the bar's tooltip once the cursor
// rests on it.
class Menu {
  public:
    // onDeviceLost receives a lost device met while drawing from the menu's own messages.
    Menu(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, IDWriteTextFormat* textFormat, Launcher& launcher, AppState& state, Tooltip& tooltip,
         std::function<void(const DeviceLostError&)> onDeviceLost);
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
    void RestartHover();
    void ShowTooltip();
    // Mouse positions are client pixels, and rows lay out in DIPs.
    D2D1_POINT_2F                               ClientDips(POINT client) const;
    void                                        ReleaseSurface();

    const GraphicsDevice&                       _graphics;
    const Config&                               _config;
    IDWriteTextFormat*                          _textFormat;
    Launcher&                                   _launcher;
    AppState&                                   _state;
    Tooltip&                                    _tooltip;
    std::function<void(const DeviceLostError&)> _onDeviceLost;
    bool                                        _shown = false;
    std::uint32_t                               _dpi   = USER_DEFAULT_SCREEN_DPI;
    RECT                                        _rect{}; // the shown menu, in screen pixels
    RECT                                        _monitor{};
    std::vector<MenuEntry>                      _entries;
    std::vector<MenuRow>                        _rows;
    std::optional<size_t>                       _highlight; // the entry under the cursor or picked with the arrow keys
    Command                                     _onSelect;
    MenuScroll                                  _scroll{};
    D2D1_RECT_F                                 _gutter{}; // beside the viewport, where the track takes clicks
    D2D1_RECT_F                                 _track{};
    std::optional<float>                        _thumbGrabDips; // while dragging the thumb, how far below its top the cursor holds it
    // Only while shown: Show creates it at the rows' size and Hide releases it.
    std::unique_ptr<Surface>    _surface;
    std::unique_ptr<BrushCache> _brushes;

    // Declared last so every member above is constructed before CreateWindowExW starts sending messages.
    UniqueOwnedWindow _window;
};
