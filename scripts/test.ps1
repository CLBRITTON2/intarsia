<#
.SYNOPSIS
Builds the debug preset and runs the unit and config tests through ctest.
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# build.ps1 entered the dev shell in this process, so VS's ctest is already first on PATH.
& (Join-Path $PSScriptRoot 'build.ps1') -Preset debug

ctest --test-dir (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\debug') --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "ctest failed with exit code $LASTEXITCODE"
}
