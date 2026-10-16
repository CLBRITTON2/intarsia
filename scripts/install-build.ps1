<#
.SYNOPSIS
Builds the release preset, installs intarsia.exe and intarsia-cli.exe to %LOCALAPPDATA%\Programs\intarsia and starts
the bar from there.

.DESCRIPTION
The new intarsia-cli checks the config in %USERPROFILE%\.config\intarsia first, so a build that cannot load it changes
nothing and leaves the running bar alone. Bars running from the install folder are closed without /F, so each one
releases its strip of the work area before its files are replaced. Bars started from anywhere else keep running.
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'vs-dev-shell.ps1')

$built   = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\release'
$install = Join-Path $env:LOCALAPPDATA 'Programs\intarsia'
$bar     = Join-Path $install 'intarsia.exe'

& (Join-Path $PSScriptRoot 'build.ps1') -Preset release
Invoke-Checked 'intarsia-cli check' { & (Join-Path $built 'intarsia-cli.exe') check }

$running = @(Get-Process intarsia -ErrorAction Ignore | Where-Object Path -EQ $bar)
foreach ($process in $running) {
    Invoke-Checked "taskkill /PID $($process.Id)" { taskkill /PID $process.Id }
}
$running | Wait-Process -Timeout 10

New-Item -ItemType Directory -Force $install | Out-Null
Copy-Item (Join-Path $built 'intarsia.exe'), (Join-Path $built 'intarsia-cli.exe') $install
Start-Process $bar
Write-Host "Installed and started $bar"
