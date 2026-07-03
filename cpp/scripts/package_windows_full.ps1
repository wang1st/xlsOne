#requires -Version 5.1
<#
.SYNOPSIS
    Build and package xlsOne for Windows (Qt5 + MinGW + WiX).

.DESCRIPTION
    This script reproduces the Windows build on a clean machine:
      1. Validates required tools: CMake, Ninja, Qt5, WiX 3.x, MinGW compiler, zlib.
      2. Configures and builds the C++/Qt project with CMake.
      3. Bundles Qt runtime via windeployqt (integrated in CMake install step).
      4. Produces both a portable ZIP and a WiX MSI installer.

    Prerequisites (one-time setup):
      - CMake >= 3.22   (pip install cmake)
      - Ninja           (pip install ninja)
      - Qt 5.15.2 for Windows x64 MinGW 8.1:
            python -m aqt install-qt windows desktop 5.15.2 win64_mingw81 -O C:\Qt
      - WiX Toolset v3.11+ (used by CPack WIX generator):
            Download https://github.com/wixtoolset/wix3/releases/download/wix3141rtm/wix314-binaries.zip
            Extract to C:\Qt\Tools\wix314
      - MinGW-w64 compiler (the Qt5 binaries work with current MinGW-w64/GCC, e.g. MSYS2 UCRT64/MINGW64)
      - zlib static library (available in MSYS2: pacman -S mingw-w64-x86_64-zlib)

.PARAMETER QtRoot
    Root directory of Qt 5.15.2 MinGW installation.
    Default: C:\Qt\5.15.2\mingw81_64

.PARAMETER WiXRoot
    Directory containing WiX 3.x candle.exe and light.exe.
    Default: C:\Qt\Tools\wix314

.PARAMETER ZlibRoot
    Directory containing zlib headers and static library.
    Default: C:\msys64\mingw64

.PARAMETER Preset
    CMake build type. Default: Release

.PARAMETER BuildDir
    CMake build directory. Default: <repo>/cpp/build-windows-qt5-release

.PARAMETER Clean
    Remove the build directory before configuring to ensure a fully reproducible build.

.EXAMPLE
    .\scripts\package_windows_full.ps1
    .\scripts\package_windows_full.ps1 -QtRoot "D:\Qt\5.15.2\mingw81_64" -WiXRoot "D:\wix314"
    .\scripts\package_windows_full.ps1 -Clean
#>
param(
    [string]$QtRoot = "C:\Qt\5.15.2\mingw81_64",
    [string]$WiXRoot = "C:\Qt\Tools\wix314",
    [string]$ZlibRoot = "C:\msys64\mingw64",
    [string]$Preset = "Release",
    [string]$BuildDir = "",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ($BuildDir -eq "") {
    $BuildDir = Join-Path (Join-Path $RepoRoot "cpp") "build-windows-qt5-release"
}

function Test-Tool {
    param([string]$Name, [string]$Path)
    if (-not (Test-Path $Path)) {
        throw "Required tool not found: $Name at $Path"
    }
    Write-Host "[OK] $Name : $Path" -ForegroundColor Green
}

Write-Host "=== xlsOne Windows Packaging ===" -ForegroundColor Cyan
Write-Host "Qt root:   $QtRoot"
Write-Host "WiX root:  $WiXRoot"
Write-Host "zlib root: $ZlibRoot"
Write-Host "Build dir: $BuildDir"
Write-Host ""

# --- Validate prerequisites ---
Test-Tool "Qt5 qmake"        (Join-Path (Join-Path $QtRoot "bin") "qmake.exe")
Test-Tool "Qt5 windeployqt"  (Join-Path (Join-Path $QtRoot "bin") "windeployqt.exe")
Test-Tool "WiX candle"       (Join-Path $WiXRoot "candle.exe")
Test-Tool "WiX light"        (Join-Path $WiXRoot "light.exe")
Test-Tool "zlib header"      (Join-Path (Join-Path $ZlibRoot "include") "zlib.h")
Test-Tool "zlib library"     (Join-Path (Join-Path $ZlibRoot "lib") "libz.a")

# Ensure CMake/Ninja are on PATH.
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
$ninja = Get-Command ninja -ErrorAction SilentlyContinue
if (-not $cmake) { throw "cmake not found on PATH. Install: pip install cmake" }
if (-not $ninja) { throw "ninja not found on PATH. Install: pip install ninja" }
Write-Host "[OK] cmake: $($cmake.Source)" -ForegroundColor Green
Write-Host "[OK] ninja: $($ninja.Source)" -ForegroundColor Green

# Ensure a MinGW compiler is available.
$gcc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $gcc) { throw "gcc not found on PATH. Install MSYS2 MinGW-w64 and add it to PATH." }
Write-Host "[OK] gcc:   $($gcc.Source)" -ForegroundColor Green

# Ensure the requested Qt is found first by CMake and windeployqt.  This must
# precede CMake configure so that find_package(QT NAMES ...) picks up qmake
# from $QtRoot instead of any other Qt on the system PATH.
$env:PATH = "$QtRoot\bin;$env:PATH"

# WiX must be on PATH for CPack to find candle/light.
$env:PATH = "$WiXRoot;$env:PATH"

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning build directory: $BuildDir" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

# --- Configure ---
Write-Host "`n--- Configuring CMake ---" -ForegroundColor Cyan
$qtCmakeDir = Join-Path (Join-Path (Join-Path $QtRoot "lib") "cmake") "Qt5"
$cmakeArgs = @(
    "-S", (Join-Path $RepoRoot "cpp"),
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$Preset",
    "-DCMAKE_PREFIX_PATH=$QtRoot;$ZlibRoot",
    "-DQt5_DIR=$qtCmakeDir",
    "-DZLIB_LIBRARY=$(Join-Path (Join-Path $ZlibRoot 'lib') 'libz.a')",
    "-DZLIB_INCLUDE_DIR=$(Join-Path $ZlibRoot 'include')"
)
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

# --- Build ---
Write-Host "`n--- Building xlsone_app ---" -ForegroundColor Cyan
& cmake --build $BuildDir --target xlsone_app --config $Preset
if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }

# --- Package ---
Write-Host "`n--- Packaging ---" -ForegroundColor Cyan

# Ensure Qt tools remain discoverable by windeployqt during CPack install steps.
$env:PATH = "$QtRoot\bin;$env:PATH"

& cpack --config (Join-Path $BuildDir "CPackConfig.cmake") -B $BuildDir -G WIX
if ($LASTEXITCODE -ne 0) { throw "CPack WIX failed" }

$env:PATH = "$QtRoot\bin;$env:PATH"
& cpack --config (Join-Path $BuildDir "CPackConfig.cmake") -B $BuildDir -G ZIP
if ($LASTEXITCODE -ne 0) { throw "CPack ZIP failed" }

# Derive the exact package file name from the CPack config so the script
# stays correct when CMakeLists.txt version is bumped.
$cpackConfig = Join-Path $BuildDir "CPackConfig.cmake"
$pkgBase = "xlsone-1.0.3-windows-amd64"
if (Test-Path $cpackConfig) {
    $m = Select-String -Path $cpackConfig -Pattern 'set\(CPACK_PACKAGE_FILE_NAME "([^"]+)"\)'
    if ($m) { $pkgBase = $m.Matches[0].Groups[1].Value }
}
$msi = Join-Path $BuildDir "$pkgBase.msi"
$zip = Join-Path $BuildDir "$pkgBase.zip"

Write-Host "`n=== Packaging complete ===" -ForegroundColor Green
Write-Host "MSI: $msi"
Write-Host "ZIP: $zip"

if (Test-Path $msi) {
    $msiSize = (Get-Item $msi).Length / 1MB
    Write-Host ("MSI size: {0:F1} MB" -f $msiSize)
}
if (Test-Path $zip) {
    $zipSize = (Get-Item $zip).Length / 1MB
    Write-Host ("ZIP size: {0:F1} MB" -f $zipSize)
}
