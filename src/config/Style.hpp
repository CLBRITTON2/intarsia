#pragma once

#include "config/Config.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// What a selector styles, from its last part.
enum class StyleTarget : std::uint8_t {
    Bar,     // *: the bar-wide font and color
    Section, // section
    Tooltip, // tooltip
    Module,  // a module's box: by type (clock), id (its name in the config, #time) or .module, with a state or :hover
    Icon,    // .icon, alone or inside a module selector
    Item     // .item with an optional .focused, .occupied, .empty or .toggle and :hover, alone or inside a module selector
};

// One part of a selector, as workspaces, #cpu.alert or .item.focused.
struct Compound {
    std::string              type;
    std::string              id;
    std::vector<std::string> classes;
    bool                     hover;
};

struct Selector {
    std::vector<Compound>        compounds; // one, or a module then its .icon or .item
    StyleTarget                  target;
    std::array<std::uint32_t, 3> specificity; // ids, then classes and :hover, then types, as CSS ranks them
};

struct Declaration {
    std::string   property;
    std::string   value; // var() already replaced
    std::uint32_t line;
};

struct StyleRule {
    Selector                 selector;
    std::vector<Declaration> declarations;
    std::uint32_t            line;
};

struct StyleSheet {
    std::string            source; // the file, for error messages
    std::vector<StyleRule> rules;  // one per selector of a selector list, in file order
};

// A subset of CSS: rules of simple selectors, :root custom properties read through var(), and /* comments */.
// Everything outside the subset is a ConfigError naming the line, never a rule that silently does nothing.
StyleSheet ParseStyleSheet(std::string_view text, std::string source);

// The * rules, which must set font-family, font-size and color, and the section and tooltip rules.
StyleConfig ResolveBarStyle(const StyleSheet& sheet);

// Every rule matching one module, by specificity and then file order, as CSS cascades. Color and font inherit from the
// bar, and the icon's and items' colors from the module. classes are those a custom module declares, colored in turn.
ModuleStyle ResolveModuleStyle(const StyleSheet& sheet, const StyleConfig& bar, ModuleKind kind, std::string_view name, const std::vector<std::string>& classes);

// Every #id names a module the config defines, so a misspelled name is an error rather than a rule that never applies.
void RequireKnownIds(const StyleSheet& sheet, const std::vector<std::string>& names);

// Letters, digits, '_' and '-': a CSS identifier's characters, which module and class names are written in.
bool IsIdentCharacter(char character);

// Whether name is one of the classes intarsia styles itself (.module, .alert, .icon ...), which a custom module cannot
// declare.
bool IsStyledClass(std::string_view name);

// The classes one custom module declares.
struct DeclaredClasses {
    std::string              module;
    std::vector<std::string> classes;
};

// Every class a rule names that intarsia does not style itself must be one a custom module declares and the rule
// matches, so a misspelled class is an error rather than a rule that never applies.
void RequireDeclaredClasses(const StyleSheet& sheet, const std::vector<DeclaredClasses>& declared);
