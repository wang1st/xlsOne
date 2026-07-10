#requires -Version 5.1
<#
.SYNOPSIS
    打包生成 xlsOne Windows MSI 安装包。

.DESCRIPTION
    一键构建并生成 MSI 安装包：
      1. 配置并构建 C++/Qt 项目（CMake + Ninja）
      2. 自动打包 Qt 运行时（windeployqt）
      3. 生成 WiX MSI 安装程序（含开始菜单/桌面快捷方式 + 安装完成启动选项）

    支持国内版（api.z-pulse.cn）和国际版（api.xlsone.com）。

.PARAMETER QtRoot
    Qt MinGW 安装根目录。默认自动检测常见路径。

.PARAMETER WiXRoot
    WiX 3.x 工具集目录（含 candle.exe / light.exe）。默认: C:\Qt\Tools\wix314

.PARAMETER MinguRoot
    MinGW 编译器目录。默认: C:\Qt\Tools\mingw1310_64

.PARAMeter ZlibRoot
    zlib 目录（仅 Qt5 需要）。默认: C:\msys64\mingw64

.PARAMETER Domestic
    构建国内版（激活服务器指向 api.z-pulse.cn）

.PARAMETER Clean
    清理构建目录后重新构建

.PARAMETER Sign
    代码签名（需要证书）

.PARAMETER CertFile
    PFX 证书文件路径

.PARAMETER CertPassword
    PFX 证书密码（建议用环境变量 XLSONE_CODESIGN_PASSWORD）

.PARAMETER CertSha1
    证书存储中的 SHA1 指纹（替代 CertFile）

.EXAMPLE
    .\package_msi.ps1                              # 默认构建国际版
    .\package_msi.ps1 -Domestic                   # 构建国内版
    .\package_msi.ps1 -Clean                      # 清理后重新构建
    .\package_msi.ps1 -Domestic -Clean            # 国内版，完全重新构建
    .\package_msi.ps1 -Sign -CertFile .\cert.pfx  # 签名构建
#>
param(
    [string]$QtRoot = "",
    [string]$WiXRoot = "C:\Qt\Tools\wix314",
    [string]$MingwRoot = "C:\Qt\Tools\mingw1310_64",
    [string]$ZlibRoot = "C:\msys64\mingw64",
    [string]$Preset = "Release",
    [string]$BuildDir = "",
    [switch]$Clean,
    [switch]$Domestic,
    [switch]$Sign,
    [string]$SignTool = "",
    [string]$CertFile = "",
    [string]$CertPassword = "",
    [string]$CertSha1 = "",
    [string]$TimestampServer = "http://timestamp.digicert.com"
)

$ErrorActionPreference = "Stop"

# ===== 自动检测 Qt 路径 =====
function Find-QtRoot {
    $candidates = @(
        "C:\Qt\6.11.1\mingw_64",
        "C:\Qt\6.8.3\mingw_64",
        "C:\Qt\6.7.3\mingw_64",
        "C:\Qt\6.6.3\mingw_64",
        "C:\Qt\6.5.3\mingw_64",
        "C:\Qt\5.15.2\mingw81_64",
        "C:\Qt\5.15.16\mingw_64"
    )
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c "bin\qmake.exe")) {
            return $c
        }
    }
    # 尝试从 PATH 检测
    $qmake = Get-Command qmake -ErrorAction SilentlyContinue
    if ($qmake) {
        $binDir = Split-Path $qmake.Source
        return Split-Path $binDir
    }
    throw "未找到 Qt 安装。请指定 -QtRoot 参数，或安装 Qt MinGW 版本。"
}

if ($QtRoot -eq "") {
    $QtRoot = Find-QtRoot
}

# ===== 仓库路径 =====
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ($BuildDir -eq "") {
    if ($Domestic) {
        $BuildDir = Join-Path (Join-Path $RepoRoot "cpp") "build-windows-cn-release"
    } else {
        $BuildDir = Join-Path (Join-Path $RepoRoot "cpp") "build-windows-release"
    }
}

# ===== 版本信息 =====
$VersionFile = Join-Path (Join-Path $RepoRoot "cpp") "CMakeLists.txt"
$Version = "1.0.4"
if (Test-Path $VersionFile) {
    $m = Select-String -Path $VersionFile -Pattern 'VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)'
    if ($m) { $Version = $m.Matches[0].Groups[1].Value }
}

# ===== 输出头信息 =====
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  xlsOne Windows MSI 打包脚本" -ForegroundColor Cyan
Write-Host "  版本: $Version" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

if ($Domestic) {
    Write-Host "[国内版] 激活服务器: https://api.z-pulse.cn" -ForegroundColor Magenta
} else {
    Write-Host "[国际版] 激活服务器: https://api.xlsone.com" -ForegroundColor Cyan
}
Write-Host "Qt 路径:    $QtRoot"
Write-Host "WiX 路径:   $WiXRoot"
Write-Host "MinGW 路径: $MingwRoot"
Write-Host "构建目录:   $BuildDir"
Write-Host ""

if (-not $Sign) {
    Write-Host "[警告] 未启用代码签名，生成的安装包将无数字签名" -ForegroundColor Yellow
} else {
    Write-Host "[签名] 已启用代码签名" -ForegroundColor Green
}
Write-Host ""

# ===== 检测 Qt 版本 =====
$qmakePath = Join-Path (Join-Path $QtRoot "bin") "qmake.exe"
if (-not (Test-Path $qmakePath)) {
    throw "qmake 未找到: $qmakePath"
}
$qtVersion = & $qmakePath -query QT_VERSION 2>$null
$qtMajor = ($qtVersion.Split('.'))[0]
Write-Host "Qt 版本: $qtVersion (major $qtMajor)" -ForegroundColor Green

# ===== 检查必需工具 =====
function Test-Tool {
    param([string]$Name, [string]$Path)
    if (-not (Test-Path $Path)) {
        throw "缺少必需工具: $Name ($Path)"
    }
    Write-Host "[OK] $Name" -ForegroundColor Green
}

Test-Tool "WiX candle" (Join-Path $WiXRoot "candle.exe")
Test-Tool "WiX light"  (Join-Path $WiXRoot "light.exe")
Test-Tool "Qt qmake"   $qmakePath

if ($qtMajor -eq "5") {
    Test-Tool "zlib header"  (Join-Path (Join-Path $ZlibRoot "include") "zlib.h")
}

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
$ninja = Get-Command ninja -ErrorAction SilentlyContinue
if (-not $cmake) { throw "cmake 未找到。安装: pip install cmake" }
if (-not $ninja) { throw "ninja 未找到。安装: pip install ninja" }
Write-Host "[OK] cmake + ninja" -ForegroundColor Green

# 确保 MinGW 在 PATH 最前面
$env:PATH = "$MingwRoot\bin;$QtRoot\bin;$WiXRoot;$env:PATH"

# ===== 代码签名辅助函数 =====
function Find-SignTool {
    if ($SignTool -and (Test-Path $SignTool)) { return $SignTool }
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
    if (-not (Test-Path $FilePath)) { return }
    $args = @("sign", "/fd", "SHA256", "/tr", $TimestampServer, "/td", "SHA256")
    if ($CertSha1) {
        $args += @("/sha1", $CertSha1)
    } elseif ($CertFile) {
        $args += @("/f", $CertFile)
        $pw = if ($CertPassword) { $CertPassword } else { $env:XLSONE_CODESIGN_PASSWORD }
        if ($pw) { $args += @("/p", $pw) }
    }
    $args += @("/v", $FilePath)
    Write-Host "[签名] $FilePath" -ForegroundColor Magenta
    & $SignToolPath @args
    if ($LASTEXITCODE -ne 0) { Write-Host "[警告] 签名失败: $FilePath" -ForegroundColor Yellow }
    else { Write-Host "[OK] 已签名: $FilePath" -ForegroundColor Green }
}

# ===== 清理构建目录 =====
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "清理构建目录..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

# ===== CMake 配置 =====
Write-Host ""
Write-Host "--- CMake 配置 ---" -ForegroundColor Cyan

$qtCmakeDir = Join-Path (Join-Path $QtRoot "lib") "cmake"
$qtCmakeDir = Join-Path $qtCmakeDir "Qt$qtMajor"

$cmakeArgs = @(
    "-S", (Join-Path $RepoRoot "cpp"),
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$Preset",
    "-DCMAKE_PREFIX_PATH=$QtRoot",
    "-DQt${qtMajor}_DIR=$qtCmakeDir"
)

if ($qtMajor -eq "5") {
    $cmakeArgs += "-DZLIB_LIBRARY=$(Join-Path (Join-Path $ZlibRoot 'lib') 'libz.a')"
    $cmakeArgs += "-DZLIB_INCLUDE_DIR=$(Join-Path $ZlibRoot 'include')"
}
if ($Domestic) {
    $cmakeArgs += "-DXLSONE_ACTIVATION_BASE_URL=https://api.z-pulse.cn"
}

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败" }

# ===== 构建 =====
Write-Host ""
Write-Host "--- 编译项目 ---" -ForegroundColor Cyan
& cmake --build $BuildDir --target xlsone_app --config $Preset
if ($LASTEXITCODE -ne 0) { throw "编译失败" }

# ===== 代码签名（可执行文件） =====
if ($Sign) {
    $exePath = Join-Path $BuildDir "app\xlsone_app.exe"
    if (Test-Path $exePath) {
        $stPath = Find-SignTool
        if ($stPath) { Invoke-Sign $exePath $stPath }
    }
}

# ===== 打包 MSI =====
Write-Host ""
Write-Host "--- 生成 MSI 安装包 ---" -ForegroundColor Cyan

& cpack --config (Join-Path $BuildDir "CPackConfig.cmake") -B $BuildDir -G WIX
if ($LASTEXITCODE -ne 0) { throw "MSI 打包失败" }

# ===== 获取输出文件名 =====
$cpackConfig = Join-Path $BuildDir "CPackConfig.cmake"
$pkgBase = "xlsone-$Version-windows-amd64"
if (Test-Path $cpackConfig) {
    $m = Select-String -Path $cpackConfig -Pattern 'set\(CPACK_PACKAGE_FILE_NAME "([^"]+)"\)'
    if ($m) { $pkgBase = $m.Matches[0].Groups[1].Value }
}
$msi = Join-Path $BuildDir "$pkgBase.msi"

# ===== 代码签名（MSI） =====
if ($Sign -and (Test-Path $msi)) {
    $stPath = Find-SignTool
    if ($stPath) { Invoke-Sign $msi $stPath }
}

# ===== 完成 =====
Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  MSI 打包完成!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""

if (Test-Path $msi) {
    $msiSize = (Get-Item $msi).Length / 1MB
    Write-Host "输出文件: $msi" -ForegroundColor Cyan
    Write-Host ("文件大小: {0:F1} MB" -f $msiSize) -ForegroundColor Cyan
} else {
    Write-Host "[错误] 未找到生成的 MSI 文件" -ForegroundColor Red
    Get-ChildItem $BuildDir -Filter "*.msi" | ForEach-Object {
        Write-Host "  找到: $($_.FullName)" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "安装包特性:" -ForegroundColor Cyan
Write-Host "  - 开始菜单快捷方式" -ForegroundColor Gray
Write-Host "  - 桌面快捷方式（可选）" -ForegroundColor Gray
Write-Host "  - 安装完成自动启动选项（可选）" -ForegroundColor Gray
if ($Domestic) {
    Write-Host "  - 国内版激活（api.z-pulse.cn）" -ForegroundColor Gray
} else {
    Write-Host "  - 国际版激活（api.xlsone.com）" -ForegroundColor Gray
}
