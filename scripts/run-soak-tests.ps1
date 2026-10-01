<#
.SYNOPSIS
Soak tests the running bar. Fails on a crash, a leak or a hang.

.DESCRIPTION
Each scenario runs build\debug\intarsia.exe with a generated config against build\debug\glazewm.exe, a test IPC server
(tests/support/GlazeWmServer.cpp) on a free port, and samples the bar every second: memory, handles, GDI and USER
objects, threads, CPU, and whether its windows still answer messages. A scenario fails when the bar exits, stops
answering, logs a fatal error, grows past the thresholds after warming up, or does not exit cleanly on WM_CLOSE. Once
warmed up it also fails when a monitor lacks exactly one bar along its top edge.

The bar's tray always runs under a class of its own (--tray-class), so no scenario takes the desktop's tray icons. The
tray scenario feeds it from build\debug\tray_sender.exe (tests/soak/TraySender.cpp), restarted every 10 s so all
its owner windows die at once, while the script folds and unfolds the tray and right clicks across its icons, and folds
and unfolds a group of modules.

The retitle scenario runs the windows backend instead, whose titles come only from the foreground window, and starts
build\debug\window_sender.exe (tests/soak/WindowSender.cpp), which takes the foreground and retitles itself about 60
times a second. Leave it in the foreground for the whole run: it fails once another window takes it.

Run it from an interactive desktop session: the bar needs a desktop to draw on.
#>
param(
    [ValidateSet('idle', 'normal', 'flap', 'reload', 'tray', 'retitle')]
    [string[]] $Scenario = @('idle', 'normal', 'flap', 'reload', 'tray', 'retitle'),
    [int] $Seconds = 60,
    # The bar build under test. The test server only builds with the debug preset's tests.
    [ValidateSet('debug', 'release')]
    [string] $Preset = 'debug',
    # Each scenario's config, style and logs go in a directory of its own under this one.
    [string] $LogRoot = [System.IO.Path]::GetTempPath(),
    [switch] $NoBuild
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot  = Split-Path -Parent $PSScriptRoot
$barExe    = Join-Path $repoRoot "build\$Preset\intarsia.exe"
$serverExe = Join-Path $repoRoot 'build\debug\glazewm.exe'
$senderExe = Join-Path $repoRoot 'build\debug\tray_sender.exe'
$retitlerExe = Join-Path $repoRoot 'build\debug\window_sender.exe'
$trayClass = 'IntarsiaSoakTray'
# The tray sender is restarted this often, and the tray's toggle clicked this often.
$senderRestartSeconds = 10
$toggleEverySeconds   = 2

# Growth allowed between the warmed-up baseline and the end of a scenario, or, in a run of $longRunSeconds or more,
# per hour of the run's second half.
$longRunSeconds     = 600
$maxPrivateGrowthMB = 16
$maxHandleGrowth    = 64
$maxGuiObjectGrowth = 32
$maxThreadGrowth    = 8
$warmupSeconds      = 10
# The idle scenario ticks a clock, cpu and memory each second and gets a GlazeWM event a second, and runs no commands.
$maxIdleCpuPercent = 3
$barThicknessDips  = 40
# The window sender aims at 60. Release drew them at about 3.3% of a core, each a title read and a redraw.
$minRetitlesPerSecond = 40
$maxRetitleCpuPercent = 6

Add-Type -Namespace Soak -Name Native -MemberDefinition @'
[DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flags);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string className, string title);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
[DllImport("user32.dll")] public static extern IntPtr SendMessageTimeoutW(IntPtr window, uint message, IntPtr wParam, IntPtr lParam, uint flags, uint timeoutMs, out IntPtr result);
[DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out RECT rect);
[DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }

[DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
[DllImport("user32.dll", SetLastError = true)] static extern bool GetWindowRect(IntPtr window, out RECT rect);
[DllImport("user32.dll", SetLastError = true)] static extern bool EnumDisplayMonitors(IntPtr dc, IntPtr clip, MonitorEnumProc callback, IntPtr data);
[DllImport("user32.dll", SetLastError = true)] static extern bool GetMonitorInfoW(IntPtr monitor, ref MONITORINFO info);
[DllImport("shcore.dll")] static extern int GetDpiForMonitor(IntPtr monitor, int type, out uint dpiX, out uint dpiY);
delegate bool MonitorEnumProc(IntPtr monitor, IntPtr dc, ref RECT rect, IntPtr data);
[StructLayout(LayoutKind.Sequential)] struct MONITORINFO { public int Size; public RECT Monitor; public RECT Work; public uint Flags; }
public struct MonitorArea { public RECT Bounds; public RECT Work; public uint Dpi; }

// Both answer in physical pixels, as the per-monitor aware bar sees them, whatever the script host's awareness.
static readonly IntPtr PerMonitorAwareV2 = new IntPtr(-4);

public static RECT WindowRect(IntPtr window) {
    IntPtr previous = SetThreadDpiAwarenessContext(PerMonitorAwareV2);
    try {
        RECT rect;
        if (!GetWindowRect(window, out rect)) { throw new System.ComponentModel.Win32Exception(); }
        return rect;
    } finally {
        SetThreadDpiAwarenessContext(previous);
    }
}

public static MonitorArea[] Monitors() {
    IntPtr previous = SetThreadDpiAwarenessContext(PerMonitorAwareV2);
    try {
        var handles = new System.Collections.Generic.List<IntPtr>();
        MonitorEnumProc collect = (IntPtr monitor, IntPtr dc, ref RECT rect, IntPtr data) => { handles.Add(monitor); return true; };
        if (!EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, collect, IntPtr.Zero)) { throw new System.ComponentModel.Win32Exception(); }
        var monitors = new MonitorArea[handles.Count];
        for (int index = 0; index < handles.Count; index++) {
            var info = new MONITORINFO { Size = Marshal.SizeOf(typeof(MONITORINFO)) };
            if (!GetMonitorInfoW(handles[index], ref info)) { throw new System.ComponentModel.Win32Exception(); }
            uint dpiX, dpiY;
            // MDT_EFFECTIVE_DPI (0), the DPI the bar scales by.
            Marshal.ThrowExceptionForHR(GetDpiForMonitor(handles[index], 0, out dpiX, out dpiY));
            monitors[index] = new MonitorArea { Bounds = info.Monitor, Work = info.Work, Dpi = dpiX };
        }
        return monitors;
    } finally {
        SetThreadDpiAwarenessContext(previous);
    }
}
'@

function Get-FreePort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $port = $listener.LocalEndpoint.Port
    $listener.Stop()
    return $port
}

# The bar's top-level windows, found by class and owner.
function Get-BarWindows([int] $processId) {
    $windows = @()
    $window  = [IntPtr]::Zero
    while ($true) {
        # [NullString]: PowerShell turns $null into "" for a string parameter, which would match only untitled windows.
        $window = [Soak.Native]::FindWindowExW([IntPtr]::Zero, $window, 'IntarsiaBar', [NullString]::Value)
        if ($window -eq [IntPtr]::Zero) {
            break
        }
        [uint32] $owner = 0
        [void][Soak.Native]::GetWindowThreadProcessId($window, [ref] $owner)
        if ($owner -eq $processId) {
            $windows += $window
        }
    }
    return $windows
}

# 'answering' when every bar window answers WM_NULL within a second, 'hung' when one does not (a stuck UI thread), or
# 'none' while the bar has no windows yet.
function Get-Responsiveness([int] $processId) {
    $windows = @(Get-BarWindows $processId)
    if ($windows.Count -eq 0) {
        return 'none'
    }
    foreach ($window in $windows) {
        [IntPtr] $result = [IntPtr]::Zero
        # SMTO_ABORTIFHUNG (2)
        if ([Soak.Native]::SendMessageTimeoutW($window, 0, [IntPtr]::Zero, [IntPtr]::Zero, 2, 1000, [ref] $result) -eq [IntPtr]::Zero) {
            return 'hung'
        }
    }
    return 'answering'
}

# Posts what the bar takes as a click or cursor move at a client point in pixels.
function Send-BarMouse([IntPtr] $window, [uint32] $message, [int] $x, [int] $y) {
    $point = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
    [void][Soak.Native]::PostMessageW($window, $message, [IntPtr]::Zero, $point)
}

# Each bar window's client width, its middle row and its DPI scale, all in pixels.
function Get-BarRows([int] $processId) {
    foreach ($window in @(Get-BarWindows $processId)) {
        $rect = [Soak.Native+RECT]::new()
        if ([Soak.Native]::GetClientRect($window, [ref] $rect)) {
            [pscustomobject]@{
                Window = $window
                Right  = $rect.Right
                Y      = [int] (($rect.Bottom - $rect.Top) / 2)
                Scale  = [Soak.Native]::GetDpiForWindow($window) / 96.0
            }
        }
    }
}

$WM_MOUSEMOVE = 0x0200
$WM_LBUTTONUP = 0x0202
$WM_RBUTTONUP = 0x0205

# The tray is the last module, so its toggle sits at the bar's right end, 26 DIPs in from the edge with the soak
# style (section margin and padding, the module's padding, the toggle's margin and its padding). Every few seconds the
# toggle is clicked to fold or unfold the tray, and every second the cursor moves over and right clicks a spot across the
# icons it unfolds, which reach the sender's owner windows.
function Send-TrayInput([int] $processId, [int] $second) {
    foreach ($row in @(Get-BarRows $processId)) {
        $toggle = $row.Right - [int] (26 * $row.Scale)
        if ($second % $toggleEverySeconds -eq 0) {
            Send-BarMouse $row.Window $WM_LBUTTONUP $toggle $row.Y
        }
        $icon = $toggle - [int] ((30 + ($second * 37) % 300) * $row.Scale)
        Send-BarMouse $row.Window $WM_MOUSEMOVE $icon $row.Y
        Send-BarMouse $row.Window $WM_RBUTTONUP $icon $row.Y
    }
}

# The stats group is the first module on the left, so its toggle sits 18 DIPs in from the bar's left edge with the
# soak style (section margin and padding, the module's padding, then half the glyph). Every few seconds the toggle is
# clicked to fold or unfold it, and the cursor moves across the members it unfolds.
function Send-GroupInput([int] $processId, [int] $second) {
    foreach ($row in @(Get-BarRows $processId)) {
        $toggle = [int] (18 * $row.Scale)
        if ($second % $toggleEverySeconds -eq 1) {
            Send-BarMouse $row.Window $WM_LBUTTONUP $toggle $row.Y
        }
        Send-BarMouse $row.Window $WM_MOUSEMOVE ($toggle + [int] ((20 + ($second * 23) % 100) * $row.Scale)) $row.Y
    }
}

function Format-Rect([Soak.Native+RECT] $rect) {
    return "($($rect.Left), $($rect.Top), $($rect.Right), $($rect.Bottom))"
}

# How the bars sit wrong: each monitor needs exactly one, along its top edge at full width and the config's thickness at
# that monitor's DPI. Prints each monitor's work area too, which shows whether the session's shell reserved the strip
# (a shell that ignores the bar leaves the work area whole, which is not a failure).
function Test-BarGeometry([int] $processId) {
    $failures = @()
    $monitors = @([Soak.Native]::Monitors())
    $bars     = @(Get-BarWindows $processId | ForEach-Object { [Soak.Native]::WindowRect($_) })
    if ($bars.Count -ne $monitors.Count) {
        $failures += "had $($bars.Count) bar windows for $($monitors.Count) monitors"
    }
    foreach ($monitor in $monitors) {
        $bounds = $monitor.Bounds
        # lround, as the bar rounds DIPs to pixels.
        $thickness = [int] [math]::Round($barThicknessDips * $monitor.Dpi / 96.0, [MidpointRounding]::AwayFromZero)
        $expected  = [Soak.Native+RECT] @{ Left = $bounds.Left; Top = $bounds.Top; Right = $bounds.Right; Bottom = $bounds.Top + $thickness }
        $matching  = @($bars | Where-Object { (Format-Rect $_) -eq (Format-Rect $expected) })
        $reserved  = $monitor.Work.Top -ge $expected.Bottom
        Write-Host "monitor $(Format-Rect $bounds) at $($monitor.Dpi) dpi: work area $(Format-Rect $monitor.Work), strip reserved: $reserved"
        if ($matching.Count -ne 1) {
            $failures += "had $($matching.Count) bars at $(Format-Rect $expected) on the monitor at $(Format-Rect $bounds), bars are at $(($bars | ForEach-Object { Format-Rect $_ }) -join ' ')"
        }
    }
    return $failures
}

# The least squares slope of one sampled property against time, scaled to an hour.
function Get-GrowthPerHour([object[]] $samples, [string] $property) {
    $meanTime  = ($samples | Measure-Object -Property Seconds -Average).Average
    $meanValue = ($samples | Measure-Object -Property $property -Average).Average
    $covariance = 0.0
    $variance   = 0.0
    foreach ($sample in $samples) {
        $covariance += ($sample.Seconds - $meanTime) * ($sample.$property - $meanValue)
        $variance   += [math]::Pow($sample.Seconds - $meanTime, 2)
    }
    return $covariance / $variance * 3600
}

function Get-Sample([System.Diagnostics.Process] $process, [System.Diagnostics.Stopwatch] $clock) {
    $process.Refresh()
    return [pscustomobject]@{
        Seconds   = $clock.Elapsed.TotalSeconds
        PrivateMB = [math]::Round($process.PrivateMemorySize64 / 1MB, 1)
        Handles   = $process.HandleCount
        Gdi       = [Soak.Native]::GetGuiResources($process.Handle, 0)
        User      = [Soak.Native]::GetGuiResources($process.Handle, 1)
        Threads   = $process.Threads.Count
        Cpu       = $process.TotalProcessorTime
    }
}

# A custom module running command every second. Each argument is written into a Lua string as it is.
function Format-CustomModule([string[]] $command, [int] $timeout, [string] $output) {
    $arguments = ($command | ForEach-Object { "`"$_`"" }) -join ', '
    return "{ type = `"custom`", command = { $arguments }, run = `"interval`", timeout = $timeout, output = `"$output`", classes = {}, format = `"{text}`", icon = `"`", interval = 1 }"
}

# A config with every GlazeWM module and a clock, cpu and memory ticking each second (cpu and memory in the stats group,
# first on the left, which starts folded), plus the named optional modules:
# the custom endless (prints past the output cap), hang (outlives its timeout), fail (exits nonzero) and json, and the
# tray, last so its toggle sits at the bar's right end whatever it unfolds.
function New-SoakConfig([string] $path, [int] $revision, [string[]] $optional) {
    $cmd  = 'C:\\Windows\\System32\\cmd.exe'
    $ping = 'C:\\Windows\\System32\\PING.EXE'
    $powershell = 'C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe'
    $optionalModules = [ordered]@{
        # Prints until the output cap kills it, every second.
        endless = Format-CustomModule @($cmd, '/c', 'for /l %i in () do @echo xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx') 5 'text'
        # Outlives its timeout, with a child of its own, every second.
        hang    = Format-CustomModule @($cmd, '/c', "$ping -n 30 127.0.0.1") 1 'text'
        fail    = Format-CustomModule @($cmd, '/c', 'exit 3') 5 'text'
        # A process slow to start, printing JSON. cmd.exe would keep the backslashes of escaped quotes.
        json    = Format-CustomModule @($powershell, '-NoProfile', '-Command', '''{\"text\":\"ok\",\"tooltip\":\"line\",\"alert\":true}''') 5 'json'
        tray    = '{ type = "tray", icon = ">" }'
    }
    $shown       = @($optionalModules.Keys | Where-Object { $_ -in $optional })
    $custom      = @($shown | Where-Object { $_ -ne 'tray' })
    $trayList    = @($shown | Where-Object { $_ -eq 'tray' })
    $rightList   = (@('"tiling"') + ($custom | ForEach-Object { "`"$_`"" }) + ($trayList | ForEach-Object { "`"$_`"" })) -join ', '
    $definitions = ($shown | ForEach-Object { "        $_ = $($optionalModules[$_])," }) -join "`n"
    $config = @"
-- soak revision $revision
return {
    bar = { edge = "top", layer = "top", space = "reserve", thickness = $barThicknessDips },
    window_manager = "glazewm",
    modules_left = { "stats", "workspaces", "mode", "apps", "title" },
    modules_center = { "clock" },
    modules_right = { $rightList },
    layouts = {},
    modules = {
        workspaces = { type = "workspaces" },
        mode = { type = "binding_mode" },
        apps = { type = "windows" },
        title = { type = "window", max_length = 60, tooltip = "{title}" },
        tiling = { type = "tiling_direction", horizontal = "H", vertical = "V" },
        clock = { type = "clock", format = "{time:%H:%M:%S}", icon = "", interval = 1,
                  on_scroll_up = { window_manager = "focus --next-workspace" } },
        stats = { type = "group", icon = ">", modules = { "cpu", "memory" } },
        cpu = { type = "cpu", format = "{usage}%", icon = "", interval = 1 },
        memory = { type = "memory", format = "{used:.1f}", icon = "", interval = 1 },
$definitions
    },
}
"@
    # WriteAllText so an editor-style save lands in one write the watcher sees.
    [System.IO.File]::WriteAllText($path, $config)
}

# The idle scenario's modules without a window manager: the foreground window's title, a clock, and cpu and memory in
# the stats group.
function New-RetitleConfig([string] $path) {
    $config = @"
return {
    bar = { edge = "top", layer = "top", space = "reserve", thickness = $barThicknessDips },
    window_manager = "windows",
    modules_left = { "stats", "title" },
    modules_center = { "clock" },
    modules_right = {},
    layouts = {},
    modules = {
        title = { type = "window", max_length = 60, tooltip = "{title}" },
        clock = { type = "clock", format = "{time:%H:%M:%S}", icon = "", interval = 1 },
        stats = { type = "group", icon = ">", modules = { "cpu", "memory" } },
        cpu = { type = "cpu", format = "{usage}%", icon = "", interval = 1 },
        memory = { type = "memory", format = "{used:.1f}", icon = "", interval = 1 },
    },
}
"@
    [System.IO.File]::WriteAllText($path, $config)
}

# A style that draws every box the bar has: sections, module boxes with hover, items in every state, a border on one
# side, inset shadows, and a tray that animates as it unfolds when the config shows one (an #id naming no module stops
# the bar).
function New-SoakStyle([string] $path, [string[]] $optional) {
    $style = @'
:root { --foreground: #e0def4; --border: rgba(110, 106, 134, 0.25); }
* { font-family: "Segoe UI"; font-size: 13px; color: var(--foreground); }
section { margin: 4px; padding: 3px; background: rgba(25, 23, 36, 0.9); border: 1px solid var(--border); border-radius: 12px; box-shadow: inset 0 1px 2px rgba(255, 255, 255, 0.18); }
tooltip { padding: 6px; background: #1f1d2e; border: 1px solid var(--border); border-radius: 10px; }
.module { padding: 0 8px; border-radius: 8px; }
.module:hover { background: rgba(64, 61, 82, 0.8); }
.icon { font-size: 16px; margin-right: 6px; color: #c4a7e7; }
.alert { color: #eb6f92; }
.item { padding: 2px 6px; margin: 0 1px; border-radius: 8px; }
.item.focused { color: #191724; background: #9ccfd8; font-weight: bold; box-shadow: inset 0 1px 1px rgba(255, 255, 255, 0.35), inset 0 -1px 1px rgba(0, 0, 0, 0.25); }
.item.empty { color: #908caa; }
#stats { border-right: 1px solid var(--border); transition: width 300ms ease-out; }
#stats .icon { transition: transform 200ms ease; }
#stats.expanded .icon { transform: rotate(90deg); }
'@
    if ('tray' -in $optional) {
        $style += "`n" + @'
#tray { transition: width 300ms ease-out; }
#tray .icon { transition: transform 200ms ease; }
#tray.expanded .icon { transform: rotate(180deg); }
#tray .item.toggle { padding: 2px 10px; }
'@
    }
    [System.IO.File]::WriteAllText($path, $style)
}

function Invoke-Scenario([string] $name) {
    $work = Join-Path $LogRoot "intarsia-soak-$name-$PID"
    New-Item -ItemType Directory -Force $work | Out-Null
    $configPath = Join-Path $work 'config.lua'
    # idle has no custom modules, so its CPU is the bar's own cost of ticking and following GlazeWM.
    $optional   = if ($name -in @('idle', 'retitle')) { @() } else { @('endless', 'hang', 'fail', 'json') }
    if ($name -eq 'tray') {
        $optional += 'tray'
    }
    if ($name -eq 'retitle') {
        New-RetitleConfig $configPath
    } else {
        New-SoakConfig $configPath 0 $optional
    }
    New-SoakStyle (Join-Path $work 'style.css') $optional
    $port       = Get-FreePort
    $serverName = if ($name -eq 'flap') { 'flap' } else { 'normal' }
    $barLog     = Join-Path $work 'intarsia.log'

    $server = $null
    $bar    = $null
    $sender = $null
    $senderStarts = 0
    $retitler = $null

    $failures   = [System.Collections.Generic.List[string]]::new()
    $samples    = [System.Collections.Generic.List[object]]::new()
    $cpuPercent = $null
    $clock      = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        $server = Start-Process $serverExe -ArgumentList '--port', $port, '--scenario', $serverName -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput (Join-Path $work 'server.out') -RedirectStandardError (Join-Path $work 'server.err')
        # --log keeps the bar off the default log, which a running bar holds open.
        $bar = Start-Process $barExe -ArgumentList '--config', "`"$configPath`"", '--log', "`"$(Join-Path $work 'intarsia-file.log')`"", '--glazewm-port', $port, '--tray-class', $trayClass -PassThru -RedirectStandardError $barLog
        for ($second = 1; $second -le $Seconds; $second++) {
            Start-Sleep -Seconds 1
            if ($bar.HasExited) {
                $failures.Add("exited early with code $($bar.ExitCode)")
                break
            }
            # Quoted in the result, since a CI runner's log directory is gone once the job ends.
            $fatal = Select-String -Path $barLog -Pattern 'event=fatal' | Select-Object -First 1
            if ($fatal) {
                $failures.Add("logged a fatal error: $($fatal.Line)")
                break
            }
            $responsiveness = Get-Responsiveness $bar.Id
            if ($responsiveness -eq 'hung') {
                $failures.Add("stopped answering window messages at ${second}s")
                break
            }
            # A bar rebuilding for a reload has no windows for a moment, but never for long.
            if ($responsiveness -eq 'none' -and $second -gt 5 -and $name -ne 'reload') {
                $failures.Add("had no bar windows at ${second}s")
                break
            }
            # Not in reload, whose twenty rebuilds a second leave any moment's set of bars half built.
            if ($second -eq $warmupSeconds -and $name -ne 'reload') {
                $geometryFailures = @(Test-BarGeometry $bar.Id)
                if ($geometryFailures.Count -gt 0) {
                    $failures.AddRange([string[]] $geometryFailures)
                    break
                }
            }
            if ($name -eq 'reload') {
                # Twenty saves a second: every settled save rebuilds every bar.
                for ($save = 0; $save -lt 20; $save++) {
                    New-SoakConfig $configPath ($second * 20 + $save) $optional
                    Start-Sleep -Milliseconds 50
                }
            }
            if ($name -eq 'tray') {
                if ($null -ne $sender -and $sender.HasExited) {
                    $failures.Add("tray sender exited on its own with code $($sender.ExitCode) at ${second}s (2: the tray stopped answering, 3: its window went away)")
                    break
                }
                # Every owner window of the last sender dies with it, without deleting its icons.
                if ($null -eq $sender -or $second % $senderRestartSeconds -eq 0) {
                    if ($null -ne $sender) {
                        Stop-Process -Id $sender.Id -Force
                        $sender.WaitForExit()
                    }
                    $senderStarts++
                    $sender = Start-Process $senderExe -ArgumentList '--class', $trayClass -PassThru -WindowStyle Hidden `
                        -RedirectStandardOutput (Join-Path $work "sender-$senderStarts.out") -RedirectStandardError (Join-Path $work "sender-$senderStarts.err")
                }
                Send-TrayInput $bar.Id $second
                Send-GroupInput $bar.Id $second
            }
            if ($name -eq 'retitle') {
                # Started once the bar is up, from this console, which may hand it the foreground.
                if ($null -eq $retitler) {
                    $retitler = Start-Process $retitlerExe -PassThru `
                        -RedirectStandardOutput (Join-Path $work 'retitler.out') -RedirectStandardError (Join-Path $work 'retitler.err')
                } elseif ($retitler.HasExited) {
                    $failures.Add("window sender exited with code $($retitler.ExitCode) at ${second}s (2: it could not take the foreground or lost it): $(Get-Content -Raw (Join-Path $work 'retitler.err'))")
                    break
                }
            }
            $samples.Add((Get-Sample $bar $clock))
        }

        if ($failures.Count -eq 0 -and $samples.Count -le $warmupSeconds + 5) {
            $failures.Add("ran only $($samples.Count) s, too short to judge growth after the $warmupSeconds s warmup")
        }
        if ($failures.Count -eq 0) {
            $baseline = $samples[$warmupSeconds]
            $final    = $samples[$samples.Count - 1]
            if ($Seconds -ge $longRunSeconds) {
                # The first half's growth is caches filling, and a leak keeps growing through the second.
                $settled = $samples.GetRange([int] ($samples.Count / 2), $samples.Count - [int] ($samples.Count / 2)).ToArray()
                $limits  = [ordered]@{ PrivateMB = $maxPrivateGrowthMB; Handles = $maxHandleGrowth; Gdi = $maxGuiObjectGrowth; User = $maxGuiObjectGrowth; Threads = $maxThreadGrowth }
                foreach ($property in $limits.Keys) {
                    $perHour = Get-GrowthPerHour $settled $property
                    if ($perHour -gt $limits[$property]) { $failures.Add("$property grew $([math]::Round($perHour, 1)) an hour over the second half, past $($limits[$property])") }
                }
            } else {
                if ($final.PrivateMB - $baseline.PrivateMB -gt $maxPrivateGrowthMB) { $failures.Add("private memory grew $($baseline.PrivateMB) to $($final.PrivateMB) MB") }
                if ($final.Handles - $baseline.Handles -gt $maxHandleGrowth) { $failures.Add("handles grew $($baseline.Handles) to $($final.Handles)") }
                if ($final.Gdi - $baseline.Gdi -gt $maxGuiObjectGrowth) { $failures.Add("GDI objects grew $($baseline.Gdi) to $($final.Gdi)") }
                if ($final.User - $baseline.User -gt $maxGuiObjectGrowth) { $failures.Add("USER objects grew $($baseline.User) to $($final.User)") }
                if ($final.Threads - $baseline.Threads -gt $maxThreadGrowth) { $failures.Add("threads grew $($baseline.Threads) to $($final.Threads)") }
            }
            $cpuPercent = ($final.Cpu - $baseline.Cpu).TotalMilliseconds / (($samples.Count - 1 - $warmupSeconds) * 10.0)
            if ($name -eq 'idle' -and $cpuPercent -gt $maxIdleCpuPercent) { $failures.Add("used $([math]::Round($cpuPercent, 1))% CPU while nearly idle") }
            if ($name -eq 'retitle' -and $cpuPercent -gt $maxRetitleCpuPercent) { $failures.Add("used $([math]::Round($cpuPercent, 1))% CPU following retitles") }
        }
        # A sender that retitles too little passes every other check without loading the bar. It reports every 5 s.
        if ($name -eq 'retitle' -and $failures.Count -eq 0) {
            $report   = Select-String -Path (Join-Path $work 'retitler.out') -Pattern '^retitles=(\d+)$' | Select-Object -Last 1
            $retitles = if ($report) { [int] $report.Matches[0].Groups[1].Value } else { 0 }
            $expected = ($samples.Count - 6) * $minRetitlesPerSecond
            if ($retitles -lt $expected) { $failures.Add("the window sender reported $retitles retitles, under the $expected of $minRetitlesPerSecond a second") }
        }
    } finally {
        if ($null -ne $sender -and -not $sender.HasExited) {
            Stop-Process -Id $sender.Id -Force
        }
        if ($null -ne $retitler -and -not $retitler.HasExited) {
            Stop-Process -Id $retitler.Id -Force
        }
        if ($null -ne $bar -and -not $bar.HasExited) {
            # taskkill gives up on a window a rebuild destroys under it, before it reaches the rest.
            if ($name -eq 'reload') {
                Start-Sleep -Seconds 1
            }
            # WM_CLOSE through taskkill without /F, as a user would stop it: every bar must release its strip and exit.
            taskkill /PID $bar.Id | Out-Null
            if (-not $bar.WaitForExit(10000)) {
                $failures.Add('did not exit within 10 s of WM_CLOSE')
                Stop-Process -Id $bar.Id -Force
            } elseif ($bar.ExitCode -ne 0) {
                $failures.Add("exited with code $($bar.ExitCode) on WM_CLOSE, expected 0")
            }
        }
        if ($null -ne $server) {
            Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
        }
    }

    $last = if ($samples.Count -gt 0) { $samples[$samples.Count - 1] } else { $null }
    return [pscustomobject]@{
        Scenario = $name
        Result   = if ($failures.Count -eq 0) { 'pass' } else { 'FAIL' }
        CpuPercent = if ($null -ne $cpuPercent) { [math]::Round($cpuPercent, 2) } else { '' }
        PrivateMB = if ($last) { $last.PrivateMB } else { '' }
        Handles  = if ($last) { $last.Handles } else { '' }
        Threads  = if ($last) { $last.Threads } else { '' }
        Detail   = if ($failures.Count -eq 0) { $work } else { ($failures -join '; ') + " (logs in $work)" }
    }
}

if (-not $NoBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Preset debug
    if ($Preset -eq 'release') {
        & (Join-Path $PSScriptRoot 'build.ps1') -Preset release
    }
}
foreach ($exe in @($barExe, $serverExe, $senderExe, $retitlerExe)) {
    if (-not (Test-Path $exe)) {
        throw "$exe not found, build the debug preset first"
    }
}

$results = foreach ($name in $Scenario) {
    Write-Host "running $name for $Seconds s"
    Invoke-Scenario $name
}
$results | Format-Table -AutoSize | Out-String -Width 400 | Write-Host
if (@($results | Where-Object Result -ne 'pass').Count -gt 0) {
    throw 'soak scenarios failed'
}
