<#
.SYNOPSIS
Dot-source to get Enter-Vs2022DevShell, which puts the VS 2022 x64 toolchain first on PATH for this session, and
Invoke-Checked, which fails on a native command's nonzero exit.
#>

# Runs a native command and throws when it exits nonzero. Its output passes through.
function Invoke-Checked([string] $what, [scriptblock] $command) {
    & $command
    if ($LASTEXITCODE -ne 0) {
        throw "$what failed with exit code $LASTEXITCODE"
    }
}

# Returns the install's path. CMake and Ninja are taken from Visual Studio as well, ahead of anything else on PATH:
# another CMake, such as MSYS2's, can carry its own curl CA bundle and fail FetchContent downloads over HTTPS.
function Enter-Vs2022DevShell {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw "vswhere not found at $vswhere, install Visual Studio 2022"
    }
    $vsPath = & $vswhere -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) {
        throw 'No Visual Studio 2022 install has the x64 C++ build tools (Microsoft.VisualStudio.Component.VC.Tools.x86.x64)'
    }
    $vsPath = @($vsPath)[0]

    # VsDevCmd shells out to vswhere by bare name and prints an error when it is not on PATH.
    $env:PATH = "$(Split-Path -Parent $vswhere);$env:PATH"
    Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

    $cmakeBin = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
    $ninjaBin = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
    foreach ($bin in @($cmakeBin, $ninjaBin)) {
        if (-not (Test-Path $bin)) {
            throw "$bin not found, install the Visual Studio 'C++ CMake tools for Windows' component"
        }
    }
    $env:PATH = "$cmakeBin;$ninjaBin;$env:PATH"
    return $vsPath
}
