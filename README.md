# intarsia

A native status bar for Windows in the spirit of Waybar. Every monitor gets a bar docked to one screen edge, built
from small modules that a Lua config places into left, center and right sections. It draws with Direct2D and
DirectWrite, and runs alongside GlazeWM or on plain Windows.

## Requirements

- Windows 10 version 1903 or later.
- Visual Studio 2022 with the Desktop development with C++ workload, including its C++ CMake tools and the bundled
  LLVM (clang-format and clang-tidy 19, used by the lint).

Nothing else needs installing. CMake fetches Lua 5.5.1, nlohmann_json 3.12.0 and GoogleTest 1.18.0 over HTTPS, each
pinned by SHA256.

## Build

```powershell
pwsh -File scripts/build.ps1 -Preset debug
pwsh -File scripts/build.ps1 -Preset release
```

The executable lands in `build\<preset>\intarsia.exe`. The scripts enter the Visual Studio developer shell themselves
and put its CMake and Ninja first on PATH, because another CMake (such as MSYS2's) can carry a CA bundle that fails
the dependency downloads.

## Test

```powershell
pwsh -File scripts/test.ps1
```

This builds the debug preset and runs the GoogleTest suite through ctest. `tests/` mirrors `src/`, one file per unit.
Some tests create windows and a Direct3D device, so run them from an interactive desktop session.

## Lint

```powershell
pwsh -File scripts/lint.ps1
```

This checks formatting with clang-format and runs clang-tidy over `src/` and `tests/`, with the rules in
`.clang-format` and `.clang-tidy`. It refuses any LLVM major other than 19, since formatting changes between majors.

## Run

The bar reads `%USERPROFILE%\.config\intarsia\config.lua`. Start from the commented example:

```powershell
New-Item -ItemType Directory -Force $env:USERPROFILE\.config\intarsia | Out-Null
Copy-Item config\config.lua $env:USERPROFILE\.config\intarsia\config.lua
Start-Process build\release\intarsia.exe -RedirectStandardError $env:TEMP\intarsia.log
```

Saving the config rebuilds the bars. A config with a wrong key or type stops the bar with a message naming the file
and the key. The bar logs structured lines to stderr, which only show when stderr is redirected as above. Stop it
with `taskkill /IM intarsia.exe`, without `/F`, so each bar gives its screen space back.

## Layout

| Path | Holds |
| --- | --- |
| `src/bar/` | one bar per monitor, section layout, tooltips |
| `src/modules/` | the modules and the factory that builds them from the config |
| `src/wm/` | window manager backends: GlazeWM over its IPC, and plain Windows |
| `src/config/` | the Lua config loader |
| `src/render/` | the shared Direct3D, Direct2D, DirectWrite and DirectComposition devices |
| `src/platform/` | Win32 pieces: appbar docking, processes, file watching, WebSocket, logging |
| `config/config.lua` | the example config, documenting every key |
| `scripts/` | build, test and lint, which CI runs too |
