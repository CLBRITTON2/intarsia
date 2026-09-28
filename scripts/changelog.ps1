<#
.SYNOPSIS
Adds a section for Version to the top of CHANGELOG.md, one line per commit after From, to edit before the release.

.PARAMETER Version
The version to release, as major.minor.patch.

.PARAMETER From
The previous release's tag (v0.1.0), or for the first release the first commit. The section lists every commit after
it up to HEAD, oldest first, merges left out.
#>
param(
    [Parameter(Mandatory)]
    [string] $Version,
    [Parameter(Mandatory)]
    [string] $From
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'ChangelogSection.ps1')
Assert-Version $Version

$repoRoot  = Split-Path -Parent $PSScriptRoot
$changelog = Join-Path $repoRoot 'CHANGELOG.md'
if ($null -ne (Get-ChangelogSection $changelog $Version)) {
    throw "CHANGELOG.md already has a section for $Version"
}

Push-Location $repoRoot
try {
    $subjects = @(git log --no-merges --reverse --format=%s "$From..HEAD")
    if ($LASTEXITCODE -ne 0) {
        throw "git log $From..HEAD failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}
if ($subjects.Count -eq 0) {
    throw "No commits after $From"
}

$lines = @(Get-Content $changelog)
# The section goes above the newest one, or after the header when there is none yet.
$insertAt = $lines.Count
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^## ') {
        $insertAt = $i
        break
    }
}
$section = @("## $Version ($(Get-Date -Format 'yyyy-MM-dd'))", '') + @($subjects | ForEach-Object { "- $($_.Trim())" }) + @('')
$before  = if ($insertAt -gt 0) { @($lines[0..($insertAt - 1)]) } else { @() }
if ($before.Count -gt 0 -and $before[-1] -ne '') {
    $before += ''
}
$after   = if ($insertAt -lt $lines.Count) { @($lines[$insertAt..($lines.Count - 1)]) } else { @() }
Set-Content -Path $changelog -Value ($before + $section + $after)
Write-Host "Added $Version to CHANGELOG.md with $($subjects.Count) entries: edit them, then commit"
