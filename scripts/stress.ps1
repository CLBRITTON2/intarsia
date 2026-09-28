<#
.SYNOPSIS
Stress tests the running bar. Fails on a crash, a leak or a hang.

.DESCRIPTION
Each scenario runs build\debug\intarsia.exe with a generated config against build\debug\glazewm.exe, a test IPC server
(tests/stress/GlazeWmServer.cpp) on a free port, and samples the bar every second: memory, handles, GDI and USER
objects, threads, CPU, and whether its windows still answer messages. A scenario fails when the bar exits, stops
answering, logs a fatal error, grows past the thresholds after warming up, or does not exit cleanly on WM_CLOSE.

The bar's tray always runs under a class of its own (--tray-class), so no scenario takes the desktop's tray icons. The
tray scenario floods it from build\debug\tray_sender.exe (tests/stress/TraySender.cpp), restarted every 10 s so all
its owner windows die at once, while the script folds and unfolds the tray and right clicks across its icons.

Run it from an interactive desktop session: the bar needs a desktop to draw on.
#>
param(
    [ValidateSet('idle', 'normal', 'flood', 'malformed', 'stall', 'flap', 'reject', 'reload', 'tray')]
    [string[]] $Scenario = @('idle', 'normal', 'flood', 'malformed', 'stall', 'flap', 'reject', 'reload', 'tray'),
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
$trayClass = 'IntarsiaStressTray'
# The tray sender is restarted this often, and the tray's toggle clicked this often.
$senderRestartSeconds = 10
$toggleEverySeconds   = 2

# Growth allowed between the warmed-up baseline and the end of a scenario.
$maxPrivateGrowthMB = 16
$maxHandleGrowth    = 64
$maxGuiObjectGrowth = 32
$maxThreadGrowth    = 8
$warmupSeconds      = 10
# The idle scenario ticks a clock, cpu and memory each second and gets a GlazeWM event a second, and runs no commands.
$maxIdleCpuPercent = 3

Add-Type -Namespace Stress -Name Native -MemberDefinition @'
[DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flags);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string className, string title);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
[DllImport("user32.dll")] public static extern IntPtr SendMessageTimeoutW(IntPtr window, uint message, IntPtr wParam, IntPtr lParam, uint flags, uint timeoutMs, out IntPtr result);
[DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out RECT rect);
[DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
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
        $window = [Stress.Native]::FindWindowExW([IntPtr]::Zero, $window, 'IntarsiaBar', [NullString]::Value)
        if ($window -eq [IntPtr]::Zero) {
            break
        }
        [uint32] $owner = 0
        [void][Stress.Native]::GetWindowThreadProcessId($window, [ref] $owner)
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
        if ([Stress.Native]::SendMessageTimeoutW($window, 0, [IntPtr]::Zero, [IntPtr]::Zero, 2, 1000, [ref] $result) -eq [IntPtr]::Zero) {
            return 'hung'
        }
    }
    return 'answering'
}

# Posts what the bar takes as a click or cursor move at a client point in pixels.
function Send-BarMouse([IntPtr] $window, [uint32] $message, [int] $x, [int] $y) {
    $point = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
    [void][Stress.Native]::PostMessageW($window, $message, [IntPtr]::Zero, $point)
}

# The tray is the last module, so its toggle sits at the bar's right end, 26 DIPs in from the edge with the stress
# style (section margin and padding, the module's padding, the toggle's margin and its padding). Every few seconds the
# toggle is clicked to fold or unfold the tray, and every second the cursor moves over and right clicks a spot across the
# icons it unfolds, which reach the sender's owner windows.
function Send-TrayInput([int] $processId, [int] $second) {
    $WM_MOUSEMOVE = 0x0200
    $WM_LBUTTONUP = 0x0202
    $WM_RBUTTONUP = 0x0205
    foreach ($window in @(Get-BarWindows $processId)) {
        $rect = [Stress.Native+RECT]::new()
        if (-not [Stress.Native]::GetClientRect($window, [ref] $rect)) {
            continue
        }
        $scale  = [Stress.Native]::GetDpiForWindow($window) / 96.0
        $y      = [int] (($rect.Bottom - $rect.Top) / 2)
        $toggle = $rect.Right - [int] (26 * $scale)
        if ($second % $toggleEverySeconds -eq 0) {
            Send-BarMouse $window $WM_LBUTTONUP $toggle $y
        }
        $icon = $toggle - [int] ((30 + ($second * 37) % 300) * $scale)
        Send-BarMouse $window $WM_MOUSEMOVE $icon $y
        Send-BarMouse $window $WM_RBUTTONUP $icon $y
    }
}

function Get-Sample([System.Diagnostics.Process] $process) {
    $process.Refresh()
    return [pscustomobject]@{
        PrivateMB = [math]::Round($process.PrivateMemorySize64 / 1MB, 1)
        Handles   = $process.HandleCount
        Gdi       = [Stress.Native]::GetGuiResources($process.Handle, 0)
        User      = [Stress.Native]::GetGuiResources($process.Handle, 1)
        Threads   = $process.Threads.Count
        Cpu       = $process.TotalProcessorTime
    }
}

# A config with every GlazeWM module and a clock, cpu and memory ticking each second, plus the named optional modules:
# the custom endless (prints past the output cap), hang (outlives its timeout), fail (exits nonzero) and json, and the
# tray, last so its toggle sits at the bar's right end whatever it unfolds.
function New-StressConfig([string] $path, [int] $revision, [string[]] $optional) {
    $cmd  = 'C:\\Windows\\System32\\cmd.exe'
    $ping = 'C:\\Windows\\System32\\PING.EXE'
    $powershell = 'C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe'
    $optionalModules = [ordered]@{
        # Prints until the output cap kills it, every second.
        endless = "{ type = `"custom`", command = { `"$cmd`", `"/c`", `"for /l %i in () do @echo xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx`" }, timeout = 5, output = `"text`", classes = {}, format = `"{text}`", icon = `"`", interval = 1 }"
        # Outlives its timeout, with a child of its own, every second.
        hang    = "{ type = `"custom`", command = { `"$cmd`", `"/c`", `"$ping -n 30 127.0.0.1`" }, timeout = 1, output = `"text`", classes = {}, format = `"{text}`", icon = `"`", interval = 1 }"
        fail    = "{ type = `"custom`", command = { `"$cmd`", `"/c`", `"exit 3`" }, timeout = 5, output = `"text`", classes = {}, format = `"{text}`", icon = `"`", interval = 1 }"
        # A process slow to start, printing JSON. cmd.exe would keep the backslashes of escaped quotes.
        json    = "{ type = `"custom`", command = { `"$powershell`", `"-NoProfile`", `"-Command`", `"'{\`"text\`":\`"ok\`",\`"tooltip\`":\`"line\`",\`"alert\`":true}'`" }, timeout = 5, output = `"json`", classes = {}, format = `"{text}`", icon = `"`", interval = 1 }"
        tray    = '{ type = "tray", icon = ">" }'
    }
    $shown       = @($optionalModules.Keys | Where-Object { $_ -in $optional })
    $custom      = @($shown | Where-Object { $_ -ne 'tray' })
    $trayList    = @($shown | Where-Object { $_ -eq 'tray' })
    $rightList   = (@('"tiling"') + ($custom | ForEach-Object { "`"$_`"" }) + @('"cpu"', '"memory"') + ($trayList | ForEach-Object { "`"$_`"" })) -join ', '
    $definitions = ($shown | ForEach-Object { "        $_ = $($optionalModules[$_])," }) -join "`n"
    $config = @"
-- stress revision $revision
return {
    bar = { edge = "top", layer = "top", space = "reserve", thickness = 40 },
    window_manager = "glazewm",
    modules_left = { "workspaces", "divider", "mode", "apps", "title" },
    modules_center = { "clock" },
    modules_right = { $rightList },
    layouts = {},
    modules = {
        workspaces = { type = "workspaces" },
        divider = { type = "divider" },
        mode = { type = "binding_mode" },
        apps = { type = "windows" },
        title = { type = "window", max_length = 60, tooltip = "{title}" },
        tiling = { type = "tiling_direction", horizontal = "H", vertical = "V" },
        clock = { type = "clock", format = "{time:%H:%M:%S}", icon = "", interval = 1,
                  on_scroll_up = { window_manager = "focus --next-workspace" } },
        cpu = { type = "cpu", format = "{usage}%", icon = "", interval = 1 },
        memory = { type = "memory", format = "{used:.1f}", icon = "", interval = 1 },
$definitions
    },
}
"@
    # WriteAllText so an editor-style save lands in one write the watcher sees.
    [System.IO.File]::WriteAllText($path, $config)
}

# A style that draws every box the bar has: sections, module boxes with hover, items in every state, a divider, and
# a tray that animates as it unfolds when the config shows one (an #id naming no module stops the bar).
function New-StressStyle([string] $path, [string[]] $optional) {
    $style = @'
:root { --foreground: #e0def4; --border: rgba(110, 106, 134, 0.25); }
* { font-family: "Segoe UI"; font-size: 13px; color: var(--foreground); }
section { margin: 4px; padding: 3px; background: rgba(25, 23, 36, 0.9); border: 1px solid var(--border); border-radius: 12px; }
tooltip { padding: 6px; background: #1f1d2e; border: 1px solid var(--border); border-radius: 10px; }
.module { padding: 0 8px; border-radius: 8px; }
.module:hover { background: rgba(64, 61, 82, 0.8); }
.icon { font-size: 16px; margin-right: 6px; color: #c4a7e7; }
.alert { color: #eb6f92; }
.item { padding: 2px 6px; margin: 0 1px; border-radius: 8px; }
.item.focused { color: #191724; background: #9ccfd8; font-weight: bold; }
.item.empty { color: #908caa; }
#divider { width: 1px; height: 60%; color: var(--border); }
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
    $work = Join-Path $LogRoot "intarsia-stress-$name-$PID"
    New-Item -ItemType Directory -Force $work | Out-Null
    $configPath = Join-Path $work 'config.lua'
    # idle has no custom modules, so its CPU is the bar's own cost of ticking and following GlazeWM.
    $optional   = if ($name -eq 'idle') { @() } else { @('endless', 'hang', 'fail', 'json') }
    if ($name -eq 'tray') {
        $optional += 'tray'
    }
    New-StressConfig $configPath 0 $optional
    New-StressStyle (Join-Path $work 'style.css') $optional
    $port       = Get-FreePort
    $serverName = if ($name -in @('reload', 'idle', 'tray')) { 'normal' } else { $name }
    $barLog     = Join-Path $work 'intarsia.log'

    $server = $null
    $bar    = $null
    $sender = $null
    $senderStarts = 0

    $failures   = [System.Collections.Generic.List[string]]::new()
    $samples    = [System.Collections.Generic.List[object]]::new()
    $cpuPercent = $null
    try {
        $server = Start-Process $serverExe -ArgumentList '--port', $port, '--scenario', $serverName -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput (Join-Path $work 'server.out') -RedirectStandardError (Join-Path $work 'server.err')
        $bar = Start-Process $barExe -ArgumentList '--config', "`"$configPath`"", '--glazewm-port', $port, '--tray-class', $trayClass -PassThru -RedirectStandardError $barLog
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
            if ($name -eq 'reload') {
                # Twenty saves a second: every settled save rebuilds every bar.
                for ($save = 0; $save -lt 20; $save++) {
                    New-StressConfig $configPath ($second * 20 + $save) $optional
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
            }
            $samples.Add((Get-Sample $bar))
        }

        if ($failures.Count -eq 0 -and $samples.Count -le $warmupSeconds + 5) {
            $failures.Add("ran only $($samples.Count) s, too short to judge growth after the $warmupSeconds s warmup")
        }
        if ($failures.Count -eq 0) {
            $baseline = $samples[$warmupSeconds]
            $final    = $samples[$samples.Count - 1]
            if ($final.PrivateMB - $baseline.PrivateMB -gt $maxPrivateGrowthMB) { $failures.Add("private memory grew $($baseline.PrivateMB) to $($final.PrivateMB) MB") }
            if ($final.Handles - $baseline.Handles -gt $maxHandleGrowth) { $failures.Add("handles grew $($baseline.Handles) to $($final.Handles)") }
            if ($final.Gdi - $baseline.Gdi -gt $maxGuiObjectGrowth) { $failures.Add("GDI objects grew $($baseline.Gdi) to $($final.Gdi)") }
            if ($final.User - $baseline.User -gt $maxGuiObjectGrowth) { $failures.Add("USER objects grew $($baseline.User) to $($final.User)") }
            if ($final.Threads - $baseline.Threads -gt $maxThreadGrowth) { $failures.Add("threads grew $($baseline.Threads) to $($final.Threads)") }
            $cpuPercent = ($final.Cpu - $baseline.Cpu).TotalMilliseconds / (($samples.Count - 1 - $warmupSeconds) * 10.0)
            if ($name -eq 'idle' -and $cpuPercent -gt $maxIdleCpuPercent) { $failures.Add("used $([math]::Round($cpuPercent, 1))% CPU while nearly idle") }
        }
    } finally {
        if ($null -ne $sender -and -not $sender.HasExited) {
            Stop-Process -Id $sender.Id -Force
        }
        if ($null -ne $bar -and -not $bar.HasExited) {
            # WM_CLOSE through taskkill without /F, as a user would stop it: every bar must release its strip and exit.
            taskkill /PID $bar.Id | Out-Null
            if (-not $bar.WaitForExit(10000)) {
                $failures.Add('did not exit within 10 s of WM_CLOSE')
                Stop-Process -Id $bar.Id -Force
            } elseif ($bar.ExitCode -ne 0) {
                $failures.Add("exited with code $($bar.ExitCode) on WM_CLOSE")
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
foreach ($exe in @($barExe, $serverExe, $senderExe)) {
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
    throw 'stress scenarios failed'
}
