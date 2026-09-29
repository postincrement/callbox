# PowerShell equivalent of rebuild_ptlib.sh.
#   powershell -File callbox\rebuild_ptlib.ps1
# Pass -Config Debug for a Visual Studio Debug build. Multi-config
# generators otherwise build Release. Single-config generators ignore it.
param(
    [string]$Config
)

$ErrorActionPreference = "Stop"
$build = Join-Path $PSScriptRoot "..\opalvoip-ptlib\build"
if (-not (Test-Path -LiteralPath $build)) {
    Write-Error "PTLib build directory not found: $build"
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
