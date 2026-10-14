#pragma once

#include "platform/Handle.hpp"

#include <lua.h>

#include <cstddef>
#include <string>
#include <string_view>

// What the Lua state may use, and whether it was stopped, shared with the allocator and the hook through the
// allocator's user data.
struct LuaLimits {
    lua_Alloc allocate; // Lua's own allocator, which the limited one bounds
    void*     allocateData;
    size_t    usedBytes;
    size_t    limitBytes;
    bool      runaway;
};

// A Lua state that has run a config chunk and holds the value it returned, a table, on top of its stack. The chunk
// runs with only the libraries that cannot reach the filesystem or other processes, under a memory ceiling and an
// instruction count, and any error it raises or limit it reaches is a ConfigError naming source.
class LuaSandbox {
  public:
    LuaSandbox(std::string_view chunk, const std::string& source);

    // The allocator's user data points at _limits.
    LuaSandbox(const LuaSandbox&)            = delete;
    LuaSandbox& operator=(const LuaSandbox&) = delete;

    lua_State*  State() const;

  private:
    // Declared before the state, whose close frees through the limited allocator.
    LuaLimits                              _limits;
    UniqueReleased<lua_State*, &lua_close> _lua;
};
