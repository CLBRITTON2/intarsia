<#
.SYNOPSIS
Publishes the package scripts/package-release.ps1 built as the GitHub release v<Version>, tagging Commit.

.PARAMETER Version
The version scripts/package-release.ps1 packaged, as major.minor.patch.

.PARAMETER Commit
The commit the release tags, the one the package was built from.

.PARAMETER Token
A GitHub token that may create releases in this repository (contents: write).
#>
param(
    [Parameter(Mandatory)]
    [string] $Version,
    [Parameter(Mandatory)]
    [string] $Commit,
    [Parameter(Mandatory)]
    [string] $Token
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'read-release-notes.ps1')
Assert-Version $Version

$dist  = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\dist'
$zip   = Join-Path $dist "intarsia-$Version.zip"
$notes = Join-Path $dist 'release-notes.md'
foreach ($path in @($zip, $notes)) {
    if (-not (Test-Path $path)) {
        throw "$path not found: run scripts/package-release.ps1 -Version $Version first"
    }
}

$env:GH_TOKEN = $Token
$attempts     = 3
for ($attempt = 1; $attempt -le $attempts; $attempt++) {
    $output = gh release create "v$Version" $zip --title "intarsia $Version" --notes-file $notes --target $Commit 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Published v$Version at $Commit"
        return
    }
    # A release that already exists will not succeed on a retry.
    if ("$output" -match 'already exists') {
        throw "Release v$Version already exists: $output"
    }
    if ($attempt -lt $attempts) {
        Write-Warning "gh release create v$Version failed (attempt $attempt of $attempts, exit code $LASTEXITCODE): $output"
        Start-Sleep -Seconds (5 * $attempt)
    }
}
throw "gh release create v$Version failed after $attempts attempts with exit code ${LASTEXITCODE}: $output"
