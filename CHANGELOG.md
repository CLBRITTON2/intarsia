# Changelog

What each release changed, newest first. `scripts/draft-changelog.ps1` starts a version's section from the commits
since the last release.

## 0.3.0 (2026-10-19)

### New

- `blur: wallpaper <radius>` blurs the monitor's wallpaper behind sections, tooltips and menus, and stays when a
  window is dragged over the bar. See `docs/styling.md`.

### Fixes

- Borders and inset shadows of one length draw equally wide on every side at fractional scales.
- An app's tray popup closes when the bars are rebuilt.
- The bar redraws at the right size after a DPI change that keeps its pixel size.

## 0.2.0 (2026-10-15)

### Config changes

- The style moves from `style.css` to `style.conf`, blocks of `key: value` properties. See `docs/styling.md`.
- `bar.monitors` is required. It picks which monitors get a bar.
- The divider module is gone. Use borders, box shadows and margins instead.
- The starter configs are now `config/windows` and `config/glazewm`.
- The config's Lua no longer has pattern matching.

### New

- `intarsia-cli check` validates a config, and `intarsia-cli monitors` lists monitor ids.
- `counter` and `group` modules, and the microphone through `audio` with `flow = "input"`.
- Menus built from a command's output, and custom commands that run continuously.
- Bars on the left and right edges.
- Background blur behind sections, tooltips and menus, and fades on boxes and items.
- A rotated log file, and `--log` to put it elsewhere.
- `scripts/install-build.ps1` builds and installs from source.

### Fixes

- The bar survives monitor changes, Explorer restarts and lost graphics devices instead of exiting.
- Tray icons reach the bar when it is started elevated.
- An animating tray no longer grows memory without limit.
- Animations are paced to each monitor's refresh and step evenly.
- Hostile configs, tray icons, window titles and command output can no longer crash or freeze the bar.

## 0.1.0 (2026-09-28)

- First release: a bar per monitor, configured in Lua, styled with a CSS subset, reloaded live on save.
- Modules: clock, cpu, memory, window, audio, network, battery, tray, custom commands, and GlazeWM's workspaces, binding
  modes and tiling direction.
- Tooltips, click and scroll actions, and layouts picked by each monitor's length.
