<#
.SYNOPSIS
Builds the debug preset and runs the unit and config tests through ctest. The integration tests run apart, through
scripts/integration.ps1.
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# build.ps1 entered the dev shell in this process, so VS's ctest is already first on PATH.
& (Join-Path $PSScriptRoot 'build.ps1') -Preset debug

# A test that hangs (a thread that never joins) fails instead of holding the job.
ctest --test-dir (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\debug') --label-exclude integration --output-on-failure --timeout 300
if ($LASTEXITCODE -ne 0) {
    throw "ctest failed with exit code $LASTEXITCODE"
}
