# Changelog

What each release changed, newest first. `scripts/changelog.ps1` starts a version's section from the commits since the
last release.

## 0.1.0 (2026-09-28)

- Create the swap chain once the bar is docked so the date draws
- Read bar edge, size, font and colors from a Lua config
- Split the bar into modules and sections with a Lua config
- Build Lua from source with FetchContent instead of vcpkg
- Add design doc, todo list and test plan
- Move the test suite to GoogleTest with per-case ctest discovery
- Reload the Lua config live and add a native window manager backend with a focused window module
- Add a GlazeWM backend and workspaces module with a verified, bounded IPC client
- Click a workspace to focus it in GlazeWM, with commands sent off the UI thread
- Add cpu and memory modules and configurable input actions for every text module
- Define modules by name with a type and show them in lists by name
- Harden launching and config loading, share logging and work queues, and name private members _camelCase
- Add a custom module that shows the first line a command prints
- Rebuild bars when monitors are added, removed, resized or rescaled
- Recover from a lost graphics device by rebuilding the device and the bars
- Add tooltips and bar.layer, drawn through a Surface shared by the bar and its popups
- Read custom module output as JSON, and reject control and bidi characters in all script output
- Skip redraws when nothing changed, bound the GlazeWM handshake, verify the IPC peer by connection and integrity, and stop untrusted text and oversized tooltips from crashing the bar
- Show window icons for the displayed workspace, skip redraws when nothing changed, harden the GlazeWM IPC and untrusted text handling, and pass lint
- Show the active binding modes and the focused container's tiling direction, click to flip it
- Replace the docs folder with a README covering build, test, lint and run
- Let module actions send GlazeWM commands directly
- Add a stress test harness and fix what it found: quit under an event flood, quit on taskkill after a reload, survive reading a half-saved config; send GlazeWM commands from module actions
- Use fixed-width integer types throughout and license the project under Apache 2.0
- Split CI into manual lint, test and smoke workflows on windows-2022
- Style the bar from a style.css beside the config and precompile headers
- Add an audio module for the default output's volume, with click to mute and scroll to change it
- Add a network module, named format placeholders and item hover
- Fix lint failures: clang-format the new modules and give CoreAudio.cpp the COM headers the lint build lacks
- Add a battery module showing charge, charging state and health, with an .alert state below a configured charge
- Add a system tray module with style-driven unfold animation
- Stop defaulting a divider to a 1px line, so an unstyled divider draws nothing, and move the appbar copy-data constant into the only test that uses it
- Share one cached icon-and-text line across the text, audio, network, battery, tiling direction and window modules
- Name every module's format values ({time}, {usage}, {used}, {text}, {title}) and drop the positional {} formats
- Give IModule default input and tooltip handlers and drop the modules' empty overrides
- Style the tray's toggle with .item.toggle and keep the hover on items that move under a still cursor
- Add a tray stress scenario flooding a tray of its own class, and upload every scenario's logs from CI
- Turn the tray animation around on every click instead of queueing clicks until it ends
- Add layouts: section lists a bar picks by its monitor's length along its edge
- Center each icon by its drawn glyph so icons from any font line up, and draw the example style's workspace items as circles without its network icon size override
- Post mouse moves, double clicks and middle clicks to tray icons, add on_middle_click, and let a module with nothing to show take no room
- Open and close tray popups for icons whose apps draw their own, in place of the bar's tooltip
- Reserve the bar's strip while it shows a tray by forwarding its own appbar requests with a result Explorer can open, and add bar.space to share the strip with the work area instead
- Cycle the example style's module icons through rose, magenta and cyan from left to right
- Split the shipped config into a basic build that needs nothing but Windows + the full example
- Add a changelog, a version resource and scripts that draft release notes, package a release and publish it

