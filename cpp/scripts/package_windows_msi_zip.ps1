#requires -Version 5.1
<#
.SYNOPSIS
    Build and package xlsOne for Windows (Qt5/Qt6 + MinGW + WiX).

.DESCRIPTION
    This script reproduces the Windows build on a clean machine:
      1. Validates required tools: CMake, Ninja, Qt, WiX 3.x, MinGW compiler, (zlib for Qt5).
      2. Configures and builds the C++/Qt project with CMake.
      3. Bundles Qt runtime via windeployqt (integrated in CMake install step).
      4. Produces both a portable ZIP and a WiX MSI installer.

    Supports both Qt5 and Qt6: the Qt major version is auto-detected from qmake.
    On Qt6 the bundled Qt6::ZlibPrivate is used (no external zlib required); on Qt5
    an external zlib static library is required.

    Prerequisites (one-time setup):
      - CMake >= 3.22   (pip install cmake)
      - Ninja           (pip install ninja)
      - Qt 6.x for Windows x64 MinGW (e.g. 6.11.1 + MinGW 13.1.0 64-bit from the Qt online installer)
      - WiX Toolset v3.11+ (used by CPack WIX generator):
            Download https://github.com/wixtoolset/wix3/releases/download/wix3141rtm/wix314-binaries.zip
            Extract to C:\Qt\Tools\wix314
      - MinGW-w64 compiler matching the Qt build (e.g. C:\Qt\Tools\mingw1310_64)

.PARAMETER QtRoot
    Root directory of the Qt MinGW installation (auto-detects Qt5 vs Qt6).
    Default: C:\Qt\6.11.1\mingw_64

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

.PARAMETER International
    Build the international edition. This points the activation endpoint at
    https://api.xlsone.com instead of the default domestic https://api.z-pulse.cn.
    Output goes to cpp/build-windows-release.

    默认为国内版(api.z-pulse.cn)，无需传此开关。

.PARAMETER Obfuscate
    Enable release hardening and embed the Ed25519 license verification public
    key from XLSONE_LICENSE_PUBLIC_KEY. The variable must contain exactly 64 hex
    characters. No private key is needed by a client build.

.PARAMETER Sign
    Code-sign the built xlsOneQt.exe and the resulting .msi with Authenticode
    (SHA256 + RFC3161 timestamp). Requires a code-signing certificate. Without
    this switch the package is produced UNSIGNED -- Windows SmartScreen will
    warn/block end users. The certificate is supplied via -CertFile (PFX) or
    -CertSha1 (thumbprint in the local cert store); signtool is auto-discovered
    from the Windows SDK, or pass -SignTool <path>.

.PARAMETER SignTool
    Path to signtool.exe. Default: auto-discovered from PATH / Windows SDK.

.PARAMETER CertFile
    Path to a PFX/P12 code-signing certificate used with -Sign.

.PARAMETER CertPassword
    Password for -CertFile. If omitted, falls back to $env:XLSONE_CODESIGN_PASSWORD.
    (Prefer the env var over the command line to avoid leaking the password into
    the process list / shell history.)

.PARAMETER CertSha1
    SHA1 thumbprint of a code-signing certificate already installed in the
    current user's / local machine cert store. Alternative to -CertFile.

.PARAMETER TimestampServer
    RFC3161 timestamp authority URL. Default: http://timestamp.digicert.com

.EXAMPLE
    .\scripts\package_windows_msi_zip.ps1
    .\scripts\package_windows_msi_zip.ps1 -QtRoot "D:\Qt\6.11.1\mingw_64" -MingwRoot "D:\Qt\Tools\mingw1310_64" -WiXRoot "D:\wix314"
    .\scripts\package_windows_msi_zip.ps1 -Clean
    .\scripts\package_windows_msi_zip.ps1 -International    # 国际版（api.xlsone.com）
    # 代码签名（PFX）:
    .\scripts\package_windows_msi_zip.ps1 -Sign -CertFile .\codesign.pfx -CertPassword ***
    # 代码签名（证书存储 thumbprint）:
    .\scripts\package_windows_msi_zip.ps1 -Sign -CertSha1 A1B2C3D4...
#>
param(
    [string]$QtRoot = "C:\Qt\6.11.1\mingw_64",
    [string]$WiXRoot = "C:\Qt\Tools\wix314",
    [string]$MingwRoot = "C:\Qt\Tools\mingw1310_64",
    [string]$ZlibRoot = "C:\msys64\mingw64",
    [string]$Preset = "Release",
    [string]$BuildDir = "",
    [switch]$Clean,
    [switch]$International,
    [switch]$Obfuscate,
    [switch]$Sign,
    [string]$SignTool = "",
    [string]$CertFile = "",
    [string]$CertPassword = "",
    [string]$CertSha1 = "",
    [string]$TimestampServer = "http://timestamp.digicert.com"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ($BuildDir -eq "") {
    if ($International) {
        $BuildDir = Join-Path (Join-Path $RepoRoot "cpp") "build-windows-release"
    } else {
        $BuildDir = Join-Path (Join-Path $RepoRoot "cpp") "build-windows-cn-release"
    }
}

if ($International) {
    Write-Host "[International] Activation endpoint baked in: https://api.xlsone.com" -ForegroundColor Cyan
} else {
    Write-Host "[Domestic] Activation endpoint baked in: https://api.z-pulse.cn" -ForegroundColor Magenta
}

function Test-Tool {
    param([string]$Name, [string]$Path)
    if (-not (Test-Path $Path)) {
        throw "Required tool not found: $Name at $Path"
    }
    Write-Host "[OK] $Name : $Path" -ForegroundColor Green
}

# --- Code signing helpers (Authenticode) ---
function Find-SignTool {
    param([string]$Preferred)
    if ($Preferred -and (Test-Path $Preferred)) { return $Preferred }
    $st = Get-Command signtool -ErrorAction SilentlyContinue
    if ($st) { return $st.Source }
    $sdkRoots = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin",
        "${env:ProgramFiles}\Windows Kits\10\bin"
    )
    foreach ($root in $sdkRoots) {
        if (Test-Path $root) {
            $cand = Get-ChildItem -Path $root -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
                Select-Object -First 1
            if ($cand) { return $cand.FullName }
        }
    }
    return $null
}

function Invoke-Sign {
    param([string]$FilePath, [string]$SignToolPath)
    if (-not (Test-Path $FilePath)) { throw "File to sign not found: $FilePath" }
    $ts = if ($TimestampServer) { $TimestampServer } else { "http://timestamp.digicert.com" }
    $args = @("sign", "/fd", "SHA256", "/tr", $ts, "/td", "SHA256")
    if ($CertSha1) {
        $args += @("/sha1", $CertSha1)
    } elseif ($CertFile) {
        $args += @("/f", $CertFile)
        $pw = if ($CertPassword) { $CertPassword } else { $env:XLSONE_CODESIGN_PASSWORD }
        if ($pw) { $args += @("/p", $pw) }
    } else {
        throw "Code signing requires -CertFile (PFX) or -CertSha1 (store thumbprint)."
    }
    $args += @("/v", $FilePath)
    # Never echo the argument list: it may contain the PFX password after /p.
    Write-Host "[Sign] Signing: $FilePath" -ForegroundColor Magenta
    & $SignToolPath @args
    if ($LASTEXITCODE -ne 0) { throw "Signing failed: $FilePath" }
    Write-Host "[OK] signed: $FilePath" -ForegroundColor Green
}

function Assert-SignConfig {
    if (-not $CertFile -and -not $CertSha1) {
        throw "Code signing (-Sign) requires -CertFile (PFX) or -CertSha1 (store thumbprint)."
    }
    if ($CertFile -and -not (Test-Path $CertFile)) {
        throw "Certificate file not found: $CertFile"
    }
}

$resolvedSignTool = $null
if (-not $Sign) {
    Write-Host "[WARN] Code signing is OFF -- the .exe/.msi will be UNSIGNED and Windows SmartScreen will warn/block users. Pass -Sign with a cert to sign." -ForegroundColor Yellow
} else {
    Assert-SignConfig
    $resolvedSignTool = Find-SignTool $SignTool
    if (-not $resolvedSignTool) {
        throw "signtool not found. Install the Windows SDK or pass -SignTool <path>."
    }
    Write-Host "[Sign] Code signing ENABLED (SHA256 + RFC3161 timestamp: $TimestampServer)" -ForegroundColor Magenta
}

if ($Obfuscate) {
    if ($env:XLSONE_LICENSE_PUBLIC_KEY -notmatch '^[0-9a-fA-F]{64}$') {
        throw "-Obfuscate requires XLSONE_LICENSE_PUBLIC_KEY to be exactly 64 hexadecimal characters."
    }
    Write-Host "[Security] Release obfuscation ENABLED with the configured Ed25519 public key." -ForegroundColor Magenta
}

Write-Host "=== xlsOne Windows Packaging ===" -ForegroundColor Cyan
Write-Host "Qt root:   $QtRoot"
Write-Host "WiX root:  $WiXRoot"
Write-Host "MinGW root: $MingwRoot"
Write-Host "zlib root: $ZlibRoot"
Write-Host "Build dir: $BuildDir"
Write-Host ""

# Prepend the MinGW compiler to PATH up front so the gcc/g++ prerequisite
# check (below) and CMake both resolve to the toolchain matching this Qt build.
$env:PATH = "$MingwRoot\bin;$env:PATH"

# --- Detect Qt major version (5 vs 6) ---
$qtVersion = & (Join-Path (Join-Path $QtRoot "bin") "qmake.exe") -query QT_VERSION 2>$null
if (-not $qtVersion) { throw "qmake not found or failed under $QtRoot" }
$qtMajor = ($qtVersion.Split('.'))[0]
Write-Host "Detected Qt $qtVersion (major $qtMajor)" -ForegroundColor Cyan

# --- Validate prerequisites ---
Test-Tool "Qt qmake"        (Join-Path (Join-Path $QtRoot "bin") "qmake.exe")
Test-Tool "Qt windeployqt"  (Join-Path (Join-Path $QtRoot "bin") "windeployqt.exe")
Test-Tool "WiX candle"       (Join-Path $WiXRoot "candle.exe")
Test-Tool "WiX light"        (Join-Path $WiXRoot "light.exe")
if ($qtMajor -eq "5") {
    # Qt5 needs an external zlib static library.
    Test-Tool "zlib header"  (Join-Path (Join-Path $ZlibRoot "include") "zlib.h")
    Test-Tool "zlib library" (Join-Path (Join-Path $ZlibRoot "lib") "libz.a")
} else {
    Write-Host "[SKIP] external zlib not required for Qt6 (uses Qt6::ZlibPrivate)" -ForegroundColor DarkGray
}

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
# from $QtRoot instead of any other Qt on the system PATH.  Also prepend the
# MinGW compiler so gcc/g++ resolve to the toolchain matching this Qt build.
$env:PATH = "$MingwRoot\bin;$QtRoot\bin;$env:PATH"

# WiX must be on PATH for CPack to find candle/light.
$env:PATH = "$WiXRoot;$env:PATH"

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning build directory: $BuildDir" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

# --- Configure ---
Write-Host "`n--- Configuring CMake ---" -ForegroundColor Cyan
$qtCmakeDir = Join-Path (Join-Path $QtRoot "lib") "cmake"
$qtCmakeDir = Join-Path $qtCmakeDir "Qt$qtMajor"
$cmakeArgs = @(
    "-S", (Join-Path $RepoRoot "cpp"),
    "-B", $BuildDir,
    "-G", "Ninja",
    # Pin the ninja validated above; otherwise a stale CMAKE_MAKE_PROGRAM in
    # CMakeCache.txt (e.g. a removed pip ninja shim) breaks the configure.
    "-DCMAKE_MAKE_PROGRAM=$($ninja.Source -replace '\\','/')",
    "-DCMAKE_BUILD_TYPE=$Preset",
    "-DCMAKE_PREFIX_PATH=$QtRoot",
    "-DQt${qtMajor}_DIR=$qtCmakeDir"
)
if ($qtMajor -eq "5") {
    # Qt5: pass the external zlib location explicitly.
    $cmakeArgs += "-DZLIB_LIBRARY=$(Join-Path (Join-Path $ZlibRoot 'lib') 'libz.a')"
    $cmakeArgs += "-DZLIB_INCLUDE_DIR=$(Join-Path $ZlibRoot 'include')"
}
if (-not $International) {
    # Point the compiled-in activation base URL at the domestic backend (default).
    # Mirrors the windows-cn-release CMake preset so the two paths stay in sync.
    $cmakeArgs += "-DXLSONE_ACTIVATION_BASE_URL=https://api.z-pulse.cn"
}
if ($Obfuscate) {
    $cmakeArgs += "-DXLSONE_OBFUSCATE=ON"
    $cmakeArgs += "-DXLSONE_LICENSE_PUBLIC_KEY=$env:XLSONE_LICENSE_PUBLIC_KEY"
} else {
    # Always override a stale CMakeCache value when reusing a build directory.
    $cmakeArgs += "-DXLSONE_OBFUSCATE=OFF"
}
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

# --- Build ---
Write-Host "`n--- Building xlsone_app ---" -ForegroundColor Cyan
& cmake --build $BuildDir --target xlsone_app --config $Preset
if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }

# Build and run the core regression suite before stripping or packaging.  This
# catches startup failures such as recursive persisted-license validation while
# the executable still has symbols that make failures diagnosable.
Write-Host "`n--- Running core tests ---" -ForegroundColor Cyan
& cmake --build $BuildDir --target xlsone_core_tests --config $Preset
if ($LASTEXITCODE -ne 0) { throw "Core test build failed" }

& ctest --test-dir $BuildDir -C $Preset --output-on-failure -R '^xlsone_core_tests$'
if ($LASTEXITCODE -ne 0) { throw "Core tests failed" }

# --- Strip symbols ---
Write-Host "`n--- Stripping xlsOneQt.exe ---" -ForegroundColor Cyan
$exePath = Join-Path $BuildDir "app\xlsOneQt.exe"
$strip = Get-Command strip -ErrorAction SilentlyContinue
if ($strip -and (Test-Path $exePath)) {
    & $strip.Source $exePath
    Write-Host "[OK] stripped: $exePath" -ForegroundColor Green
} elseif (-not (Test-Path $exePath)) {
    Write-Host "[WARN] xlsOneQt.exe not found at $exePath; skipping strip" -ForegroundColor Yellow
} else {
    Write-Host "[WARN] strip tool not found on PATH; skipping" -ForegroundColor Yellow
}

# Sign the executable before CPack stages it so both the MSI and portable ZIP
# contain the signed binary.
if ($Sign) {
    Invoke-Sign $exePath $resolvedSignTool
}

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
$pkgBase = "xlsone-1.0.4-windows-amd64"
if (Test-Path $cpackConfig) {
    $m = Select-String -Path $cpackConfig -Pattern 'set\(CPACK_PACKAGE_FILE_NAME "([^"]+)"\)'
    if ($m) { $pkgBase = $m.Matches[0].Groups[1].Value }
}
$msi = Join-Path $BuildDir "$pkgBase.msi"
$zip = Join-Path $BuildDir "$pkgBase.zip"

# Sign the MSI container (the embedded EXE was already signed before packaging).
if ($Sign -and (Test-Path $msi)) {
    Invoke-Sign $msi $resolvedSignTool
}

# Treat package contents as part of the build contract. This administratively
# extracts the MSI, expands the ZIP, checks the Qt/MinGW runtime and plugin
# layout, audits transitive DLL dependencies, and briefly launches both copies.
$packageVerifier = Join-Path $PSScriptRoot "verify_windows_packages.ps1"
$objdump = Join-Path (Join-Path $MingwRoot "bin") "objdump.exe"
& $packageVerifier -MsiPath $msi -ZipPath $zip -ObjdumpPath $objdump -QtMajor ([int]$qtMajor)
if ($LASTEXITCODE -ne 0) { throw "Windows package verification failed" }

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

# --- Collect artifacts into .build/ so scripts/deploy/deploy.sh can find them ---
$artifactDir = Join-Path $RepoRoot ".build"
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
foreach ($pkg in @($msi, $zip)) {
    if (Test-Path $pkg) {
        Copy-Item -Force $pkg (Join-Path $artifactDir (Split-Path $pkg -Leaf))
        Write-Host "[OK] collected: .build\$(Split-Path $pkg -Leaf)" -ForegroundColor Green
    }
}
