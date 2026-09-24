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

function Get-Vs2022Path {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw "vswhere not found at $vswhere, install Visual Studio 2022"
    }
    $path = & $vswhere -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $path) {
        throw 'No Visual Studio 2022 install has the x64 C++ build tools (Microsoft.VisualStudio.Component.VC.Tools.x86.x64)'
    }
    return @($path)[0]
}

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

$vsPath = Get-Vs2022Path
# VsDevCmd shells out to vswhere by bare name and prints an error when it is not on PATH.
$env:PATH = "$(Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer');$env:PATH"
Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

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
