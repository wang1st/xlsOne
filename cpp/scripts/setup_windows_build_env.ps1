#requires -Version 5.1
<#
.SYNOPSIS
    One-time check of the xlsOne Windows build environment.

.DESCRIPTION
    Verifies the toolchain required by package_windows_full.ps1:
      - Python packages: cmake, ninja (installed via pip if missing)
      - Qt 6.11.1 for Windows x64 MinGW (installed via the Qt online installer)
      - MinGW 13.1 (ships with Qt 6.11.1 as Tools\mingw1310_64)
      - WiX Toolset v3.14 binaries

    This script no longer installs Qt itself. Qt 6.11.1 (with its bundled
    MinGW 13.1) must be installed on the machine by the developer using the
    official Qt online installer (https://www.qt.io/download-qt-installer).
    The Qt5 / aqtinstall toolchain setup has been removed — the project is
    built with Qt 6 only.

.PARAMETER QtRoot
    Root directory where Qt 6.11.1 is installed.
    Default: C:\Qt

.PARAMETER WiXRoot
    Directory where WiX 3.x binaries are installed.
    Default: C:\Qt\Tools\wix314

.PARAMETER Force
    Reinstall WiX even if it already exists.

.EXAMPLE
    .\scripts\setup_windows_build_env.ps1
    .\scripts\setup_windows_build_env.ps1 -QtRoot "D:\Qt" -WiXRoot "D:\wix314" -Force
#>
param(
    [string]$QtRoot = "C:\Qt",
    [string]$WiXRoot = "C:\Qt\Tools\wix314",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$qtVersion = "6.11.1"
$qtDirName = "mingw_64"
$mingwDirName = "mingw1310_64"
$qtFullDir = Join-Path $QtRoot "$qtVersion\$qtDirName"
$mingwFullDir = Join-Path $QtRoot "Tools\$mingwDirName"
$wixUrl = "https://github.com/wixtoolset/wix3/releases/download/wix3141rtm/wix314-binaries.zip"

function Test-Command {
    param([string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) {
        Write-Host "[OK] $Name found: $($cmd.Source)" -ForegroundColor Green
        return $true
    }
    return $false
}

function Install-PythonTool {
    param([string]$Name)
    if (Test-Command $Name) { return }
    Write-Host "Installing $Name via pip..." -ForegroundColor Cyan
    & python -m pip install --upgrade $Name
    if ($LASTEXITCODE -ne 0) { throw "Failed to install $Name" }
}

Write-Host "=== xlsOne Windows Build Environment Setup ===" -ForegroundColor Cyan
Write-Host "Qt dir:        $qtFullDir"
Write-Host "MinGW dir:     $mingwFullDir"
Write-Host "WiX dir:       $WiXRoot"
Write-Host ""

# --- Python toolchain ---
if (-not (Test-Command python)) {
    throw "Python not found on PATH. Install Python 3.10+ and add it to PATH first."
}

Install-PythonTool "cmake"
Install-PythonTool "ninja"

# --- Qt 6.11.1 (preinstalled by developer) ---
$qmake = Join-Path $qtFullDir "bin" "qmake.exe"
if (Test-Path $qmake) {
    Write-Host "[OK] Qt $qtVersion found at $qtFullDir" -ForegroundColor Green
} else {
    Write-Host "MISSING: Qt $qtVersion at $qtFullDir" -ForegroundColor Red
    Write-Host "         Install it via the official Qt online installer, selecting:" -ForegroundColor Yellow
    Write-Host "           - Qt 6.11.1 -> MinGW 64-bit (mingw_64)" -ForegroundColor Yellow
    Write-Host "           - Qt 6.11.1 -> Developer and Designer Tools -> MinGW 13.1.0 (mingw1310_64)" -ForegroundColor Yellow
    Write-Host "         Download: https://www.qt.io/download-qt-installer" -ForegroundColor Yellow
}

# --- MinGW 13.1 (bundled with Qt 6.11.1) ---
$gcc = Join-Path $mingwFullDir "bin" "gcc.exe"
if (Test-Path $gcc) {
    Write-Host "[OK] MinGW 13.1 found at $mingwFullDir" -ForegroundColor Green
} else {
    Write-Host "MISSING: MinGW 13.1 at $mingwFullDir" -ForegroundColor Red
    Write-Host "         Install it as part of Qt 6.11.1 (Tools\mingw1310_64) via the Qt installer." -ForegroundColor Yellow
}

# --- WiX 3.14 ---
$candle = Join-Path $WiXRoot "candle.exe"
$light = Join-Path $WiXRoot "light.exe"
if ((Test-Path $candle) -and (Test-Path $light) -and -not $Force) {
    Write-Host "[OK] WiX already installed at $WiXRoot" -ForegroundColor Green
} else {
    Write-Host "Downloading WiX 3.14 binaries..." -ForegroundColor Cyan
    $zip = Join-Path $env:TEMP "wix314-binaries.zip"
    Invoke-WebRequest -Uri $wixUrl -OutFile $zip -UseBasicParsing

    if (Test-Path $WiXRoot) { Remove-Item -Recurse -Force $WiXRoot }
    New-Item -ItemType Directory -Force -Path $WiXRoot | Out-Null

    Write-Host "Extracting WiX to $WiXRoot ..." -ForegroundColor Cyan
    Expand-Archive -Path $zip -DestinationPath $WiXRoot -Force
    Remove-Item $zip
    Write-Host "[OK] WiX installed" -ForegroundColor Green
}

Write-Host ""
Write-Host "=== Setup complete ===" -ForegroundColor Green
Write-Host "You can now run: .\scripts\package_windows_full.ps1 -Domestic"
