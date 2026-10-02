# Custom modules

A `custom` module runs a command and shows what it prints.

```lua
weather = {
    type = "custom",
    command = { "C:\\Windows\\System32\\curl.exe", "--silent", "--fail", "https://wttr.in/?format=%c%t" },
    run = "interval",
    timeout = 10,
    output = "text",
    classes = {},
    format = "{text}",
    icon = "",
    interval = 900,
},
```

| Key | |
| --- | --- |
| `command` | Program and arguments, under the same rules as an [action](configuration.md#actions): an `.exe`, no shell. |
| `run` | `interval` runs it every `interval` seconds. `continuous` keeps it running (see below). |
| `timeout` | Whole seconds, up to 86400, before an interval run is killed, along with everything it started. Not allowed with `continuous`. |
| `output` | `text` shows the first line. `json` reads an object (see below). |
| `classes` | Classes the JSON output may set. `{}` for none. |
| `format` | `{text}` is the output. |
| `icon`, `interval` | As for other modules. |

`tooltip` and click actions are optional, as on other modules.

Name `curl.exe` by its full path. Git and MSYS2 put their own builds earlier on PATH, and those behave differently.

## PowerShell scripts

```lua
command = {
    "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
    "-NoProfile", "-ExecutionPolicy", "Bypass",
    "-File", "C:\\Users\\you\\scripts\\uptime.ps1",
},
```

- `-NoProfile` keeps your profile's output and startup time out of the module.
- `-ExecutionPolicy Bypass` is needed because Windows PowerShell blocks scripts by default. `pwsh.exe` works the same
  way and starts faster.
- Use absolute paths inside the script too, since the working directory isn't defined.
- `.bat` and `.cmd` files aren't accepted directly. Run them through `cmd.exe /c` if you have to.

For non-ASCII output, have the script write UTF-8:

```powershell
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false
```

## JSON output

With `output = "json"`, the command prints one object:

```json
{"text": "42 GB", "tooltip": "C: has 42 GB free\nof 476 GB", "alert": false}
```

| Field | |
| --- | --- |
| `text` | Required. Fills `{text}`. |
| `tooltip` | Optional. May span lines. |
| `alert` | Optional. `true` draws the module in its `.alert` color. |
| `class` | Optional. One of the module's `classes`, drawn in that class's color. |

Other fields are errors, and so is `alert: true` together with a `class`.

```powershell
$disk = Get-PSDrive C
$free = [math]::Round($disk.Free / 1GB)
$total = [math]::Round(($disk.Free + $disk.Used) / 1GB)
[pscustomobject]@{
    text = "$free GB"
    tooltip = "C: has $free GB free`nof $total GB"
    alert = $free -lt 20
} | ConvertTo-Json -Compress
```

### Classes

List the classes in the module, then style them:

```lua
classes = { "rain", "sun" },
```

```css
#weather.rain, #weather.rain .icon {
    color: #60a5fa;
}

#weather.sun, #weather.sun .icon {
    color: #facc15;
}
```

Classes only set color. Printing a class the list doesn't have fails the run, and styling one it doesn't have is a
style error.

## Continuous commands

With `run = "continuous"`, the command starts once and stays running. Each line it prints replaces the module's
value (one JSON object per line with `output = "json"`). If it exits, the module shows the exit code as an alert and
restarts it after `interval` seconds. It's killed with the bar.

```powershell
while ($true) {
    $reply = Test-Connection 1.1.1.1 -Count 1 -ErrorAction SilentlyContinue
    if ($reply) { "$($reply.ResponseTime) ms" } else { "no reply" }
    Start-Sleep -Seconds 5
}
```

```lua
ping = {
    type = "custom",
    command = {
        "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
        "-NoProfile", "-ExecutionPolicy", "Bypass",
        "-File", "C:\\Users\\you\\scripts\\ping.ps1",
    },
    run = "continuous",
    output = "text",
    classes = {},
    format = "{text}",
    icon = "\u{E701}",
    interval = 10,
},
```

## Menus

Any action can open a menu. `command` prints the entries, and picking one runs `on_select` with the entry's `id`
appended as the last argument.

```lua
on_right_click = {
    menu = {
        command = {
            "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
            "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", "C:\\Users\\you\\scripts\\folders.ps1",
        },
        timeout = 5,
        on_select = { "C:\\Windows\\explorer.exe" },
    },
},
```

The command runs each time the menu opens and prints:

```json
{"items": [
    {"id": "C:\\Users\\you\\Documents", "text": "Documents"},
    {"separator": true},
    {"id": "C:\\Users\\you\\dev", "text": "Code", "tooltip": "Projects", "active": true}
]}
```

| Field | |
| --- | --- |
| `id` | Required. Passed to `on_select`. Can't be empty or start with `-` or `/`. |
| `text` | Required. The row's label. |
| `active` | Optional. Styles the row with `menu .item.active`. |
| `tooltip` | Optional. Shown beside the menu while hovering the row. |

`{"separator": true}` adds a gap. A menu holds up to 50 entries.

The script that produced that output:

```powershell
$items = @(
    @{ id = "C:\Users\you\Documents"; text = "Documents" }
    @{ separator = $true }
    @{ id = "C:\Users\you\dev"; text = "Code"; tooltip = "Projects"; active = $true }
)
@{ items = $items } | ConvertTo-Json -Compress
```

Because the id comes from command output, `on_select` can't be `cmd.exe`, and PowerShell there must use `-File`,
never `-Command`. Otherwise an id could be read as shell syntax. A script reads the id from `$args[0]`.

## Errors

A failed run shows a short reason in the module's alert color, and its tooltip shows the full detail. The log has a
line for each failure.

| Message | Cause |
| --- | --- |
| `did not finish within 10s` | Hit `timeout`. |
| `exited with code N` | The command failed. Run it by hand to see why. |
| `printed invalid JSON`, `printed a JSON field other than ...` | Output isn't a single object with the fields above. |
| `printed a JSON class the module's classes do not list` | The `class` isn't in `classes`. |
| `... has control character ...` | Tabs, escape codes and other control characters are rejected. Only tooltips may contain line breaks. |
| `printed more than ... bytes` | Output exceeded 64 KiB. |
