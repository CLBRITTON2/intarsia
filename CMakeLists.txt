cmake_minimum_required(VERSION 3.25)
project(intarsia LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Lua REQUIRED)

add_executable(intarsia WIN32
  src/main.cpp
  src/errors.cpp
  src/graphics.cpp
  src/appbar.cpp
  src/clock.cpp
  src/config.cpp
  src/text.cpp
  src/bar_window.cpp
  src/intarsia.manifest
)

target_compile_definitions(intarsia PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(intarsia PRIVATE /W4 /WX /permissive- /utf-8)
target_include_directories(intarsia PRIVATE ${LUA_INCLUDE_DIR})
target_link_libraries(intarsia PRIVATE ${LUA_LIBRARIES} d3d11 dxgi d2d1 dwrite dcomp shcore)
