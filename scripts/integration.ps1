<#
.SYNOPSIS
Builds the debug preset and runs the integration tests through ctest.

.DESCRIPTION
Each test starts the real build\debug\intarsia.exe against build\debug\glazewm.exe (tests/stress/GlazeWmServer.cpp) on
a free port and judges it by what it does outside its process. Each test's config, style and logs stay in
%TEMP%\intarsia-integration-<test>-<pid>.

Run it from an interactive desktop session: the bar needs a desktop to draw on.
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# build.ps1 entered the dev shell in this process, so VS's ctest is already first on PATH.
& (Join-Path $PSScriptRoot 'build.ps1') -Preset debug

# One test at a time, since every bar docks to the same monitors.
ctest --test-dir (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\debug') --label-regex integration --output-on-failure --timeout 120
if ($LASTEXITCODE -ne 0) {
    throw "ctest failed with exit code $LASTEXITCODE"
}
