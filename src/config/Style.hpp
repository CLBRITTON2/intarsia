#pragma once

#include "config/Config.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// One entry of style.conf: a property (key: value), or a block (key { ... }) holding the entries between its braces.
struct StyleNode {
    std::string            key;
    std::string            value; // $names already replaced, empty for a block
    std::uint32_t          line;
    std::vector<StyleNode> children;
};

struct StyleSheet {
    std::string            source; // the file, for error messages
    std::vector<StyleNode> blocks; // every top-level block but vars, in file order
};

// Blocks (key { ... }) of properties (key: value, ended by a line break, ; or }), with // comments and $names from the
// vars block. Every entry is checked against the block it is in, and anything outside the format is a ConfigError
// naming the line, never an entry that silently does nothing.
StyleSheet ParseStyleSheet(std::string_view text, std::string source);

// The bar block, which must set font, size and color, and the section, tooltip and menu blocks.
StyleConfig ResolveBarStyle(const StyleSheet& sheet);

// The block named for the module over the module block, each one's states over both one's base. Color, font and size
// that neither sets inherit from the bar, and the icon's and items' color from the module. classes are those a custom
// module declares, each a block of the module's own that colors it.
ModuleStyle ResolveModuleStyle(const StyleSheet& sheet, const StyleConfig& bar, ModuleKind kind, std::string_view name, const std::vector<std::string>& classes);

// Every top-level block that is not bar, section, module, tooltip or menu names a module the config defines, so a
// misspelled name is an error rather than a block that never applies.
void RequireKnownModules(const StyleSheet& sheet, const std::vector<std::string>& names);

// Whether name is a top-level block of the style's own (bar, section ...), which a module's name would collide with.
bool IsReservedModuleName(std::string_view name);

// Letters, digits, '_' and '-': what module names, class names and style keys are written in.
bool IsNameCharacter(char character);

// Whether name is a block every module may hold (icon, item, hover ...), which a custom module's class would collide
// with.
bool IsStyledClass(std::string_view name);

// Every class block under module must be one some custom module declares, so a misspelled class is an error rather
// than a block that never applies. declared holds every custom module's classes. A module's own block checks its
// classes as it resolves.
void RequireDeclaredClasses(const StyleSheet& sheet, const std::vector<std::string>& declared);
