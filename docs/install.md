# Installing

intarsia runs on Windows 10 1903 or later. It has no dependencies.

## From a release

1. Download `intarsia-<version>.zip` from the Releases page and unzip it somewhere permanent, such as
   `%LOCALAPPDATA%\Programs`.
2. Copy a starter config to `%USERPROFILE%\.config\intarsia\`:
   - `config\windows`: a plain bar that works on any machine.
   - `config\glazewm`: a bar for GlazeWM with workspaces. Its files comment every key.
3. Start `intarsia.exe`.

```powershell
$version = '0.2.0'
$app = "$env:LOCALAPPDATA\Programs\intarsia-$version"
Expand-Archive "$env:USERPROFILE\Downloads\intarsia-$version.zip" "$env:LOCALAPPDATA\Programs"
New-Item -ItemType Directory -Force "$env:USERPROFILE\.config\intarsia" | Out-Null
Copy-Item "$app\config\windows\*" "$env:USERPROFILE\.config\intarsia"
Start-Process "$app\intarsia.exe"
```

The bar reads `config.lua` for what it shows and `style.conf` for how it looks. Saving either one reloads the bar.

## From source

See [Build](../README.md#build) in the README. The executable is `build\release\intarsia.exe`, and the starter
configs are in the repo's `config` folder.

## Starting at login

Without a window manager, put a shortcut to `intarsia.exe` in `shell:startup`.

With GlazeWM, add it to `startup_commands` in `%USERPROFILE%\.glzr\glazewm\config.yaml`:

```yaml
general:
  startup_commands:
    - 'shell-exec %LOCALAPPDATA%/Programs/intarsia-0.2.0/intarsia.exe'
```

If GlazeWM runs elevated, the bar relaunches itself unelevated so tray icons still reach it.

## Stopping

```powershell
taskkill /IM intarsia.exe
```

Leave off `/F`, so each bar closes cleanly and gives its screen space back.

## Updating

Unzip the new version, point your shortcut or startup command at it, and restart the bar. Your config stays where it
is. Check `CHANGELOG.md` for changed settings first.

## Command line

| Option | Effect |
| --- | --- |
| `--config <path>` | Read another `config.lua`, and the `style.conf` beside it. |
| `--log <path>` | Log somewhere other than `%LOCALAPPDATA%\intarsia\intarsia.log`. |
| `--glazewm-port <port>` | Connect to GlazeWM on a port other than 6123. |

## Troubleshooting

**An error box appears.** The config or style has a mistake, and the message names the file and line. The bar is
strict: a missing key, an unknown key, a module listed but not defined, a module defined but not shown, or a property
a block doesn't take all stop it. A bad save while it runs also stops it, so fix the file and start it again.

**Nothing appears.** Check `Get-Process intarsia`, then the end of the log:

```powershell
Get-Content "$env:LOCALAPPDATA\intarsia\intarsia.log" -Tail 30
```

**Windows overlap the bar.** Set `space = "reserve"` under `bar`.

**Workspaces says it's disconnected.** GlazeWM isn't running, or runs as another user. The bar reconnects on its own
once GlazeWM is up.
