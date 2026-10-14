#pragma once

#include "config/Config.hpp"
#include "platform/Text.hpp"

#include <lua.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

// Reads one Lua table with raw access only: a metamethod raising an error outside lua_pcall hits Lua's panic handler,
// which aborts the process. Every read throws a ConfigError naming the key's path, as modules.clock.format.
class TableReader {
  public:
    // path is the table's own key path, empty for the table the config returns.
    TableReader(lua_State* lua, std::int32_t index, std::string source, std::string path);

    bool        Has(std::string_view key) const;
    TableReader Table(std::string_view key) const;
    std::string String(std::string_view key) const;
    float       Number(std::string_view key) const;

    // A sequence of strings such as { "clock", "cpu" }.
    std::vector<std::string> StringList(std::string_view key) const;

    // A sequence of tables such as { { min_length = 1921 } }, each read as layouts[1] and so on.
    std::vector<TableReader> TableList(std::string_view key) const;

    // Every key of the table, which must all be strings.
    std::vector<std::string> Keys() const;

    void                     RejectUnknownKeys(const std::vector<std::string_view>& known) const;
    std::string              KeyPath(std::string_view key) const;
    ConfigError              Error(std::string_view message) const;

  private:
    std::string TableName() const;

    // ReadFile checked the source, but a Lua escape ("\xff", "\u{D800}") still builds any bytes.
    std::string StringAt(std::int32_t index, std::string_view name) const;

    size_t      CountKeys(std::int32_t tableIndex) const;
    void        RequirePlainList(std::int32_t listIndex, std::string_view key, lua_Integer length) const;

    // Every table read stays on the stack while its TableReader lives, one slot per module, so the stack can grow past
    // the LUA_MINSTACK slots Lua guarantees. Pushing past the allocated stack is unchecked in a release build.
    void Reserve(std::int32_t slots) const;

    // Leaves room for StringList's element on top of the field.
    void         PushField(std::string_view key, std::int32_t expectedType, std::string_view expectedName) const;

    lua_State*   _lua;
    std::int32_t _index;
    std::string  _source;
    std::string  _path;
};

float                PositiveNumber(const TableReader& table, std::string_view key);
std::int64_t         PositiveWholeNumber(const TableReader& table, std::string_view key);
std::chrono::seconds Seconds(const TableReader& table, std::string_view key); // a whole number of seconds up to a day

// Rejects a list that names one entry twice.
void RejectRepeats(const TableReader& table, std::string_view key, const std::vector<std::string>& entries);

// One word a config key accepts and the value it stands for.
template <typename Value>
struct Keyword {
    std::string_view name;
    Value            value;
};

template <typename Value, size_t Count>
Value ParseKeyword(const TableReader& table, std::string_view key, const std::array<Keyword<Value>, Count>& keywords) {
    const std::string value = table.String(key);
    const auto        entry = std::ranges::find(keywords, value, &Keyword<Value>::name);
    if (entry == keywords.end()) {
        throw table.Error(std::format("{} must be one of {}, got '{}'", table.KeyPath(key), JoinNames(keywords | std::views::transform(&Keyword<Value>::name)), value));
    }
    return entry->value;
}
