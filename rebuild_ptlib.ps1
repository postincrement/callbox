# PowerShell equivalent of rebuild_ptlib.sh.
#   powershell -File callbox\rebuild_ptlib.ps1
#   powershell -File callbox\rebuild_ptlib.ps1 -Regenerate
#
# -Regenerate deletes the build directory and generates a Visual Studio 2022
# x64 solution. Pass -Config Debug for a Debug build. Multi-config generators
# otherwise build Release. Single-config generators ignore -Config.
param(
    [string]$Config,
    [switch]$Regenerate
)

$ErrorActionPreference = "Stop"
$source = Join-Path $PSScriptRoot "..\opalvoip-ptlib"
$build = Join-Path $source "build"

if ($Regenerate) {
    if (Test-Path -LiteralPath $build) {
        Remove-Item -LiteralPath $build -Recurse -Force
    }
    $configureArgs = @(
        "-S", $source,
        "-B", $build,
        "-G", "Visual Studio 17 2022",
        "-A", "x64"
    )
    if ($env:CMAKE_TOOLCHAIN_FILE) {
        $configureArgs += @("-DCMAKE_TOOLCHAIN_FILE=$env:CMAKE_TOOLCHAIN_FILE")
    }
    elseif ($env:VCPKG_ROOT) {
        $toolchain = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
        $configureArgs += @("-DCMAKE_TOOLCHAIN_FILE=$toolchain")
    }
    & cmake @configureArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

if (-not (Test-Path -LiteralPath $build)) {
    Write-Error "PTLib build directory not found: $build. Pass -Regenerate to create a Visual Studio 2022 solution."
}
$build = (Resolve-Path -LiteralPath $build).Path

if (-not $Config) {
    $cache = Join-Path $build "CMakeCache.txt"
    if (Select-String -Path $cache -Pattern "^CMAKE_CONFIGURATION_TYPES:" -Quiet) {
        $Config = "Release"
    }
}

$cmakeArgs = @("--build", $build, "-j", "4")
if ($Config) {
    $cmakeArgs += @("--config", $Config)
}

& cmake @cmakeArgs
exit $LASTEXITCODE
