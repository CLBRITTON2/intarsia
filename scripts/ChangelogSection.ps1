<#
.SYNOPSIS
Reads one version's section out of CHANGELOG.md. Dot-source it.
#>

# The lines under "## <version> (<date>)", up to the next section, or $null when CHANGELOG.md has no section for it.
function Get-ChangelogSection([string] $changelogPath, [string] $version) {
    if (-not (Test-Path $changelogPath)) {
        throw "$changelogPath not found"
    }
    $lines   = @(Get-Content $changelogPath)
    $heading = "^## $([regex]::Escape($version)) \("
    $start   = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match $heading) {
            $start = $i + 1
            break
        }
    }
    if ($start -lt 0) {
        return $null
    }
    $end = $lines.Count
    for ($i = $start; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^## ') {
            $end = $i
            break
        }
    }
    return @($lines[$start..($end - 1)] | Where-Object { $_ -ne '' })
}

function Assert-Version([string] $version) {
    if ($version -notmatch '^\d+\.\d+\.\d+$') {
        throw "Version must be major.minor.patch, got '$version'"
    }
}
