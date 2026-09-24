<#
.SYNOPSIS
Fails when any source file is not clang-formatted or trips a clang-tidy error.

.DESCRIPTION
Uses the LLVM tools bundled with Visual Studio 2022 so local runs and CI share one clang version. clang-format
output changes between LLVM majors, so the script refuses any major other than the pinned one.
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$pinnedLlvmMajor = 19
$repoRoot        = Split-Path -Parent $PSScriptRoot

. (Join-Path $PSScriptRoot 'VsDevShell.ps1')

function Assert-LlvmMajor([string] $tool, [int] $major) {
    if (-not (Test-Path $tool)) {
        throw "$tool not found, it ships with the Visual Studio C++ workload"
    }
    $output = & $tool --version
    if ("$output" -notmatch 'version (\d+)\.') {
        throw "Could not read a version from '$tool --version': $output"
    }
    if ([int] $Matches[1] -ne $major) {
        throw "$tool is LLVM $($Matches[1]), this repo pins LLVM $major"
    }
}

function Invoke-Checked([string] $what, [scriptblock] $command) {
    & $command
    if ($LASTEXITCODE -ne 0) {
        throw "$what failed with exit code $LASTEXITCODE"
    }
}

$vsPath = Enter-Vs2022DevShell

$llvmBin     = Join-Path $vsPath 'VC\Tools\Llvm\x64\bin'
$clangFormat = Join-Path $llvmBin 'clang-format.exe'
$clangTidy   = Join-Path $llvmBin 'clang-tidy.exe'
Assert-LlvmMajor $clangFormat $pinnedLlvmMajor
Assert-LlvmMajor $clangTidy $pinnedLlvmMajor

$sources = @(Get-ChildItem -Path (Join-Path $repoRoot 'src') -Recurse -Include '*.cpp', '*.hpp' | ForEach-Object FullName)
if ($sources.Count -eq 0) {
    throw "No sources found under $repoRoot\src"
}

Invoke-Checked 'clang-format' { & $clangFormat --dry-run --Werror $sources }

Push-Location $repoRoot
try {
    Invoke-Checked 'cmake configure' { cmake --preset debug }
} finally {
    Pop-Location
}

# Headers are checked through the .cpp files that include them (HeaderFilterRegex in .clang-tidy).
$translationUnits = @($sources | Where-Object { $_ -like '*.cpp' })
Invoke-Checked 'clang-tidy' { & $clangTidy -p (Join-Path $repoRoot 'build\debug') --quiet $translationUnits }
