cmake_minimum_required(VERSION 3.25)
project(intarsia LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL checks, Control Flow Guard and CET shadow stack compatibility for every target, Lua and the tests included.
add_compile_options(/sdl /guard:cf)
add_link_options(/guard:cf /CETCOMPAT)

include(FetchContent)

# Lua ships no CMake build, so the release tarball is built here as a static library.
FetchContent_Declare(lua
  URL https://www.lua.org/ftp/lua-5.5.1.tar.gz
  URL_HASH SHA256=1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(lua json)

file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/src/*.c)
list(REMOVE_ITEM LUA_SOURCES ${lua_SOURCE_DIR}/src/lua.c ${lua_SOURCE_DIR}/src/luac.c)
# Built as C++ so Lua raises errors as C++ exceptions: its C build longjmps, which skips C++ destructors.
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${lua_SOURCE_DIR}/src)

# Everything but the entry point, so the tests link the same code the bar runs.
add_library(intarsia_core STATIC
  src/App.cpp
  src/bar/Bar.cpp
  src/bar/Layout.cpp
  src/bar/Tooltip.cpp
  src/config/Config.cpp
  src/modules/BindingModes.cpp
  src/modules/Clock.cpp
  src/modules/Cpu.cpp
  src/modules/IconText.cpp
  src/modules/Memory.cpp
  src/modules/CommandPoller.cpp
  src/modules/Custom.cpp
  src/modules/DirectionToggle.cpp
  src/modules/ModuleFactory.cpp
  src/modules/TextModule.cpp
  src/modules/Ticks.cpp
  src/modules/Window.cpp
  src/modules/WindowIcons.cpp
  src/modules/WindowList.cpp
  src/modules/Workspaces.cpp
  src/platform/AppBar.cpp
  src/platform/Errors.cpp
  src/platform/FileWatcher.cpp
  src/platform/Handle.cpp
  src/platform/Log.cpp
  src/platform/PeerProcess.cpp
  src/platform/Process.cpp
  src/platform/Text.cpp
  src/platform/WebSocket.cpp
  src/render/Graphics.cpp
  src/render/Surface.cpp
  src/wm/GlazeWm.cpp
  src/wm/GlazeWmProtocol.cpp
  src/wm/Native.cpp
  src/wm/WindowManagerFactory.cpp
)
target_include_directories(intarsia_core PUBLIC src)
target_compile_definitions(intarsia_core PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(intarsia_core PUBLIC /W4 /WX /permissive- /utf-8)
target_link_libraries(intarsia_core PUBLIC lua nlohmann_json::nlohmann_json d3d11 dxgi d2d1 dwrite dcomp shcore winhttp iphlpapi windowscodecs)

add_executable(intarsia WIN32 src/main.cpp src/intarsia.manifest)
target_link_libraries(intarsia PRIVATE intarsia_core)
# 0x800 is LOAD_LIBRARY_SEARCH_SYSTEM32: the loader resolves the executable's imports (d2d1, winhttp, the C runtime)
# from System32 only, never from a DLL planted beside it.
target_link_options(intarsia PRIVATE /DEPENDENTLOADFLAG:0x800)

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
    tests/bar/Tooltip.cpp
    tests/config/Config.cpp
    tests/modules/BindingModes.cpp
    tests/modules/Clock.cpp
    tests/modules/CommandPoller.cpp
    tests/modules/Cpu.cpp
    tests/modules/DirectionToggle.cpp
    tests/modules/Memory.cpp
    tests/modules/Ticks.cpp
    tests/modules/Window.cpp
    tests/modules/WindowIcons.cpp
    tests/modules/WindowList.cpp
    tests/modules/Workspaces.cpp
    tests/wm/GlazeWmProtocol.cpp
    tests/platform/AppBar.cpp
    tests/platform/FileWatcher.cpp
    tests/platform/Process.cpp
    tests/platform/PeerProcess.cpp
    tests/platform/Text.cpp
    tests/platform/WebSocket.cpp
    tests/platform/WorkQueue.cpp
    tests/render/Surface.cpp
    tests/wm/Native.cpp
  )
  # Winsock, BCrypt and CryptoAPI play the far end of a loopback connection in the WebSocket and peer tests.
  target_link_libraries(intarsia_gtests PRIVATE intarsia_core GTest::gmock GTest::gtest_main ws2_32 bcrypt crypt32)
  target_compile_definitions(intarsia_gtests PRIVATE INTARSIA_EXAMPLE_CONFIG="${CMAKE_SOURCE_DIR}/config/config.lua")
  gtest_discover_tests(intarsia_gtests)
endif()
