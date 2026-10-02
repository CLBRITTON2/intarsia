# intarsia

A native status bar for Windows. Every monitor gets a bar docked to one screen edge, built
from small modules that a Lua config places into left, center and right sections and a style file styles. It draws with
Direct2D and DirectWrite, and runs alongside GlazeWM or on plain Windows.

## Docs

- [Installing intarsia](docs/install.md)
- [Configuring the bar](docs/configuration.md), with every module
- [Styling the bar](docs/styling.md)
- [Custom modules](docs/custom-modules.md): your own scripts and menus on the bar

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
pwsh -File scripts/run-unit-tests.ps1
```

This builds the debug preset and runs the GoogleTest suite through ctest. `tests/` mirrors `src/`, one file per unit.
Some tests create windows and a Direct3D device, so run them from an interactive desktop session.

## Integration tests

```powershell
pwsh -File scripts/run-integration-tests.ps1
```

This runs the real bar against a test GlazeWM IPC server and judges it only by what it does outside its process, such
as the commands the server receives for clicks along the bar, the colors it draws before and after a config or style
save, its exit code and the messages a tray app gets.
`tests/integration/` holds the harness and the cases. Each step prints a line: the processes started, every command a
sweep of clicks sent, pushed events, tray messages sent and received, pixels found, the bar's exit code, and then the
bar's log. Run it from an interactive desktop session.

## Soak tests

```powershell
pwsh -File scripts/run-soak-tests.ps1 -Preset release
```

This runs the real bar through the load real use puts on it, each scenario against a test GlazeWM IPC server
(`tests/support/GlazeWmServer.cpp`): idle, steady events, a server that drops the connection every second or two, a
config rewritten twenty times a second, a tray that apps keep adding, changing and removing icons in, and a foreground
window that retitles itself about 60 times a second (`tests/soak/WindowSender.cpp`, which must keep the foreground). Custom
modules run commands that print past the output cap, outlive their timeout and fail. The bar is sampled every second
and a scenario fails if it crashes, stops answering, logs a fatal error, leaks memory, handles, GDI or USER objects or
threads, idles above 3% of a core, or does not exit cleanly. `-Scenario` picks scenarios and `-Seconds` sets how long
each runs. A run of 600 seconds or more judges growth by its trend per hour over the run's second half instead, as in
`-Seconds 3600 -Scenario normal,tray` for an hour-long check.

## Lint

```powershell
pwsh -File scripts/lint.ps1
```

This checks formatting with clang-format and runs clang-tidy over `src/` and `tests/`, with the rules in
`.clang-format` and `.clang-tidy`. It refuses any LLVM major other than 19, since formatting changes between majors.

## Release

```powershell
pwsh -File scripts/draft-changelog.ps1 -Version 0.2.0 -From v0.1.0
pwsh -File scripts/package-release.ps1 -Version 0.2.0
```

`draft-changelog.ps1` adds a section for the version to `CHANGELOG.md`, one line per commit since the previous
release's tag (for the first release, pass the first commit). Edit it and commit. `package-release.ps1` then builds
from a clean `build\dist` with the version stamped into `intarsia.exe`, and packages `build\dist\intarsia-<version>.zip`
with both configs, the docs, this README, the license and the changelog, and `build\dist\release-notes.md` with the version's
section. It refuses uncommitted changes and a version the changelog lacks. The Release workflow runs the tests and both steps on
`windows-2022`, then `scripts/publish-release.ps1` publishes the zip as the GitHub release `v<version>`.

## Run

The bar reads `%USERPROFILE%\.config\intarsia\config.lua` for what it shows and `style.conf` beside it for how it
looks. Start from `config\windows`, a plain bar that needs nothing but Windows, or `config\glazewm`, which runs with
GlazeWM and documents every key:

```powershell
New-Item -ItemType Directory -Force $env:USERPROFILE\.config\intarsia | Out-Null
Copy-Item config\windows\config.lua, config\windows\style.conf $env:USERPROFILE\.config\intarsia
Start-Process build\release\intarsia.exe -RedirectStandardError $env:TEMP\intarsia.log
```

`style.conf` is indented `key: value` blocks: `bar` for the bar-wide font and color, `section` and `tooltip` for their
boxes, `module` for every module and a module's name in the config (`date`) for one, with its `icon` and `item` blocks
inside. [Styling](docs/styling.md) lists every block. `--config <path>` reads another config and the `style.conf`
beside it, and
`--glazewm-port <port>` connects to GlazeWM on a port other than 6123. `--log <path>` writes the log file somewhere
other than `%LOCALAPPDATA%\intarsia\intarsia.log`, which a running bar keeps open. Saving either file rebuilds the bars. A wrong
key, block or property stops the bar with a message naming the file and where in it. The bar logs structured lines to stderr, which only show when stderr is redirected as above. Stop it
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
| `config/glazewm/` | a config and style for GlazeWM that document every key and block |
| `tests/` | unit tests mirroring `src/`, `tests/integration/`, `tests/soak/`, and `tests/support/` for the test GlazeWM server both use |
| `CHANGELOG.md` | what each release changed |
| `scripts/` | build, test, soak test, lint, changelog and release |

## License

Apache License 2.0, in [LICENSE](LICENSE).
