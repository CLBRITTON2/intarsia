cmake_minimum_required(VERSION 3.25)
project(intarsia LANGUAGES CXX RC)

# The version intarsia.exe's version resource carries. scripts/release.ps1 sets it, every other build stays 0.0.0.
set(INTARSIA_VERSION "0.0.0" CACHE STRING "Version stamped into intarsia.exe, as major.minor.patch")
if(NOT INTARSIA_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
  message(FATAL_ERROR "INTARSIA_VERSION must be major.minor.patch, got '${INTARSIA_VERSION}'")
endif()
set(INTARSIA_VERSION_MAJOR ${CMAKE_MATCH_1})
set(INTARSIA_VERSION_MINOR ${CMAKE_MATCH_2})
set(INTARSIA_VERSION_PATCH ${CMAKE_MATCH_3})
configure_file(src/intarsia.rc.in ${CMAKE_BINARY_DIR}/intarsia.rc @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL checks, Control Flow Guard and CET shadow stack compatibility for every target, Lua and the tests included.
# An incremental relink can leave the Control Flow Guard table stale, so an indirect call into an object it did not
# recompile fails fast in ntdll (FAST_FAIL_GUARD_ICALL_CHECK_FAILURE).
add_compile_options(/sdl /guard:cf)
add_link_options(/guard:cf /CETCOMPAT /INCREMENTAL:NO)

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
  src/LaunchOptions.cpp
  src/audio/CoreAudio.cpp
  src/bar/Bar.cpp
  src/bar/FrameStats.cpp
  src/battery/PowerStatus.cpp
  src/bar/Layout.cpp
  src/bar/Tooltip.cpp
  src/config/Config.cpp
  src/config/Style.cpp
  src/modules/AnimatedValue.cpp
  src/modules/Audio.cpp
  src/modules/Battery.cpp
  src/modules/BindingModes.cpp
  src/modules/Clock.cpp
  src/modules/Cpu.cpp
  src/modules/IconText.cpp
  src/modules/Memory.cpp
  src/modules/Network.cpp
  src/modules/Placeholders.cpp
  src/network/IpHelper.cpp
  src/modules/CommandPoller.cpp
  src/modules/Counter.cpp
  src/modules/CounterSampler.cpp
  src/modules/Custom.cpp
  src/modules/DirectionToggle.cpp
  src/modules/Divider.cpp
  src/modules/Fold.cpp
  src/modules/Group.cpp
  src/modules/ItemRow.cpp
  src/modules/ModuleFactory.cpp
  src/modules/TextModule.cpp
  src/modules/Ticks.cpp
  src/modules/Tray.cpp
  src/modules/Window.cpp
  src/modules/WindowIcons.cpp
  src/modules/WindowList.cpp
  src/modules/Workspaces.cpp
  src/platform/AppBar.cpp
  src/platform/ChangeSignal.cpp
  src/platform/Errors.cpp
  src/platform/FileWatcher.cpp
  src/platform/FrameClock.cpp
  src/platform/Handle.cpp
  src/platform/Log.cpp
  src/platform/PeerProcess.cpp
  src/platform/PerformanceCounter.cpp
  src/platform/Process.cpp
  src/platform/ShellLaunch.cpp
  src/platform/Text.cpp
  src/platform/Token.cpp
  src/platform/WebSocket.cpp
  src/render/BrushCache.cpp
  src/render/Graphics.cpp
  src/render/Icon.cpp
  src/render/Surface.cpp
  src/tray/NotifyIconProtocol.cpp
  src/tray/ShellNotifyIcon.cpp
  src/wm/GlazeWm.cpp
  src/wm/GlazeWmProtocol.cpp
  src/wm/Native.cpp
  src/wm/WindowManagerFactory.cpp
)
target_include_directories(intarsia_core PUBLIC src)
target_compile_definitions(intarsia_core PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(intarsia_core PUBLIC /W4 /WX /permissive- /utf-8)
target_link_libraries(intarsia_core PUBLIC lua nlohmann_json::nlohmann_json d3d11 dxgi d2d1 dwrite dcomp shcore winhttp iphlpapi windowscodecs wlanapi ws2_32 powrprof setupapi shlwapi pdh)

# Parsing these headers is most of every file's compile time, so each target parses them once. Off for the lint, since
# clang-tidy cannot read MSVC's precompiled headers.
option(INTARSIA_PRECOMPILE_HEADERS "Precompile the Windows, DirectX and standard headers" ON)
set(INTARSIA_PRECOMPILED_HEADERS
  <windows.h> <d2d1_1.h> <dwrite.h> <wrl/client.h> <nlohmann/json.hpp>
  <algorithm> <chrono> <filesystem> <format> <functional> <map> <memory> <mutex> <optional> <string> <string_view>
  <thread> <variant> <vector>
)
if(INTARSIA_PRECOMPILE_HEADERS)
  target_precompile_headers(intarsia_core PRIVATE ${INTARSIA_PRECOMPILED_HEADERS})
endif()

add_executable(intarsia WIN32 src/main.cpp src/intarsia.manifest ${CMAKE_BINARY_DIR}/intarsia.rc)
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
    tests/LaunchOptions.cpp
    tests/bar/Layout.cpp
    tests/bar/Tooltip.cpp
    tests/config/Config.cpp
    tests/config/Style.cpp
    tests/audio/CoreAudio.cpp
    tests/battery/PowerStatus.cpp
    tests/modules/AnimatedValue.cpp
    tests/modules/Audio.cpp
    tests/modules/Battery.cpp
    tests/modules/BindingModes.cpp
    tests/modules/Clock.cpp
    tests/modules/CommandPoller.cpp
    tests/modules/Counter.cpp
    tests/modules/CounterSampler.cpp
    tests/modules/Cpu.cpp
    tests/modules/DirectionToggle.cpp
    tests/modules/Fold.cpp
    tests/modules/IconText.cpp
    tests/modules/ItemRow.cpp
    tests/modules/Memory.cpp
    tests/modules/Network.cpp
    tests/modules/Placeholders.cpp
    tests/network/IpHelper.cpp
    tests/modules/Ticks.cpp
    tests/modules/Tray.cpp
    tests/modules/Window.cpp
    tests/modules/WindowIcons.cpp
    tests/modules/WindowList.cpp
    tests/modules/Workspaces.cpp
    tests/wm/GlazeWmProtocol.cpp
    tests/platform/AppBar.cpp
    tests/platform/ChangeSignal.cpp
    tests/platform/FileWatcher.cpp
    tests/platform/Log.cpp
    tests/platform/Process.cpp
    tests/platform/PeerProcess.cpp
    tests/platform/PerformanceCounter.cpp
    tests/platform/ShellLaunch.cpp
    tests/platform/Text.cpp
    tests/platform/WebSocket.cpp
    tests/platform/WindowProc.cpp
    tests/platform/WorkQueue.cpp
    tests/render/Icon.cpp
    tests/render/Surface.cpp
    tests/tray/NotifyIconProtocol.cpp
    tests/tray/ShellNotifyIcon.cpp
    tests/wm/Native.cpp
  )
  # Winsock, BCrypt and CryptoAPI play the far end of a loopback connection in the WebSocket and peer tests.
  target_link_libraries(intarsia_gtests PRIVATE intarsia_core GTest::gmock GTest::gtest_main ws2_32 bcrypt crypt32)
  target_compile_definitions(intarsia_gtests PRIVATE INTARSIA_WINDOWS_CONFIG="${CMAKE_SOURCE_DIR}/config/windows/config.lua"
                                                     INTARSIA_GLAZEWM_CONFIG="${CMAKE_SOURCE_DIR}/config/glazewm/config.lua")
  if(INTARSIA_PRECOMPILE_HEADERS)
    target_precompile_headers(intarsia_gtests PRIVATE ${INTARSIA_PRECOMPILED_HEADERS} <gmock/gmock.h> <gtest/gtest.h>)
  endif()
  gtest_discover_tests(intarsia_gtests)

  # Runs the real bar against the test GlazeWM server below, so ctest runs it apart from the unit tests by its label.
  # The bar's manifest makes it per monitor DPI aware too, so every rect, column and cursor position is in the bar's
  # physical pixels.
  add_executable(intarsia_integration src/intarsia.manifest tests/integration/Harness.cpp tests/integration/Actions.cpp tests/integration/Config.cpp tests/integration/Custom.cpp
                                      tests/integration/GlazeWm.cpp tests/integration/Tooltip.cpp tests/integration/Tray.cpp tests/integration/TrayApp.cpp)
  target_link_libraries(intarsia_integration PRIVATE intarsia_core GTest::gmock GTest::gtest_main ws2_32 bcrypt crypt32)
  target_compile_definitions(intarsia_integration PRIVATE INTARSIA_EXE="$<TARGET_FILE:intarsia>" INTARSIA_GLAZEWM_SERVER="$<TARGET_FILE:test_glazewm_server>"
                                                          INTARSIA_ARGV_RECORDER="$<TARGET_FILE:integration_argv_recorder>")
  add_dependencies(intarsia_integration intarsia test_glazewm_server integration_argv_recorder)
  gtest_discover_tests(intarsia_integration PROPERTIES LABELS integration)

  # The program the integration tests' actions and custom commands run.
  add_executable(integration_argv_recorder WIN32 tests/integration/ArgvRecorder.cpp)
  set_target_properties(integration_argv_recorder PROPERTIES OUTPUT_NAME argv_recorder)
  target_compile_definitions(integration_argv_recorder PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(integration_argv_recorder PRIVATE /W4 /WX /permissive- /utf-8)

  # The soak tests' IPC server (scripts/soak.ps1), named glazewm.exe because the bar's peer check requires it.
  add_executable(test_glazewm_server tests/support/GlazeWmServer.cpp)
  set_target_properties(test_glazewm_server PROPERTIES OUTPUT_NAME glazewm)
  target_compile_definitions(test_glazewm_server PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(test_glazewm_server PRIVATE /W4 /WX /permissive- /utf-8)
  target_link_libraries(test_glazewm_server PRIVATE ws2_32 bcrypt crypt32)

  # The soak tests' tray flood, sending the tray the payloads apps do. It takes their layout from the core.
  add_executable(soak_tray_sender tests/soak/TraySender.cpp)
  set_target_properties(soak_tray_sender PROPERTIES OUTPUT_NAME tray_sender)
  target_link_libraries(soak_tray_sender PRIVATE intarsia_core)
endif()
