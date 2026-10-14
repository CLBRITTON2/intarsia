#pragma once

#include "config/Config.hpp"
#include "config/Style.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

// The ConfigError for line of source, as style.conf:12: message.
ConfigError StyleError(std::string_view source, std::uint32_t line, std::string_view message);

// A block (key { ... }) has children and no value, a property (key: value) a value.
bool IsBlock(const StyleNode& node);

// block's entry named key, or null when block is null or has none.
const StyleNode* Child(const StyleNode* block, std::string_view key);

// The sheet's top-level block named key, or null.
const StyleNode* TopLevel(const StyleSheet& sheet, std::string_view key);

// style.conf's entries in file order, each $name replaced with its vars value and the vars block dropped. Checks the
// format only, not what each block may hold.
std::vector<StyleNode> ParseStyleTree(std::string_view text, std::string_view source);
