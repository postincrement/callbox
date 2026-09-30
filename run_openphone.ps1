# Run OpenPhone with callbox\libs on the DLL search path and PTLIBPLUGINDIR
# pointing at the codec and device plugins for this configuration.
#   powershell -File callbox\run_openphone.ps1
#   powershell -File callbox\run_openphone.ps1 -Config Debug
# Extra arguments are passed through to openphone.exe.
param(
    [string]$Config,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$OpenPhoneArgs
)

$ErrorActionPreference = "Stop"
$build = Join-Path $PSScriptRoot "..\opalvoip-opal\build"
if (-not (Test-Path -LiteralPath $build)) {
    Write-Error "OPAL build directory not found: $build. Run rebuild_opal.ps1 first."
}
$build = (Resolve-Path -LiteralPath $build).Path

if (-not $Config) {
    $cache = Join-Path $build "CMakeCache.txt"
    if (Select-String -Path $cache -Pattern "^CMAKE_CONFIGURATION_TYPES:" -Quiet) {
        $Config = "Release"
    }
}

$libs = Join-Path $PSScriptRoot "libs"
$openPhoneDir = Join-Path $build "samples"
if ($Config) {
    $libs = Join-Path $libs $Config
    $openPhoneDir = Join-Path $openPhoneDir $Config
}
$exe = Join-Path $openPhoneDir "openphone.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    Write-Error "OpenPhone was not built: $exe. Run rebuild_opal.ps1 first."
}
$opalStem = if ($Config -eq "Debug") { "opal64d" } else { "opal64" }
$ptlibStem = if ($Config -eq "Debug") { "ptlib64d" } else { "ptlib64" }
if (-not (Test-Path -LiteralPath (Join-Path $libs "$opalStem.dll")) -or
    -not (Test-Path -LiteralPath (Join-Path $libs "$ptlibStem.dll"))) {
    Write-Error "Runtime DLLs are missing from $libs. Run rebuild_opal.ps1 first."
}

$env:PATH = "$libs;$env:PATH"

# The plugin manager splits PTLIBPLUGINDIR on ';' and walks each directory,
# including subdirectories. Codec plugins are under the OPAL build and device
# plugins under the PTLib build. A Visual Studio build nests each plugin in
# <family>/<Config>, so only that configuration's plugin DLLs are listed.
$pluginDirs = @()
$ptlibBuild = Join-Path $PSScriptRoot "..\opalvoip-ptlib\build"
foreach ($root in @(
        (Join-Path $build "plugins"),
        (Join-Path $ptlibBuild "plugins"))) {
    if (-not (Test-Path -LiteralPath $root)) {
        continue
    }
    Get-ChildItem -LiteralPath $root -Recurse -Filter "*_ptplugin.dll" -File | ForEach-Object {
        if ($Config -and $_.Directory.Name -ne $Config) {
            return
        }
        if ($pluginDirs -notcontains $_.DirectoryName) {
            $pluginDirs += $_.DirectoryName
        }
    }
}
if ($pluginDirs.Count -eq 0) {
    Write-Error "No plugin directories were found. Build the OPAL and PTLib plugins, then run this script again."
}
$env:PTLIBPLUGINDIR = ($pluginDirs -join ';')

& $exe @OpenPhoneArgs
exit $LASTEXITCODE
