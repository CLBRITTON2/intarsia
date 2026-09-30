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

. (Join-Path $PSScriptRoot 'vs-dev-shell.ps1')
Enter-Vs2022DevShell | Out-Null

Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    Invoke-Checked "cmake --preset $Preset" { cmake --preset $Preset }
    Invoke-Checked "cmake --build --preset $Preset" { cmake --build --preset $Preset }
} finally {
    Pop-Location
}
