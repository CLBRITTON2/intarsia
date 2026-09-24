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

# Everything but the entry point, so the tests link the same code the bar runs.
add_library(intarsia_core STATIC
  src/bar/Bar.cpp
  src/bar/Layout.cpp
  src/config/Config.cpp
  src/modules/Clock.cpp
  src/modules/ModuleFactory.cpp
  src/platform/AppBar.cpp
  src/platform/Errors.cpp
  src/platform/Text.cpp
  src/render/Graphics.cpp
)
target_include_directories(intarsia_core PUBLIC src)
target_compile_definitions(intarsia_core PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(intarsia_core PUBLIC /W4 /WX /permissive- /utf-8)
target_link_libraries(intarsia_core PUBLIC lua d3d11 dxgi d2d1 dwrite dcomp shcore)

add_executable(intarsia WIN32 src/main.cpp src/intarsia.manifest)
target_link_libraries(intarsia PRIVATE intarsia_core)

option(BUILD_TESTING "Build the GoogleTest unit tests" OFF)
if(BUILD_TESTING)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
    URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  # Link the CRT the same way as the code under test (/MD), and keep GoogleTest out of any install.
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  include(GoogleTest)
  add_executable(intarsia_gtests
    tests/bar/Layout.cpp
    tests/bar/Sections.cpp
    tests/config/Config.cpp
    tests/modules/Clock.cpp
    tests/platform/AppBar.cpp
    tests/platform/Text.cpp
  )
  target_link_libraries(intarsia_gtests PRIVATE intarsia_core GTest::gmock GTest::gtest_main)
  target_compile_definitions(intarsia_gtests PRIVATE INTARSIA_EXAMPLE_CONFIG="${CMAKE_SOURCE_DIR}/config/config.lua")
  gtest_discover_tests(intarsia_gtests)
endif()
