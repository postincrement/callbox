# PowerShell equivalent of rebuild_opal.sh.
#   powershell -File callbox\rebuild_opal.ps1
# Pass -Config Debug for a Visual Studio Debug build. Multi-config
# generators otherwise build Release. Single-config generators ignore it.
param(
    [string]$Config
)

$ErrorActionPreference = "Stop"
$build = Join-Path $PSScriptRoot "..\opalvoip-opal\build"
if (-not (Test-Path -LiteralPath $build)) {
    Write-Error "OPAL build directory not found: $build"
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
