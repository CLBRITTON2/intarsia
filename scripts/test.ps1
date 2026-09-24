<#
.SYNOPSIS
Builds the debug preset and runs the unit and config tests through ctest.
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

& (Join-Path $PSScriptRoot 'build.ps1') -Preset debug

. (Join-Path $PSScriptRoot 'VsDevShell.ps1')
Enter-Vs2022DevShell | Out-Null

ctest --test-dir (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\debug') --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "ctest failed with exit code $LASTEXITCODE"
}
