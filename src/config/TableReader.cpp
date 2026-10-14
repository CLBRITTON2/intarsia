#include "config/TableReader.hpp"

#include "platform/Text.hpp"

#include <lauxlib.h>
#include <lua.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    // Whole numbers are read through a float, which holds every whole number up to 2^24 exactly.
    constexpr float kMaxWholeNumber = 16777216.0f;

    // Far inside the 32-bit millisecond waits these reach, which wrap past about 49.7 days.
    constexpr std::int64_t kLongestWaitSeconds = 86400;

} // namespace

TableReader::TableReader(lua_State* lua, std::int32_t index, std::string source, std::string path) : _lua(lua), _index(index), _source(std::move(source)), _path(std::move(path)) {}

bool TableReader::Has(std::string_view key) const {
    Reserve(1);
    lua_pushlstring(_lua, key.data(), key.size());
    const std::int32_t type = lua_rawget(_lua, _index);
    lua_pop(_lua, 1);
    return type != LUA_TNIL;
}

TableReader TableReader::Table(std::string_view key) const {
    PushField(key, LUA_TTABLE, "a table");
    return TableReader(_lua, lua_gettop(_lua), _source, KeyPath(key));
}

std::string TableReader::String(std::string_view key) const {
    PushField(key, LUA_TSTRING, "a string");
    std::string value = StringAt(-1, KeyPath(key));
    lua_pop(_lua, 1);
    return value;
}

float TableReader::Number(std::string_view key) const {
    PushField(key, LUA_TNUMBER, "a number");
    const auto value = static_cast<float>(lua_tonumber(_lua, -1));
    lua_pop(_lua, 1);
    return value;
}

std::vector<std::string> TableReader::StringList(std::string_view key) const {
    PushField(key, LUA_TTABLE, "a list of strings");
    const std::int32_t listIndex = lua_gettop(_lua);
    const auto         length    = static_cast<lua_Integer>(lua_rawlen(_lua, listIndex));
    RequirePlainList(listIndex, key, length);
    std::vector<std::string> values;
    values.reserve(static_cast<size_t>(length));
    for (lua_Integer position = 1; position <= length; ++position) {
        if (lua_rawgeti(_lua, listIndex, position) != LUA_TSTRING) {
            throw Error(std::format("{}[{}] must be a string, got {}", KeyPath(key), position, luaL_typename(_lua, -1)));
        }
        values.push_back(StringAt(-1, std::format("{}[{}]", KeyPath(key), position)));
        lua_pop(_lua, 1);
    }
    lua_pop(_lua, 1);
    return values;
}

std::vector<TableReader> TableReader::TableList(std::string_view key) const {
    PushField(key, LUA_TTABLE, "a list of tables");
    const std::int32_t listIndex = lua_gettop(_lua);
    const auto         length    = static_cast<lua_Integer>(lua_rawlen(_lua, listIndex));
    RequirePlainList(listIndex, key, length);
    std::vector<TableReader> entries;
    entries.reserve(static_cast<size_t>(length));
    for (lua_Integer position = 1; position <= length; ++position) {
        Reserve(1);
        if (lua_rawgeti(_lua, listIndex, position) != LUA_TTABLE) {
            throw Error(std::format("{}[{}] must be a table, got {}", KeyPath(key), position, luaL_typename(_lua, -1)));
        }
        entries.emplace_back(_lua, lua_gettop(_lua), _source, std::format("{}[{}]", KeyPath(key), position));
    }
    return entries;
}

std::vector<std::string> TableReader::Keys() const {
    std::vector<std::string> keys;
    Reserve(2);
    lua_pushnil(_lua);
    while (lua_next(_lua, _index) != 0) {
        if (lua_type(_lua, -2) != LUA_TSTRING) {
            throw Error(std::format("{} has a key that is not a string", TableName()));
        }
        keys.push_back(StringAt(-2, std::format("a key of {}", TableName())));
        lua_pop(_lua, 1);
    }
    return keys;
}

void TableReader::RejectUnknownKeys(const std::vector<std::string_view>& known) const {
    for (const std::string& key : Keys()) {
        if (std::ranges::find(known, key) == known.end()) {
            throw Error(std::format("{} is not a known key", KeyPath(key)));
        }
    }
}

std::string TableReader::KeyPath(std::string_view key) const {
    if (_path.empty()) {
        return std::string(key);
    }
    return std::format("{}.{}", _path, key);
}

ConfigError TableReader::Error(std::string_view message) const {
    return ConfigError(std::format("{}: {}", _source, message));
}

std::string TableReader::TableName() const {
    if (_path.empty()) {
        return "the returned table";
    }
    return _path;
}

std::string TableReader::StringAt(std::int32_t index, std::string_view name) const {
    size_t                 length = 0;
    const char*            data   = lua_tolstring(_lua, index, &length);
    const std::string_view value(data, length);
    if (!IsValidUtf8(value)) {
        throw Error(std::format("{} is not valid UTF-8", name));
    }
    return std::string(value);
}

size_t TableReader::CountKeys(std::int32_t tableIndex) const {
    size_t count = 0;
    Reserve(2);
    lua_pushnil(_lua);
    while (lua_next(_lua, tableIndex) != 0) {
        ++count;
        lua_pop(_lua, 1);
    }
    return count;
}

void TableReader::RequirePlainList(std::int32_t listIndex, std::string_view key, lua_Integer length) const {
    if (CountKeys(listIndex) != static_cast<size_t>(length)) {
        throw Error(std::format("{} must be a plain list, it has keys other than 1 to {}", KeyPath(key), length));
    }
}

void TableReader::Reserve(std::int32_t slots) const {
    if (lua_checkstack(_lua, slots) == 0) {
        throw Error(std::format("Lua cannot grow its stack by {} slots to read {}", slots, TableName()));
    }
}

void TableReader::PushField(std::string_view key, std::int32_t expectedType, std::string_view expectedName) const {
    Reserve(2);
    lua_pushlstring(_lua, key.data(), key.size());
    const std::int32_t type = lua_rawget(_lua, _index);
    if (type == LUA_TNIL) {
        throw Error(std::format("{} is missing", KeyPath(key)));
    }
    if (type != expectedType) {
        throw Error(std::format("{} must be {}, got {}", KeyPath(key), expectedName, lua_typename(_lua, type)));
    }
}

float PositiveNumber(const TableReader& table, std::string_view key) {
    const float value = table.Number(key);
    if (!(value > 0.0f) || !std::isfinite(value)) {
        throw table.Error(std::format("{} must be greater than 0, got {}", table.KeyPath(key), value));
    }
    return value;
}

std::int64_t PositiveWholeNumber(const TableReader& table, std::string_view key) {
    const float value = PositiveNumber(table, key);
    if (value != std::floor(value) || value > kMaxWholeNumber) {
        throw table.Error(std::format("{} must be a whole number up to {}, got {}", table.KeyPath(key), kMaxWholeNumber, value));
    }
    return static_cast<std::int64_t>(value);
}

std::chrono::seconds Seconds(const TableReader& table, std::string_view key) {
    const std::int64_t value = PositiveWholeNumber(table, key);
    if (value > kLongestWaitSeconds) {
        throw table.Error(std::format("{} must be at most {} seconds, got {}", table.KeyPath(key), kLongestWaitSeconds, value));
    }
    return std::chrono::seconds(value);
}

// A set rather than a scan of the entries before each one, since a config's loop can build a list of any length.
void RejectRepeats(const TableReader& table, std::string_view key, const std::vector<std::string>& entries) {
    std::set<std::string_view> seen;
    for (const std::string& entry : entries) {
        if (!seen.insert(entry).second) {
            throw table.Error(std::format("{} lists '{}' twice", table.KeyPath(key), entry));
        }
    }
}
