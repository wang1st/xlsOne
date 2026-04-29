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
if ($QtBin -ne "" -and (Test-Path $Exe)) {
    & (Join-Path $QtBin "windeployqt.exe") $Exe
}

cpack --config (Join-Path $BuildDir "CPackConfig.cmake") -G ZIP
