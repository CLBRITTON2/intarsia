# intarsia

A native status bar for Windows. Every monitor gets a bar docked to one screen edge, built from small modules that a
Lua config places into left, center and right sections and a style file styles. It draws with Direct2D and
DirectWrite, and runs alongside GlazeWM or on plain Windows.

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

The bar lands in `build\<preset>\intarsia.exe`, with `intarsia-cli.exe` beside it. The scripts enter the Visual
Studio developer shell themselves and put its CMake and Ninja first on PATH, because another CMake (such as MSYS2's)
can carry a CA bundle that fails the dependency downloads.

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

This runs the real bar against a test GlazeWM IPC server and judges it only by what it does outside its process.
`Get-Help scripts/run-integration-tests.ps1 -Full` describes what it prints.

## Soak tests

```powershell
pwsh -File scripts/run-soak-tests.ps1 -Preset release
```

This runs the real bar through the load real use puts on it and fails on a crash, a leak or a hang.
`Get-Help scripts/run-soak-tests.ps1 -Full` describes each scenario, what fails one and the parameters.

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
from a clean `build\dist` with the version stamped into both executables, and packages
`build\dist\intarsia-<version>.zip` with both configs, the docs, this README, the license and the changelog, and
`build\dist\release-notes.md` with the version's section. It refuses uncommitted changes and a version the changelog
lacks. The Release workflow runs the tests and both steps on `windows-2022`, then `scripts/publish-release.ps1`
publishes the zip as the GitHub release `v<version>`.

## Run

[Installing intarsia](docs/install.md) covers the starter configs, starting and stopping the bar, its options and
its log. A source build runs as `build\release\intarsia.exe`, with the starter configs in `config\`.

## Layout

| Path | Holds |
| --- | --- |
| `src/bar/` | one bar per monitor, section layout, tooltips, menus |
| `src/modules/` | the modules and the factory that builds them from the config |
| `src/wm/` | window manager backends: GlazeWM over its IPC, and plain Windows |
| `src/config/` | the Lua config loader and the style sheet parser |
| `src/render/` | the shared Direct3D, Direct2D, DirectWrite and DirectComposition devices |
| `src/platform/` | Win32 pieces: appbar docking, processes, file watching, WebSocket, logging |
| `src/audio/` | the default playback and recording devices and their volume, through Core Audio |
| `src/battery/` | the batteries' charge and whether it is rising or falling |
| `src/network/` | the interface the default route leaves through, Wi-Fi or other |
| `src/tray/` | the notification area window that receives apps' icons, and its message format |
| `src/cli/` | `intarsia-cli.exe`, the console program that prints what the bar sees |
| `config/windows/` | a plain config and style that need nothing but Windows |
| `config/glazewm/` | a config and style for GlazeWM that document every key and block |
| `tests/` | unit tests mirroring `src/` |
| `tests/integration/` | the integration tests and their harness |
| `tests/soak/` | the programs the soak tests feed the bar with |
| `tests/support/` | helpers and programs the tests share, such as the test GlazeWM server |
| `CHANGELOG.md` | what each release changed |
| `scripts/` | build, test, soak test, lint, changelog and release |

## License

Apache License 2.0, in [LICENSE](LICENSE).
