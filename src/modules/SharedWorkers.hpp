#pragma once

#include "config/Config.hpp"
#include "platform/ChangeSignal.hpp"

#include <format>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

// One Worker per module name of kind Kind, shared by every bar that shows it, so each does its work once however many
// monitors there are. Worker is made from the module's name, its Settings and the change signal.
template <typename Worker, typename Settings, ModuleKind Kind>
class SharedWorkers {
  public:
    SharedWorkers(const Config& config, ChangeSignal& changes) {
        ForEachModuleInConfig(config, [&](const ModuleInstance& instance) {
            if (instance.kind == Kind && !_workers.contains(instance.name)) {
                _workers.emplace(instance.name, std::make_unique<Worker>(instance.name, std::get<Settings>(instance.settings), changes));
            }
        });
    }

    // Throws std::logic_error for a name the config defines no module of Kind under.
    const Worker& For(const std::string& name) const {
        const auto worker = _workers.find(name);
        if (worker == _workers.end()) {
            throw std::logic_error(std::format("no shared worker for module {}, the config defines no module of its kind under that name", name));
        }
        return *worker->second;
    }

  private:
    std::map<std::string, std::unique_ptr<Worker>> _workers;
};
