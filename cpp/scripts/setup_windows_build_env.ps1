#requires -Version 5.1
<#
.SYNOPSIS
    One-time setup of the xlsOne Windows build environment.

.DESCRIPTION
    Installs the toolchain required by package_windows_full.ps1:
      - Python packages: aqtinstall, cmake, ninja
      - Qt 5.15.2 for Windows x64 MinGW 8.1 (via aqtinstall)
      - WiX Toolset v3.14 binaries
    Existing installations are left untouched unless -Force is passed.

    You still need a MinGW-w64 compiler and zlib.  The easiest source on
    Windows is MSYS2:
        pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-zlib
    Then add C:\msys64\mingw64\bin to your system PATH.

.PARAMETER QtRoot
    Root directory where Qt will be installed.
    Default: C:\Qt

.PARAMETER WiXRoot
    Directory where WiX 3.x binaries will be extracted.
    Default: C:\Qt\Tools\wix314

.PARAMETER Force
    Reinstall Qt and/or WiX even if they already exist.

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

$wixUrl = "https://github.com/wixtoolset/wix3/releases/download/wix3141rtm/wix314-binaries.zip"
$qtVersion = "5.15.2"
# aqtinstall arch name vs. the actual directory it creates differ.
$qtAqtArch = "win64_mingw81"
$qtDirName = "mingw81_64"
$qtFullDir = Join-Path $QtRoot "$qtVersion\$qtDirName"

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
Write-Host "Qt install dir: $qtFullDir"
Write-Host "WiX install dir: $WiXRoot"
Write-Host ""

# --- Python toolchain ---
if (-not (Test-Command python)) {
    throw "Python not found on PATH. Install Python 3.10+ and add it to PATH first."
}

Install-PythonTool "aqtinstall"
Install-PythonTool "cmake"
Install-PythonTool "ninja"

# --- Qt 5.15.2 ---
$qmake = Join-Path $qtFullDir "bin" "qmake.exe"
if ((Test-Path $qmake) -and -not $Force) {
    Write-Host "[OK] Qt $qtVersion already installed at $qtFullDir" -ForegroundColor Green
} else {
    Write-Host "Installing Qt $qtVersion $qtAqtArch to $QtRoot ..." -ForegroundColor Cyan
    & python -m aqt install-qt windows desktop $qtVersion $qtAqtArch -O $QtRoot
    if ($LASTEXITCODE -ne 0) { throw "aqt install-qt failed" }
    Write-Host "[OK] Qt installed" -ForegroundColor Green
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

# --- MinGW / zlib hint ---
Write-Host ""
Write-Host "=== Manual prerequisites ===" -ForegroundColor Yellow
$gccCmd = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $gccCmd) {
    Write-Host "MISSING: gcc. Install MSYS2 MinGW-w64 and add C:\msys64\mingw64\bin to PATH." -ForegroundColor Red
} else {
    Write-Host "[OK] gcc: $($gccCmd.Source)" -ForegroundColor Green
}

$zlibHeader = "C:\msys64\mingw64\include\zlib.h"
$zlibLib = "C:\msys64\mingw64\lib\libz.a"
if (-not (Test-Path $zlibHeader) -or -not (Test-Path $zlibLib)) {
    Write-Host "MISSING: zlib. In MSYS2 run: pacman -S mingw-w64-x86_64-zlib" -ForegroundColor Red
} else {
    Write-Host "[OK] zlib found" -ForegroundColor Green
}

Write-Host ""
Write-Host "=== Setup complete ===" -ForegroundColor Green
Write-Host "You can now run: .\scripts\package_windows_full.ps1"
