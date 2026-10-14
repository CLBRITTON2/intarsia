#include "config/LuaSandbox.hpp"

#include "config/Config.hpp"
#include "platform/Text.hpp"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <string_view>

namespace {

    // Far more than any real config needs (a few thousand instructions), and still under a second to reach.
    constexpr std::int32_t kMaxConfigInstructions = 50'000'000;
    // Far more than any real config allocates (a few hundred KiB with the libraries), and a string built in one C call
    // runs no instructions for the count hook to see.
    constexpr size_t kMaxConfigMemoryBytes = size_t{16} << 20;

    // oldSize is the block's size only for a live block, else a tag of what is being allocated.
    size_t HeldBytes(const void* block, size_t oldSize) {
        if (block == nullptr) {
            return 0;
        }
        return oldSize;
    }

    // Lua's own allocator with a ceiling: a refused request raises a memory error inside the lua_pcall.
    void* LimitedAlloc(void* userData, void* block, size_t oldSize, size_t newSize) {
        auto*        limits = static_cast<LuaLimits*>(userData);
        const size_t held   = HeldBytes(block, oldSize);
        if (newSize > held && newSize - held > limits->limitBytes - limits->usedBytes) {
            return nullptr;
        }
        void* resized = limits->allocate(limits->allocateData, block, oldSize, newSize);
        if (newSize != 0 && resized == nullptr) {
            return nullptr;
        }
        limits->usedBytes = limits->usedBytes - held + newSize;
        return resized;
    }

    // Puts the limited allocator over the state's own, counting from what the state holds already so that freeing it
    // later balances.
    void LimitAllocations(lua_State* lua, LuaLimits& limits) {
        limits.allocate  = lua_getallocf(lua, &limits.allocateData);
        limits.usedBytes = (static_cast<size_t>(lua_gc(lua, LUA_GCCOUNT)) << 10) + static_cast<size_t>(lua_gc(lua, LUA_GCCOUNTB));
        lua_setallocf(lua, LimitedAlloc, &limits);
    }

    LuaLimits& LimitsOf(lua_State* lua) {
        void* userData = nullptr;
        lua_getallocf(lua, &userData);
        return *static_cast<LuaLimits*>(userData);
    }

    // Called once the count is reached. The error raised is catchable, so from here on every instruction raises again
    // until it reaches the sandbox's own call, though no protected call is left in the sandbox to catch it.
    void AbortRunawayConfig(lua_State* lua, lua_Debug*) {
        LimitsOf(lua).runaway = true;
        lua_sethook(lua, AbortRunawayConfig, LUA_MASKCOUNT, 1);
        luaL_error(lua, "the config ran %d instructions without finishing, is there an endless loop?", kMaxConfigInstructions);
    }

    void OpenLibrary(lua_State* lua, const char* name, lua_CFunction open) {
        luaL_requiref(lua, name, open, 1);
        lua_pop(lua, 1);
    }

    // A config's error() can hold any bytes, and the message reaches a console that refuses ones that are not UTF-8.
    std::string ErrorText(const char* message) {
        if (message == nullptr) {
            return "non-string Lua error";
        }
        return ToUtf8(DrawableText(std::string_view(message)));
    }

    std::string PopErrorMessage(lua_State* lua) {
        std::string text = ErrorText(lua_tostring(lua, -1));
        lua_pop(lua, 1);
        return text;
    }

    // The ConfigError for a load or call that did not return LUA_OK.
    ConfigError LuaError(lua_State* lua, std::int32_t status, std::string_view source) {
        if (LimitsOf(lua).runaway) {
            return ConfigError(std::format("{}: the config ran {} instructions without finishing, is there an endless loop?", source, kMaxConfigInstructions));
        }
        if (status == LUA_ERRMEM) {
            return ConfigError(std::format("{}: the config used more than {} MiB of memory", source, kMaxConfigMemoryBytes >> 20));
        }
        return ConfigError(PopErrorMessage(lua));
    }

} // namespace

LuaSandbox::LuaSandbox(std::string_view chunk, const std::string& source) :
    _limits{
        .allocate     = nullptr,
        .allocateData = nullptr,
        .usedBytes    = 0,
        .limitBytes   = kMaxConfigMemoryBytes,
        .runaway      = false,
    },
    _lua(luaL_newstate()) {
    if (_lua == nullptr) {
        throw ConfigError("luaL_newstate failed to allocate a Lua state");
    }
    lua_State* lua = _lua.get();
    LimitAllocations(lua, _limits);
    // Only libraries that cannot reach the filesystem or other processes: base minus its loaders. load goes too, since it
    // accepts precompiled chunks and Lua does not verify bytecode, pcall since a config is data with nothing to recover
    // from, and setmetatable since Lua runs a __gc metamethod with hooks off, where an endless loop could not be stopped.
    OpenLibrary(lua, LUA_GNAME, luaopen_base);
    OpenLibrary(lua, LUA_STRLIBNAME, luaopen_string);
    OpenLibrary(lua, LUA_TABLIBNAME, luaopen_table);
    OpenLibrary(lua, LUA_MATHLIBNAME, luaopen_math);
    for (const char* removed : {"dofile", "loadfile", "load", "pcall", "xpcall", "setmetatable"}) {
        lua_pushnil(lua);
        lua_setglobal(lua, removed);
    }
    // Pattern matching backtracks in C, out of the count hook's reach, and a short pattern can take exponential time.
    // The string metatable's __index is this same table, so the method form goes too.
    lua_getglobal(lua, LUA_STRLIBNAME);
    for (const char* removed : {"find", "match", "gmatch", "gsub"}) {
        lua_pushnil(lua);
        lua_setfield(lua, -2, removed);
    }
    lua_pop(lua, 1);
    // Reload runs on the UI thread, so a config that never finishes would freeze the bar: stop it instead.
    lua_sethook(lua, AbortRunawayConfig, LUA_MASKCOUNT, kMaxConfigInstructions);

    const std::string  chunkName = "@" + source;
    const std::int32_t loaded    = luaL_loadbufferx(lua, chunk.data(), chunk.size(), chunkName.c_str(), "t");
    if (loaded != LUA_OK) {
        throw LuaError(lua, loaded, source);
    }
    const std::int32_t ran = lua_pcall(lua, 0, 1, 0);
    // What follows pushes strings outside any protected call, where a refused allocation reaches Lua's panic handler and
    // aborts the bar. Only the reader allocates from here on, so the config's ceiling has done its job.
    _limits.limitBytes = std::numeric_limits<size_t>::max();
    if (ran != LUA_OK) {
        throw LuaError(lua, ran, source);
    }
    if (lua_type(lua, -1) != LUA_TTABLE) {
        throw ConfigError(std::format("{}: must return a table, got {}", source, luaL_typename(lua, -1)));
    }
}

lua_State* LuaSandbox::State() const {
    return _lua.get();
}
