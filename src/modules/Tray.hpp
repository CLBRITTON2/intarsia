#pragma once

#include "config/Config.hpp"
#include "modules/Fold.hpp"
#include "modules/IModule.hpp"
#include "modules/IconText.hpp"
#include "modules/ItemRow.hpp"
#include "tray/ITray.hpp"

#include <wrl/client.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The notification area: one item per icon apps show, in the order they added them, then a toggle item that folds
// them away or shows them again. Starts folded, showing the toggle alone. Icons are in the module's .item style at its
// .icon font-size, and the toggle in its .item.toggle style with its text as an icon. The icons unfold over the module's width transition,
// revealed from the start as the box grows along the bar, and the toggle turns to the .expanded icon's rotation over the icon's.
// Clicking an icon reaches its app as a click on the taskbar's icon would, and resting on one shows its tooltip.
class Tray final : public IModule {
  public:
    Tray(TrayConfig config, ITray& tray, ModuleStyle style);
    ~Tray() override;
    Tray(const Tray&)                  = delete;
    Tray&       operator=(const Tray&) = delete;

    bool        Layout(const RenderContext& render) override;
    D2D1_SIZE_F Size() const override;
    // Tells the tray where the icons the box's edge fully uncovers are on screen.
    void Place(const ModulePlacement& placement) override;
    void Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    // 0 while unfolding, folding or turning, so the bar draws every display frame until the transitions end.
    std::optional<std::uint32_t> MillisecondsUntilChange() const override;
    void                         Click(D2D1_POINT_2F point) override;
    void                         RightClick(D2D1_POINT_2F point) override;
    void                         MiddleClick(D2D1_POINT_2F point) override;
    // Over an icon, also tells its app the cursor moved. Leaving the icon whose popup is open closes it.
    bool Hover(std::optional<D2D1_POINT_2F> point) override;
    // None: clicks go to the icon under the cursor, or fold and unfold the tray.
    const ModuleActions& Actions() const override;
    // The tooltip of the icon the cursor rests on. An icon whose app draws its own popup has none: its popup opens.
    std::optional<std::wstring> TooltipText() override;
    // The icon under the cursor.
    std::optional<D2D1_RECT_F> TooltipItem() const override;

  private:
    // One shown icon. The bitmap is made from its pixels on this bar's device.
    struct Slot {
        TrayIcon                            icon;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    };

    // The icons' length along the bar unfolded, how much of it shows, and how far back the toggle sits of where the
    // whole row puts it.
    float IconsLength() const;
    float RevealedLength() const;
    float ToggleShift() const;
    // A point on the module as a point on the whole row, so the row's items can be hit where they are drawn.
    D2D1_POINT_2F         RowPoint(D2D1_POINT_2F point) const;
    std::optional<size_t> ItemUnder(D2D1_POINT_2F point) const;
    // The shown icon under point, or null over the toggle or between items.
    const Slot* SlotAt(D2D1_POINT_2F point) const;
    bool        ToggleAt(D2D1_POINT_2F point) const;

    TrayConfig  _config;
    ITray&      _tray;
    ModuleStyle _style;
    Fold        _fold;
    // What the last layout read of them, so drawing and hit tests agree with one frame.
    float             _revealShown = 0.0f;
    float             _angleShown  = 0.0f;
    std::vector<Slot> _slots; // as last laid out with any icon in sight
    IconText          _toggle;
    // Every icon then the toggle, as if fully unfolded.
    ItemRow                 _row = EmptyItemRow();
    HoveredItem             _hovered;
    std::optional<TrayIcon> _popup; // the icon whose app's popup is open
    bool                    _laidOut = false;
};

// The icons a tray shows: those with pixels that their app has not hidden.
std::vector<TrayIcon> ShownIcons(const std::vector<TrayIcon>& icons);
