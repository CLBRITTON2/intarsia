cmake_minimum_required(VERSION 3.25)
project(intarsia LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Lua ships no CMake build, so the release tarball is built here as a static library.
FetchContent_Declare(lua
  URL https://www.lua.org/ftp/lua-5.5.1.tar.gz
  URL_HASH SHA256=1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(lua)

file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/src/*.c)
list(REMOVE_ITEM LUA_SOURCES ${lua_SOURCE_DIR}/src/lua.c ${lua_SOURCE_DIR}/src/luac.c)
# Built as C++ so Lua raises errors as C++ exceptions: its C build longjmps, which skips C++ destructors.
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${lua_SOURCE_DIR}/src)

add_executable(intarsia WIN32
  src/main.cpp
  src/bar/Bar.cpp
  src/bar/Layout.cpp
  src/config/Config.cpp
  src/modules/Clock.cpp
  src/modules/ModuleFactory.cpp
  src/platform/AppBar.cpp
  src/platform/Errors.cpp
  src/platform/Text.cpp
  src/render/Graphics.cpp
  src/intarsia.manifest
)

target_include_directories(intarsia PRIVATE src)
target_compile_definitions(intarsia PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(intarsia PRIVATE /W4 /WX /permissive- /utf-8)
target_link_libraries(intarsia PRIVATE lua d3d11 dxgi d2d1 dwrite dcomp shcore)
