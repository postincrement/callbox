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
    $vcpkgRoot = $null
    if ($env:CMAKE_TOOLCHAIN_FILE) {
        $configureArgs += @("-DCMAKE_TOOLCHAIN_FILE=$($env:CMAKE_TOOLCHAIN_FILE)")
        if ($env:CMAKE_TOOLCHAIN_FILE -match "vcpkg") {
            $usingVcpkg = $true
            # <vcpkg>/scripts/buildsystems/vcpkg.cmake
            $vcpkgRoot = Split-Path (Split-Path (Split-Path $env:CMAKE_TOOLCHAIN_FILE -Parent) -Parent) -Parent
        }
    }
    else {
        if ($env:VCPKG_ROOT) {
            $vcpkgRoot = $env:VCPKG_ROOT
        }
        else {
            $siblingToolchain = Join-Path $PSScriptRoot "..\vcpkg\scripts\buildsystems\vcpkg.cmake"
            if (Test-Path -LiteralPath $siblingToolchain) {
                $vcpkgRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\vcpkg")).Path
            }
        }
        if ($vcpkgRoot) {
            $toolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
            if (Test-Path -LiteralPath $toolchain) {
                $configureArgs += @("-DCMAKE_TOOLCHAIN_FILE=$toolchain")
                $usingVcpkg = $true
            }
        }
    }
    # A classic "vcpkg install wxwidgets" is under <vcpkg>/installed. Manifest
    # mode installs into the build tree and does not search that directory, so
    # the existing install would be invisible.
    if ($usingVcpkg -and $vcpkgRoot) {
        $classicWx = Join-Path $vcpkgRoot "installed\x64-windows\share\wxwidgets\wxWidgetsConfig.cmake"
        if (Test-Path -LiteralPath $classicWx) {
            $configureArgs += "-DVCPKG_MANIFEST_MODE=OFF"
            Write-Host "Using the vcpkg wxWidgets install in $vcpkgRoot\installed\x64-windows"
        }
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

$openPhoneProject = Join-Path $build "samples\openphone.vcxproj"
$openPhoneMakefileDir = Join-Path $build "samples\CMakeFiles\openphone.dir"
if (-not (Test-Path -LiteralPath $openPhoneProject) -and -not (Test-Path -LiteralPath $openPhoneMakefileDir)) {
    Write-Error "OpenPhone was not generated. Run this script with -Regenerate so wxWidgets is found and the OpenPhone target is created."
}

# OpenPhone links the PTLib import library from the sibling build. That
# library is not an OPAL target, so build it before OpenPhone when it is missing.
$ptlibConfigDir = if ($Config) { Join-Path $ptlibBuild $Config } else { $ptlibBuild }
$ptlibStem = if ($Config -eq "Debug") { "ptlib64d" } else { "ptlib64" }
if (-not (Test-Path -LiteralPath (Join-Path $ptlibConfigDir "$ptlibStem.lib")) -and
    -not (Test-Path -LiteralPath (Join-Path $ptlibConfigDir "libpt.a"))) {
    Write-Host "Building PTLib"
    $ptlibArgs = @("--build", (Resolve-Path -LiteralPath $ptlibBuild).Path, "-j", "4")
    if ($Config) {
        $ptlibArgs += @("--config", $Config)
    }
    & cmake @ptlibArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

# Build OpenPhone directly. It pulls in libopal. Waiting for every plugin and
# sample to succeed first skips OpenPhone when an unrelated target fails.
Write-Host "Building OpenPhone"
$cmakeArgs = @("--build", $build, "--target", "openphone", "-j", "4")
if ($Config) {
    $cmakeArgs += @("--config", $Config)
}
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

# Plugins are loaded at run time, so the OpenPhone target does not build them.
function Build-Plugins([string]$BuildDir) {
    $pluginRoot = Join-Path $BuildDir "plugins"
    if (-not (Test-Path -LiteralPath $pluginRoot)) {
        return
    }
    $projects = @(Get-ChildItem -LiteralPath $pluginRoot -Recurse -Filter *.vcxproj -File |
        Where-Object { $_.BaseName -like "*_ptplugin" -or $_.BaseName -like "ptlib_plugin_*" })
    if ($projects.Count -eq 0) {
        return
    }
    Write-Host "Building plugins"
    $pluginArgs = @("--build", (Resolve-Path -LiteralPath $BuildDir).Path, "-j", "4")
    foreach ($project in $projects) {
        $pluginArgs += @("--target", $project.BaseName)
    }
    if ($Config) {
        $pluginArgs += @("--config", $Config)
    }
    & cmake @pluginArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
Build-Plugins $build
Build-Plugins $ptlibBuild

# Keep the loader search path outside the OpenPhone output directory.
$libs = Join-Path $PSScriptRoot "libs"
if ($Config) {
    $libs = Join-Path $libs $Config
}
New-Item -ItemType Directory -Force -Path $libs | Out-Null

$openPhoneDir = Join-Path $build "samples"
if ($Config) {
    $openPhoneDir = Join-Path $openPhoneDir $Config
}
$opalBin = if ($Config) { Join-Path $build $Config } else { $build }
$ptlibBin = if ($Config) { Join-Path $ptlibBuild $Config } else { $ptlibBuild }
$opalStem = if ($Config -eq "Debug") { "opal64d" } else { "opal64" }
$ptlibStem = if ($Config -eq "Debug") { "ptlib64d" } else { "ptlib64" }

# wxWidgets and its dependencies are already beside OpenPhone. The OPAL and
# PTLib DLLs are not, so copy those from the library build directories last.
Get-ChildItem -LiteralPath $openPhoneDir -Filter *.dll -File -ErrorAction SilentlyContinue |
    Copy-Item -Destination $libs -Force
foreach ($dll in @(
        (Join-Path $opalBin "$opalStem.dll"),
        (Join-Path $ptlibBin "$ptlibStem.dll"))) {
    if (-not (Test-Path -LiteralPath $dll)) {
        Write-Error "Required DLL was not built: $dll"
    }
    Copy-Item -LiteralPath $dll -Destination $libs -Force
}
Write-Host "Copied runtime DLLs to $libs"
exit 0
