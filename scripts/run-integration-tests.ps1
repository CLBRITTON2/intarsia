<#
.SYNOPSIS
Builds the debug preset and runs the integration tests, printing each test's steps.

.DESCRIPTION
Each test starts the real build\debug\intarsia.exe against build\debug\glazewm.exe (tests/support/GlazeWmServer.cpp) on
a free port and judges it by what it does outside its process. Every step prints a line to stdout: each process
started, each sweep of clicks and every command it sent, each pushed event, the bar's exit, then the bar's log and the
server's stderr. Each test's config, style and logs stay in %TEMP%\intarsia-integration-<test>-<pid>.

The executable runs directly rather than through ctest, which hides the output of a test that passes.

Run it from an interactive desktop session: the bar needs a desktop to draw on.
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'vs-dev-shell.ps1')

& (Join-Path $PSScriptRoot 'build.ps1') -Preset debug

# GoogleTest runs one test at a time, since every bar docks to the same monitors.
Invoke-Checked 'intarsia_integration.exe' { & (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\debug\intarsia_integration.exe') }
