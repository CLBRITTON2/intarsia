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

. (Join-Path $PSScriptRoot 'read-release-notes.ps1')
. (Join-Path $PSScriptRoot 'vs-dev-shell.ps1')
Assert-Version $Version

$repoRoot  = Split-Path -Parent $PSScriptRoot
$changelog = Join-Path $repoRoot 'CHANGELOG.md'
if ($null -ne (Get-ReleaseNotes $changelog $Version)) {
    throw "CHANGELOG.md already has a section for $Version"
}

$subjects = @(Invoke-Checked "git log $From..HEAD" { git -C $repoRoot log --no-merges --reverse --format=%s "$From..HEAD" })
if ($subjects.Count -eq 0) {
    throw "No commits after $From"
}

$lines = @(Get-Content $changelog)
# The section goes above the newest one, or after the header when there is none yet.
$insertAt = Find-Line $lines '^## ' 0
if ($insertAt -lt 0) {
    $insertAt = $lines.Count
}
$section = @("## $Version ($(Get-Date -Format 'yyyy-MM-dd'))", '') + @($subjects | ForEach-Object { "- $($_.Trim())" }) + @('')
$before  = @($lines | Select-Object -First $insertAt)
if ($before.Count -gt 0 -and $before[-1] -ne '') {
    $before += ''
}
$after   = @($lines | Select-Object -Skip $insertAt)
Set-Content -Path $changelog -Value ($before + $section + $after)
Write-Host "Added $Version to CHANGELOG.md with $($subjects.Count) entries: edit them, then commit"
