<#
.SYNOPSIS
Builds the debug preset and runs the unit tests through ctest. The integration tests run apart, through
scripts/run-integration-tests.ps1.
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'vs-dev-shell.ps1')

# build.ps1 entered the dev shell in this process, so VS's ctest is already first on PATH.
& (Join-Path $PSScriptRoot 'build.ps1') -Preset debug

# A test that hangs (a thread that never joins) fails instead of holding the job.
Invoke-Checked 'ctest' { ctest --test-dir (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\debug') --label-exclude integration --output-on-failure --timeout 300 }
