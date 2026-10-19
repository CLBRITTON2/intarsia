#pragma once

#include "config/Config.hpp"
#include "modules/AnimatedValue.hpp"
#include "modules/IModule.hpp"
#include "modules/IconText.hpp"

#include <cstdint>
#include <optional>
#include <string>

// A group's toggle: its icon, which folds the group's members away or unfolds them after it. Starts folded. The bar
// owns and places the members, widening the group's box by Revealed() of theirs, and the toggle turns to the expanded
// icon's rotation over the icon's transition.
class Group final : public IModule {
  public:
    Group(const GroupConfig& config, const ModuleStyle& style);

    bool Layout(const RenderContext& render) override;
    // The toggle alone: the bar adds the members' share.
    D2D1_SIZE_F Size() const override;
    void Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    // 0 while unfolding, folding or turning, so the bar draws every display frame until the transitions end.
    std::optional<std::uint32_t> MillisecondsUntilChange() const override;
    // Any click that reaches the group folds or unfolds it: its members take clicks on themselves.
    void Click(D2D1_POINT_2F point) override;
    // None: a group's keys are its type, icon and members.
    const ModuleActions& Actions() const override;

    // How much of the members' length shows as of the last layout, 0 folded to 1 unfolded.
    float Revealed() const;

  private:
    std::wstring _icon;
    Fold _fold;
    FoldPosition _shown{.revealed = 0.0f, .angleDegrees = 0.0f}; // as of the last layout, so drawing and placing agree
    IconText _toggle;
    bool _laidOut = false;
};
