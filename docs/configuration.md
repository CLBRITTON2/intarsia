# Configuration

The bar reads `%USERPROFILE%\.config\intarsia\config.lua`, a Lua file that returns one table. Appearance lives in
`style.css` instead (see [Styling](styling.md)).

Every key listed here is required unless it's marked optional, and unknown keys are errors. A mistake stops the bar
with a message naming the line.

```lua
return {
    bar = { edge = "top", layer = "top", space = "reserve", thickness = 36 },
    window_manager = "windows",
    modules_left = { "title" },
    modules_center = {},
    modules_right = { "cpu", "memory", "clock" },
    layouts = {},
    modules = {
        title = { type = "window", max_length = 60 },
        cpu = { type = "cpu", format = "{usage}%", icon = "\u{E950}", interval = 2 },
        memory = { type = "memory", format = "{used:.1f} GB", icon = "\u{E964}", interval = 2 },
        clock = { type = "clock", format = "{time:%H:%M}", icon = "", interval = 60 },
    },
}
```

## bar

| Key | Values | |
| --- | --- | --- |
| `edge` | `top`, `bottom`, `left`, `right` | The screen edge the bar docks to. |
| `layer` | `top`, `bottom` | `top` stays above windows and drops behind fullscreen apps. `bottom` lets windows cover it. |
| `space` | `reserve`, `share` | `reserve` shrinks the work area so maximized windows stop at the bar. `share` leaves it alone, for window managers that leave their own gap. |
| `thickness` | number up to 10000 | Height (or width on a side edge) in DIPs, scaled by each monitor's DPI. |

## window_manager

- `windows`: no window manager. Window titles and focus come from Windows.
- `glazewm`: connects to GlazeWM, and retries while it's down. Required for `workspaces`, `windows`, `binding_mode` and
  `tiling_direction`.

## Sections

`modules_left`, `modules_center` and `modules_right` list module names in display order. `{}` leaves a section empty.

Every listed name must be defined under `modules`, and every defined module must appear in a list or a group. Names
use letters, digits, `_` and `-`.

## layouts

A layout swaps in different section lists on monitors at least `min_length` DIPs long along the bar's edge (for a top
bar, the width in pixels divided by the scale, so 2560 px at 125% is 2048).

```lua
layouts = {
    {
        min_length = 1600,
        modules_left = { "title" },
        modules_center = { "clock" },
        modules_right = { "cpu", "memory" },
    },
},
```

A monitor uses the last layout it qualifies for, so list them in increasing `min_length`. Use `layouts = {}` for none.

## modules

Each entry is a name you choose and a table with a `type`. You can define the same type several times under different
names, and `style.css` can target each one by name (`#work_clock`).

### Common keys

- `format`: text with `{placeholders}`. Placeholders take a
  [std::format spec](https://en.cppreference.com/w/cpp/utility/format/spec): `{used:.1f}` for one decimal,
  `{usage:>3}` to pad to three characters.
- `icon`: a glyph shown before the text, or `""`.
- `interval`: whole seconds between updates, up to 86400 (a day).
- `tooltip` (optional): a second format shown on hover. `\n` breaks lines.
- Actions (optional): `on_click`, `on_right_click`, `on_middle_click`, `on_scroll_up`, `on_scroll_down`.

`tooltip` and actions work on `clock`, `window`, `cpu`, `memory`, `audio`, `network`, `battery`, `custom` and
`counter`.

### Actions

An action has one of three forms.

**Run a program:** the program, then its arguments.

```lua
on_click = { "C:\\Windows\\System32\\Taskmgr.exe" },
on_right_click = { "C:\\Windows\\explorer.exe", "ms-settings:display" },
```

The program must resolve to an `.exe`, by absolute path or on PATH. It runs without a shell, so arguments arrive
exactly as written. For a script, run `pwsh.exe -File <script>`.

**Send a GlazeWM command** (`glazewm` only):

```lua
on_scroll_up = { window_manager = "focus --prev-workspace" },
```

**Open a menu** built from a command's output. See [Menus](custom-modules.md#menus).

### clock

```lua
clock = {
    type = "clock",
    format = "{time:%a %d %b  %H:%M}",
    tooltip = "{time:%A %d %B %Y, %H:%M:%S}",
    icon = "\u{E823}",
    interval = 60,
},
```

`{time:...}` takes a [chrono format spec](https://en.cppreference.com/w/cpp/chrono/system_clock/formatter) (`%H`,
`%M`, `%a`, `%d` and so on). Updates align to the clock, so `interval = 60` ticks on the minute. Use `1` if you show
seconds.

### cpu

```lua
cpu = {
    type = "cpu",
    format = "{usage}%",
    tooltip = "Speed {ghz:.2f} GHz\n{cores}",
    icon = "\u{E950}",
    interval = 2,
},
```

| Placeholder | |
| --- | --- |
| `{usage}` | Total load, whole percent. |
| `{ghz}` | Current clock speed. |
| `{max_core}` | Busiest logical processor, whole percent. |
| `{cores}` | Every logical processor's load, four per line. |

### memory

```lua
memory = {
    type = "memory",
    format = "{used:.1f} GB",
    tooltip = "{used:.1f} of {total:.0f} GB ({percent}%)",
    icon = "\u{E964}",
    interval = 2,
},
```

`{used}`, `{total}`, `{available}`, `{cached}`, `{committed}` and `{commit_limit}` are in GB. `{percent}` is RAM in use
as a whole percent.

### window

The focused window's title.

```lua
title = { type = "window", max_length = 80, tooltip = "{title}" },
```

`max_length` is in characters. Longer titles are cut with an ellipsis. `{title}` in the tooltip is the full title.

### audio

```lua
volume = {
    type = "audio",
    flow = "output",
    format = "{volume}%",
    format_muted = "muted",
    icon = "\u{E767}",
    icon_muted = "\u{E74F}",
    step = 5,
    tooltip = "{device}",
},
```

- `flow`: `output` for speakers, `input` for the microphone. Define two modules to show both.
- `step`: volume change per scroll notch, 1 to 100.
- `{volume}` is a whole percent, `{device}` the device name.

Clicking toggles mute and scrolling changes volume unless you set those actions yourself. The module follows the
default device, and shows nothing while there isn't one.

### network

```lua
net = {
    type = "network",
    format_wifi = "{signal}%",
    format_ethernet = "",
    format_disconnected = "offline",
    icons_wifi = { "\u{E872}", "\u{E873}", "\u{E874}", "\u{E701}" },
    icon_ethernet = "\u{E839}",
    icon_disconnected = "\u{EB55}",
    tooltip = "{interface} {ssid}\n{ip}\n{down} down, {up} up",
},
```

- `format_ethernet` covers every connection that isn't Wi-Fi. An empty format shows only the icon.
- `icons_wifi` runs from weakest to strongest signal, any length.
- Placeholders: `{ssid}`, `{signal}` (percent), `{ip}`, `{interface}`, `{down}` and `{up}` (rates like `1.2 MB/s`).

Windows 11 hides the SSID and signal from apps without location access (Settings, Privacy & security, Location).

### battery

```lua
battery = {
    type = "battery",
    format_discharging = "{capacity}%",
    format_charging = "{capacity}%",
    format_plugged = "{capacity}%",
    icons = { "\u{E850}", "\u{E853}", "\u{E856}", "\u{E859}", "\u{E83F}" },
    icon_charging = "\u{E83E}",
    icon_plugged = "\u{EA93}",
    warning_at = 30,
    critical_at = 15,
    tooltip = "{capacity}% {time}\nHealth {health}%",
},
```

- `plugged` means on AC but not charging (full, or held by a charge limit).
- `icons` runs from emptiest to fullest and is used on battery.
- At or below `warning_at` and `critical_at` percent on battery, the module takes its `.warning` or `.critical` style.
  `critical_at` must be below `warning_at`.
- `{capacity}` is a percent. `{time}` is time left as `1:05`, empty until Windows estimates it. `{health}` is current
  full capacity against design capacity, empty if the battery doesn't report it.

On a machine without a battery the module shows nothing.

### tray

```lua
tray = { type = "tray", icon = "\u{E76C}" },
```

`icon` is the toggle that unfolds the tray. It starts folded. Click an icon for its app, right click for its menu.

While a tray module is shown, the bar receives every app's tray icons and forwards them to the taskbar, so the
taskbar still has them after the bar exits.

### counter

Any Windows performance counter.

```lua
gpu = {
    type = "counter",
    counter = "\\GPU Engine(*engtype_3D)\\Utilization Percentage",
    combine = "sum",
    format = "{value:.0f}%",
    icon = "\u{E7F4}",
    interval = 2,
},
```

- `counter` is the English counter path. `typeperf -q` lists them, `typeperf -qx "PhysicalDisk"` lists instances.
  Lua strings need doubled backslashes.
- A `*` instance matches all instances, and `combine` (`sum`, `max` or `average`) merges them. It's required even
  without a wildcard.
- `{value}` is the number. `{rate}` formats it as bytes per second, for byte counters
  (`\\PhysicalDisk(_Total)\\Disk Bytes/sec`).
- A path that doesn't exist on the machine stops the config from loading.

### group

Folds other modules behind an icon that unfolds them inline on click.

```lua
more = { type = "group", icon = "\u{E712}", modules = { "gpu", "disk" } },
```

Members are defined under `modules` as usual, but appear in no section list. Groups can't nest. A group starts
folded.

### custom

Runs your own command and shows its output. See [Custom modules](custom-modules.md).

### GlazeWM modules

These need `window_manager = "glazewm"`.

| Module | Shows |
| --- | --- |
| `{ type = "workspaces" }` | One item per workspace on the monitor (`.focused`, `.occupied`, `.empty`). Click to switch. Shows why when GlazeWM is disconnected. |
| `{ type = "windows" }` | One icon per window on the current workspace (`.focused`). Click to focus. |
| `{ type = "binding_mode" }` | Each active binding mode's name. Nothing when none is active. |
| `{ type = "tiling_direction", horizontal = "\u{E8AB}", vertical = "\u{E8CB}" }` | The next tiling direction, as one of the two icons. Click to flip it. |

## Icons

The starter configs use Segoe MDL2 Assets, which ships with Windows (Segoe Fluent Icons on Windows 11 has the same
codes). Look up codes in `charmap` and write `U+E823` as `"\u{E823}"`. Any installed font works, Nerd Fonts included.
Set the font on `.icon` in `style.css`.
