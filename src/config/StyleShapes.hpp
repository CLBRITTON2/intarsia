#pragma once

#include "config/Config.hpp"
#include "config/Style.hpp"

#include <string>
#include <string_view>
#include <vector>

// What each block of style.conf may hold. Every block, property and value is checked here, so a key the bar does not
// read is an error rather than one that silently does nothing.

// Checks one top-level block against its shape: bar, section, module, tooltip and menu their own, any other a
// module's.
void CheckTopLevelBlock(const StyleNode& block, std::string_view source);

// What a module's own block holds that only some module types draw, and that each other block it holds is one of
// classes.
void RequireDrawnBlocks(const StyleNode& own, ModuleKind kind, const std::vector<std::string>& classes, std::string_view source);
