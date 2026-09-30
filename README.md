# intarsia

A native status bar for Windows. Every monitor gets a bar docked to one screen edge, built
from small modules that a Lua config places into left, center and right sections and a CSS file styles. It draws with
Direct2D and DirectWrite, and runs alongside GlazeWM or on plain Windows.

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

## Integration tests

```powershell
pwsh -File scripts/integration.ps1
```

This runs the real bar against a test GlazeWM IPC server and judges it only by what it does outside its process, such
as the commands the server receives for clicks along the bar. `tests/integration/` holds the harness and the cases.
Each step prints a line: the processes started, every command a sweep of clicks sent, pushed events, pixels found, the
bar's exit code, and then the bar's log. Run it from an interactive desktop session.

## Stress tests

```powershell
pwsh -File scripts/stress.ps1 -Preset release
```

This runs the real bar through a set of scenarios, each against a test GlazeWM IPC server
(`tests/stress/GlazeWmServer.cpp`): idle, steady events, an event flood, malformed and oversized messages, a server that
never finishes the handshake, one that drops the connection every second or two, one that rejects every query, and a
config rewritten twenty times a second. Custom modules run commands that print past the output cap, outlive their
timeout and fail. The bar is sampled every second and a scenario fails if it crashes, stops answering, logs a fatal
error, leaks memory, handles, GDI or USER objects or threads, idles above 3% of a core, or does not exit cleanly.
`-Scenario` picks scenarios, `-Seconds` sets how long each runs, and a long `-Seconds` makes it a soak test.

## Lint

```powershell
pwsh -File scripts/lint.ps1
```

This checks formatting with clang-format and runs clang-tidy over `src/` and `tests/`, with the rules in
`.clang-format` and `.clang-tidy`. It refuses any LLVM major other than 19, since formatting changes between majors.

## Release

```powershell
pwsh -File scripts/changelog.ps1 -Version 0.2.0 -From v0.1.0
pwsh -File scripts/release.ps1 -Version 0.2.0
```

`changelog.ps1` adds a section for the version to `CHANGELOG.md`, one line per commit since the previous release's tag
(for the first release, pass the first commit). Edit it and commit. `release.ps1` then builds from a clean
`build\dist` with the version stamped into `intarsia.exe`, and packages `build\dist\intarsia-<version>.zip` with both
configs, this README, the license and the changelog, and `build\dist\release-notes.md` with the version's section. It
refuses uncommitted changes and a version the changelog lacks. The Release workflow runs the tests and both steps on
`windows-2022`, then `scripts/publish-release.ps1` publishes the zip as the GitHub release `v<version>`.

## Run

The bar reads `%USERPROFILE%\.config\intarsia\config.lua` for what it shows and `style.css` beside it for how it
looks. Start from `config\windows`, a plain bar that needs nothing but Windows, or `config\glazewm`, which runs with
GlazeWM and documents every key and selector:

```powershell
New-Item -ItemType Directory -Force $env:USERPROFILE\.config\intarsia | Out-Null
Copy-Item config\windows\config.lua, config\windows\style.css $env:USERPROFILE\.config\intarsia
Start-Process build\release\intarsia.exe -RedirectStandardError $env:TEMP\intarsia.log
```

`style.css` is a subset of CSS: `*` for the bar-wide font and color, `section` and `tooltip` for their boxes, and each
module by `.module`, its type (`clock`) or its name in the config (`#date`), with its `.icon` and `.item`s inside. The
example lists every selector it takes. `--config <path>` reads another config and the `style.css` beside it, and
`--glazewm-port <port>` connects to GlazeWM on a port other than 6123. `--log <path>` writes the log file somewhere
other than `%LOCALAPPDATA%\intarsia\intarsia.log`, which a running bar keeps open. Saving either file rebuilds the bars. A wrong
key, selector or property stops the bar with a message naming the file and where in it. The bar logs structured lines to stderr, which only show when stderr is redirected as above. Stop it
with `taskkill /IM intarsia.exe`, without `/F`, so each bar gives its screen space back.

## Layout

| Path | Holds |
| --- | --- |
| `src/bar/` | one bar per monitor, section layout, tooltips |
| `src/modules/` | the modules and the factory that builds them from the config |
| `src/wm/` | window manager backends: GlazeWM over its IPC, and plain Windows |
| `src/config/` | the Lua config loader and the style sheet parser |
| `src/render/` | the shared Direct3D, Direct2D, DirectWrite and DirectComposition devices |
| `src/platform/` | Win32 pieces: appbar docking, processes, file watching, WebSocket, logging |
| `config/windows/` | a plain config and style that need nothing but Windows |
| `config/glazewm/` | a config and style for GlazeWM that document every key and selector |
| `tests/` | unit tests mirroring `src/`, and `tests/stress/` for the stress tests' IPC server |
| `CHANGELOG.md` | what each release changed |
| `scripts/` | build, test, stress test, lint, changelog and release |

## License

Apache License 2.0, in [LICENSE](LICENSE).
