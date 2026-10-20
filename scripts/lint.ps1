<#
.SYNOPSIS
Fails when any source file is not clang-formatted or trips a clang-tidy error.

.DESCRIPTION
Uses the LLVM tools bundled with Visual Studio 2022 so local runs and CI share one clang version. clang-format
output changes between LLVM majors, so the script refuses any major other than the pinned one. clang-tidy runs one
process per translation unit, as many at once as there are logical processors and free memory allows.

.PARAMETER Changed
Checks only the sources that differ from HEAD (staged, unstaged or untracked), plus the translation units that
include a changed header. For quick local runs: CI runs the full check.
#>
param(
    [switch] $Changed
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$pinnedLlvmMajor = 19
$gbPerUnit       = 1.25
$reserveGb       = 1
$repoRoot        = Split-Path -Parent $PSScriptRoot

. (Join-Path $PSScriptRoot 'vs-dev-shell.ps1')

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

$vsPath = Enter-Vs2022DevShell

$llvmBin     = Join-Path $vsPath 'VC\Tools\Llvm\x64\bin'
$clangFormat = Join-Path $llvmBin 'clang-format.exe'
$clangTidy   = Join-Path $llvmBin 'clang-tidy.exe'
Assert-LlvmMajor $clangFormat $pinnedLlvmMajor
Assert-LlvmMajor $clangTidy $pinnedLlvmMajor

$sourceDirs = @((Join-Path $repoRoot 'src'), (Join-Path $repoRoot 'tests'))
$allSources = @(Get-ChildItem -Path $sourceDirs -Recurse -Include '*.cpp', '*.hpp' | ForEach-Object FullName)
if ($allSources.Count -eq 0) {
    throw "No sources found under $($sourceDirs -join ', ')"
}
# Headers are checked through the .cpp files that include them (HeaderFilterRegex in .clang-tidy).
$allUnits = @($allSources | Where-Object { $_ -like '*.cpp' })

if ($Changed) {
    $changedPaths = @(Invoke-Checked 'git diff' { git -C $repoRoot diff --name-only HEAD }) + @(Invoke-Checked 'git ls-files' { git -C $repoRoot ls-files --others --exclude-standard })
    $changedFull = @($changedPaths | ForEach-Object { [System.IO.Path]::GetFullPath((Join-Path $repoRoot $_)) })
    $sources     = @($allSources | Where-Object { $_ -in $changedFull })
    # A header change reaches every unit that names it in an include, matched by file name.
    $headerNames = @($sources | Where-Object { $_ -like '*.hpp' } | ForEach-Object { [System.IO.Path]::GetFileName($_) })
    $units       = @($allUnits | Where-Object {
            $unit = $_
            ($unit -in $sources) -or ($headerNames.Count -gt 0 -and (Select-String -Path $unit -SimpleMatch -Pattern $headerNames -Quiet))
        })
    if ($sources.Count -eq 0) {
        Write-Host 'no changed sources to lint'
        return
    }
} else {
    $sources = $allSources
    $units   = $allUnits
}

Invoke-Checked 'clang-format' { & $clangFormat --dry-run --Werror $sources }

Push-Location $repoRoot
try {
    # Configure only: clang-tidy needs the compile commands, and the lint preset leaves out the precompiled headers
    # clang-tidy cannot read.
    Invoke-Checked 'cmake configure' { cmake --preset lint }
} finally {
    Pop-Location
}

$buildDir = Join-Path $repoRoot 'build\lint'
# The static analyzer peaks at about 1.1 GB on the largest units, so free memory, not the core count, bounds how many
# run at once. The reserve is fixed rather than a share of free memory: a CI runner has about 6 GB free and needs three
# at once to finish within the lint job's hour.
$freeGb   = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB
$jobs     = [Math]::Max(1, [Math]::Min([Environment]::ProcessorCount, [Math]::Floor(($freeGb - $reserveGb) / $gbPerUnit)))
Write-Host "clang-tidy running $jobs at once ($([Math]::Round($freeGb, 1)) GB free)"
$results  = $units | ForEach-Object -ThrottleLimit $jobs -Parallel {
    # DirectXMath (through C++/WinRT's numerics) includes clang's cpuid.h under clang, and VS's clang tools ship no
    # clang builtin headers.
    $output = & $using:clangTidy -p $using:buildDir --quiet --extra-arg=-D_XM_NO_INTRINSICS_ $_ 2>&1
    [pscustomobject]@{
        Unit     = $_
        ExitCode = $LASTEXITCODE
        Output   = @($output | ForEach-Object { "$_" } | Where-Object { $_ -notmatch '^\d+ warnings? generated\.$' })
    }
}

$failed = @($results | Where-Object ExitCode -ne 0)
foreach ($result in @($results | Sort-Object Unit)) {
    if ($result.ExitCode -ne 0 -or $result.Output.Count -gt 0) {
        Write-Host "== $($result.Unit)"
        $result.Output | ForEach-Object { Write-Host $_ }
    }
}
Write-Host "clang-tidy checked $($units.Count) translation units, $($failed.Count) failed"
if ($failed.Count -gt 0) {
    throw "clang-tidy failed on $(($failed | ForEach-Object { [System.IO.Path]::GetFileName($_.Unit) }) -join ', ')"
}
