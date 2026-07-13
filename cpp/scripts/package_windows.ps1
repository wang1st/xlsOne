param(
    [string]$Preset = "windows-release",
    [string]$QtBin = ""
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $Root "build-windows-release"

Push-Location $Root
try {
    cmake --preset $Preset
    cmake --build --preset $Preset
}
finally {
    Pop-Location
}

$Exe = Join-Path $BuildDir "app\xlsOneQt.exe"

# Strip symbol tables from the release binary to reduce size and hinder
# static analysis.  CMake also passes -s when XLSONE_OBFUSCATE is enabled.
$Strip = Get-Command strip -ErrorAction SilentlyContinue
if ($Strip -and (Test-Path $Exe)) {
    & $Strip.Source $Exe
}

if ($QtBin -ne "" -and (Test-Path $Exe)) {
    & (Join-Path $QtBin "windeployqt.exe") $Exe
}

cpack --config (Join-Path $BuildDir "CPackConfig.cmake") -G ZIP
