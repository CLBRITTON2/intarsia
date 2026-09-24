<#
.SYNOPSIS
Configures and builds one CMake preset with the Visual Studio 2022 toolchain.
#>
param(
    [Parameter(Mandatory)]
    [ValidateSet('debug', 'release')]
    [string] $Preset
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'VsDevShell.ps1')
Enter-Vs2022DevShell | Out-Null

Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --preset $Preset failed with exit code $LASTEXITCODE"
    }
    cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --build --preset $Preset failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}
