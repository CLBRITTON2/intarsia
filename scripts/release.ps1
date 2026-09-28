<#
.SYNOPSIS
Builds a release of intarsia and packages it as build\dist\intarsia-<Version>.zip, with its notes beside it.

.DESCRIPTION
Refuses uncommitted changes to tracked files, so the package is what HEAD holds, and a Version with no section in
CHANGELOG.md (scripts/changelog.ps1 adds one). The build starts from an empty build\dist, with Version stamped into
intarsia.exe's version resource. The zip holds intarsia.exe, both shipped configs, README.md, LICENSE and
CHANGELOG.md, and build\dist\release-notes.md holds the version's changelog section.

.PARAMETER Version
The version to release, as major.minor.patch.
#>
param(
    [Parameter(Mandatory)]
    [string] $Version
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'ChangelogSection.ps1')
. (Join-Path $PSScriptRoot 'VsDevShell.ps1')
Assert-Version $Version

$repoRoot = Split-Path -Parent $PSScriptRoot
$notes    = Get-ChangelogSection (Join-Path $repoRoot 'CHANGELOG.md') $Version
if ($null -eq $notes) {
    throw "CHANGELOG.md has no section for ${Version}: run scripts/changelog.ps1 -Version $Version -From <previous tag>, edit it and commit"
}

Push-Location $repoRoot
try {
    # Untracked files never reach the build or the package, so only changes to tracked ones refuse the release.
    $dirty = @(git status --porcelain --untracked-files=no)
    if ($LASTEXITCODE -ne 0) {
        throw "git status failed with exit code $LASTEXITCODE"
    }
    if ($dirty.Count -gt 0) {
        throw "The working tree has uncommitted changes, commit or stash them first:`n$($dirty -join "`n")"
    }
    $configFiles = @(git ls-files config)
    if ($LASTEXITCODE -ne 0) {
        throw "git ls-files config failed with exit code $LASTEXITCODE"
    }

    Enter-Vs2022DevShell | Out-Null
    $dist = Join-Path $repoRoot 'build\dist'
    if (Test-Path $dist) {
        Remove-Item -Recurse -Force $dist
    }
    cmake --preset release -B $dist "-DINTARSIA_VERSION=$Version"
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --preset release failed with exit code $LASTEXITCODE"
    }
    cmake --build $dist --target intarsia
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --build $dist failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

$stage = Join-Path $dist "package\intarsia-$Version"
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item (Join-Path $dist 'intarsia.exe') $stage
foreach ($file in $configFiles) {
    $target = Join-Path $stage $file
    New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null
    Copy-Item (Join-Path $repoRoot $file) $target
}
Copy-Item (Join-Path $repoRoot 'README.md'), (Join-Path $repoRoot 'LICENSE'), (Join-Path $repoRoot 'CHANGELOG.md') $stage

$zip = Join-Path $dist "intarsia-$Version.zip"
Compress-Archive -Path $stage -DestinationPath $zip
Set-Content -Path (Join-Path $dist 'release-notes.md') -Value $notes
Write-Host "Packaged $zip"
