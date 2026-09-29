# PowerShell equivalent of rebuild_opal.sh.
#   powershell -File callbox\rebuild_opal.ps1
#   powershell -File callbox\rebuild_opal.ps1 -Regenerate
#
# -Regenerate deletes the build directory and generates a Visual Studio 2022
# x64 solution against the sibling PTLib build, with samples enabled so
# OpenPhone exists. Pass -Config Debug for a Debug build. Multi-config
# generators otherwise build Release. Single-config generators ignore -Config.
param(
    [string]$Config,
    [switch]$Regenerate
)

$ErrorActionPreference = "Stop"
$source = Join-Path $PSScriptRoot "..\opalvoip-opal"
$build = Join-Path $source "build"
$ptlibBuild = Join-Path $PSScriptRoot "..\opalvoip-ptlib\build"

if ($Regenerate) {
    $ptlibConfig = Join-Path $ptlibBuild "PTLibConfig.cmake"
    if (-not (Test-Path -LiteralPath $ptlibConfig)) {
        Write-Error "PTLib has not been configured. Run rebuild_ptlib.ps1 -Regenerate first."
    }
    if (Test-Path -LiteralPath $build) {
        Remove-Item -LiteralPath $build -Recurse -Force
    }
    $configureArgs = @(
        "-S", $source,
        "-B", $build,
        "-G", "Visual Studio 17 2022",
        "-A", "x64",
        "-DOPAL_PTLIB_DIR=$ptlibBuild",
        "-DOPAL_BUILD_SAMPLES=ON"
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
    Write-Error "OPAL build directory not found: $build. Pass -Regenerate to create a Visual Studio 2022 solution."
}
$build = (Resolve-Path -LiteralPath $build).Path

if (-not $Config) {
    $cache = Join-Path $build "CMakeCache.txt"
    if (Select-String -Path $cache -Pattern "^CMAKE_CONFIGURATION_TYPES:" -Quiet) {
        $Config = "Release"
    }
}

$cmakeArgs = @("--build", $build)
if ($Config) {
    $cmakeArgs += @("--config", $Config)
}

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$cmakeArgs += @("--target", "openphone")
& cmake @cmakeArgs
exit $LASTEXITCODE
