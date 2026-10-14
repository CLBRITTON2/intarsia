<#
.SYNOPSIS
Dot-source to get Get-ReleaseNotes, which reads one version's notes out of CHANGELOG.md, and the two helpers it and
the release scripts share: Assert-Version and Find-Line.
#>

function Assert-Version([string] $version) {
    if ($version -notmatch '^\d+\.\d+\.\d+$') {
        throw "Version must be major.minor.patch, got '$version'"
    }
}

# The index of the first line from start on that matches pattern, or -1.
function Find-Line([string[]] $lines, [string] $pattern, [int] $start) {
    for ($i = $start; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match $pattern) {
            return $i
        }
    }
    return -1
}

# The lines under "## <version> (<date>)", up to the next section, or $null when CHANGELOG.md has no section for it.
function Get-ReleaseNotes([string] $changelogPath, [string] $version) {
    if (-not (Test-Path $changelogPath)) {
        throw "$changelogPath not found"
    }
    $lines   = @(Get-Content $changelogPath)
    $heading = Find-Line $lines "^## $([regex]::Escape($version)) \(" 0
    if ($heading -lt 0) {
        return $null
    }
    $end = Find-Line $lines '^## ' ($heading + 1)
    if ($end -lt 0) {
        $end = $lines.Count
    }
    return @($lines[($heading + 1)..($end - 1)] | Where-Object { $_ -ne '' })
}
