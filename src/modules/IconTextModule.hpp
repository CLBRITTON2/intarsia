#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/TextLine.hpp"

#include <utility>

// A module that shows one ModuleText, laid out again only when it changes, and runs the actions its config binds.
// Config is the module's config struct, which carries those actions. A subclass supplies only the content.
template <typename Config>
class IconTextModule : public IModule {
  public:
    bool Layout(const RenderContext& render) final {
        return _line.Update(render, Content());
    }

    D2D1_SIZE_F Size() const final {
        return _line.Size();
    }

    void Draw(const RenderContext& render, D2D1_POINT_2F origin) const final {
        _line.Draw(render, origin);
    }

    const ModuleActions& Actions() const final {
        return _config.actions;
    }

  protected:
    explicit IconTextModule(Config config) : _config(std::move(config)) {}

    const Config& Settings() const {
        return _config;
    }

    // What the module shows now.
    virtual ModuleText Content() = 0;

  private:
    Config   _config;
    TextLine _line;
};
