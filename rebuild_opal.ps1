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
    $usingVcpkg = $false
    if ($env:CMAKE_TOOLCHAIN_FILE) {
        $configureArgs += @("-DCMAKE_TOOLCHAIN_FILE=$env:CMAKE_TOOLCHAIN_FILE")
        if ($env:CMAKE_TOOLCHAIN_FILE -match "vcpkg") {
            $usingVcpkg = $true
        }
    }
    elseif ($env:VCPKG_ROOT) {
        $toolchain = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
        $configureArgs += @("-DCMAKE_TOOLCHAIN_FILE=$toolchain")
        $usingVcpkg = $true
    }
    # WXDIR is a wxWidgets source tree. vcpkg's toolchain finds its own install,
    # and a ROOT_DIR set here hides that.
    if ($env:WXDIR -and -not $usingVcpkg) {
        $configureArgs += @("-DwxWidgets_ROOT_DIR=$env:WXDIR")
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

$openPhoneProject = Join-Path $build "samples\openphone.vcxproj"
$openPhoneMakefileDir = Join-Path $build "samples\CMakeFiles\openphone.dir"
if (-not (Test-Path -LiteralPath $openPhoneProject) -and -not (Test-Path -LiteralPath $openPhoneMakefileDir)) {
    Write-Error "OpenPhone was not generated. With vcpkg, run this script with -Regenerate so the manifest can install wxwidgets. Otherwise set WXDIR to a wxWidgets tree and reconfigure."
}

Write-Host "Building OpenPhone"
$cmakeArgs += @("--target", "openphone", "-j", "4")
& cmake @cmakeArgs
exit $LASTEXITCODE
