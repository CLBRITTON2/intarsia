#pragma once

#include "bar/Tooltip.hpp"
#include "config/Config.hpp"
#include "platform/Launcher.hpp"
#include "platform/Process.hpp"
#include "platform/Threads.hpp"
#include "platform/Win32Window.hpp"
#include "render/Box.hpp"
#include "render/Graphics.hpp"

#include <d2d1_1.h>
#include <dwrite.h>
#include <windows.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// One entry a menu command printed.
struct MenuEntry {
    std::string                id; // the argument on_select gets when the entry is picked
    std::string                text;
    bool                       active;  // drawn in the menu item active block colors, such as the profile already connected
    std::optional<std::string> tooltip; // shown beside the menu while the cursor rests on the entry

    bool                       operator==(const MenuEntry&) const = default;
};

// A blank line between entries, as tall as one, that cannot be picked.
struct MenuSeparator {
    bool operator==(const MenuSeparator&) const = default;
};

using MenuItem = std::variant<MenuEntry, MenuSeparator>;

// A menu command that gave no entries to show: it failed, or printed output ParseMenuOutput refuses.
struct MenuFailure {
    std::string reason; // short enough to show in the menu
    std::string detail; // the full failure, for the log

    bool        operator==(const MenuFailure&) const = default;
};

using MenuListing = std::variant<std::vector<MenuItem>, MenuFailure>;

// The most items, separators included, one menu shows. Output listing more fails instead of growing a menu past any
// screen.
inline constexpr size_t kMaxMenuEntries = 50;

// What a menu command printed: one JSON object, {"items":[{"id":"work","text":"Work","active":true},
// {"separator":true}, ...]}, active and tooltip optional. Output is untrusted: id, text and tooltip must be drawable
// (RequireDrawable), a tooltip may also hold line breaks, and an id is never empty and never starts with '-' or '/', so
// the program on_select starts cannot read it as an option. Throws OutputError otherwise.
std::vector<MenuItem> ParseMenuOutput(std::string_view output);

// Whether items holds an entry to pick, rather than nothing or only separators.
bool HasEntries(const std::vector<MenuItem>& items);

// Runs one menu command at a time on its own thread, and posts message to target once its listing is ready.
class MenuLister {
  public:
    MenuLister(HWND target, std::uint32_t message);
    MenuLister(const MenuLister&)            = delete;
    MenuLister& operator=(const MenuLister&) = delete;

    // Kills the listing in progress, if any, and starts command.
    void List(const Command& command, std::chrono::seconds timeout);

    // The finished listing, or nullopt when a later List call replaced it before it was taken.
    std::optional<MenuListing> Take();

  private:
    void                       Run(HANDLE stopEvent);

    HWND                       _target;
    std::uint32_t              _message;
    std::mutex                 _mutex;
    std::optional<MenuListing> _ready;
    // Captured by value, the command would make the thread body's move constructor throwing.
    Command              _command;
    std::chrono::seconds _timeout;
    // Declared last so every member above exists while a listing runs. Destruction kills a listing in progress.
    std::unique_ptr<StoppableThread> _thread;
};

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

// A menu's rows placed down its box and stretched to the widest, and the window that holds them.
struct MenuLayout {
    std::vector<MenuRow> rows;
    SIZE                 size;   // the window, in pixels, never larger than its monitor
    MenuScroll           scroll; // at the top
    D2D1_RECT_F          gutter; // beside the viewport, where the track takes clicks
    D2D1_RECT_F          track;
};

// The widest row's text may lay out in a menu on monitorRect before its ellipsis, capped by style's max-width.
float MenuTextWidth(const MenuStyle& style, const ItemStyle& row, const RECT& monitorRect, std::uint32_t dpi);

// rows, each sized to its text with its box at the origin, stacked inside style's box. Past max-height or the monitor's
// height the window stops growing, the rows scroll and the scroll bar takes its gutter.
MenuLayout LayOutMenu(std::vector<MenuRow> rows, const MenuStyle& style, const RECT& monitorRect, std::uint32_t dpi);

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
TooltipAnchor RowTooltipAnchor(const RECT& row, const RECT& monitorRect, std::uint32_t dpi);

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
    MenuLayout                                  _layout{};  // as Show laid it out, its scroll moved since
    std::optional<size_t>                       _highlight; // the entry under the cursor or picked with the arrow keys
    Command                                     _onSelect;
    std::optional<float>                        _thumbGrabDips; // while dragging the thumb, how far below its top the cursor holds it
    // Only while shown: Show creates it at the rows' size and Hide releases it.
    std::unique_ptr<Canvas> _canvas;

    // Declared last so every member above is constructed before CreateWindowExW starts sending messages.
    UniqueOwnedWindow _window;
};
