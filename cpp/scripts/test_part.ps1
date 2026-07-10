#requires -Version 5.1
<#
.SYNOPSIS
    鎵撳寘鐢熸垚 xlsOne Windows MSI 瀹夎鍖呫€?
.DESCRIPTION
    涓€閿瀯寤哄苟鐢熸垚 MSI 瀹夎鍖咃細
      1. 閰嶇疆骞舵瀯寤?C++/Qt 椤圭洰锛圕Make + Ninja锛?      2. 鑷姩鎵撳寘 Qt 杩愯鏃讹紙windeployqt锛?      3. 鐢熸垚 WiX MSI 瀹夎绋嬪簭锛堝惈寮€濮嬭彍鍗?妗岄潰蹇嵎鏂瑰紡 + 瀹夎瀹屾垚鍚姩閫夐」锛?
    鏀寔鍥藉唴鐗堬紙api.z-pulse.cn锛夊拰鍥介檯鐗堬紙api.xlsone.com锛夈€?
.PARAMETER QtRoot
    Qt MinGW 瀹夎鏍圭洰褰曘€傞粯璁よ嚜鍔ㄦ娴嬪父瑙佽矾寰勩€?
.PARAMETER WiXRoot
    WiX 3.x 宸ュ叿闆嗙洰褰曪紙鍚?candle.exe / light.exe锛夈€傞粯璁? C:\Qt\Tools\wix314

.PARAMETER MinguRoot
    MinGW 缂栬瘧鍣ㄧ洰褰曘€傞粯璁? C:\Qt\Tools\mingw1310_64

.PARAMeter ZlibRoot
    zlib 鐩綍锛堜粎 Qt5 闇€瑕侊級銆傞粯璁? C:\msys64\mingw64

.PARAMETER Domestic
    鏋勫缓鍥藉唴鐗堬紙婵€娲绘湇鍔″櫒鎸囧悜 api.z-pulse.cn锛?
.PARAMETER Clean
    娓呯悊鏋勫缓鐩綍鍚庨噸鏂版瀯寤?
.PARAMETER Sign
    浠ｇ爜绛惧悕锛堥渶瑕佽瘉涔︼級

.PARAMETER CertFile
    PFX 璇佷功鏂囦欢璺緞

.PARAMETER CertPassword
    PFX 璇佷功瀵嗙爜锛堝缓璁敤鐜鍙橀噺 XLSONE_CODESIGN_PASSWORD锛?
.PARAMETER CertSha1
    璇佷功瀛樺偍涓殑 SHA1 鎸囩汗锛堟浛浠?CertFile锛?
.EXAMPLE
    .\package_msi.ps1                              # 榛樿鏋勫缓鍥介檯鐗?    .\package_msi.ps1 -Domestic                   # 鏋勫缓鍥藉唴鐗?    .\package_msi.ps1 -Clean                      # 娓呯悊鍚庨噸鏂版瀯寤?    .\package_msi.ps1 -Domestic -Clean            # 鍥藉唴鐗堬紝瀹屽叏閲嶆柊鏋勫缓
    .\package_msi.ps1 -Sign -CertFile .\cert.pfx  # 绛惧悕鏋勫缓
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

# ===== 鑷姩妫€娴?Qt 璺緞 =====
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
    # 灏濊瘯浠?PATH 妫€娴?    $qmake = Get-Command qmake -ErrorAction SilentlyContinue
    if ($qmake) {
        $binDir = Split-Path $qmake.Source
        return Split-Path $binDir
    }
    throw "鏈壘鍒?Qt 瀹夎銆傝鎸囧畾 -QtRoot 鍙傛暟锛屾垨瀹夎 Qt MinGW 鐗堟湰銆?
}

if ($QtRoot -eq "") {
    $QtRoot = Find-QtRoot
}

# ===== 浠撳簱璺緞 =====
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ($BuildDir -eq "") {
    if ($Domestic) {
        $BuildDir = Join-Path (Join-Path $RepoRoot "cpp") "build-windows-cn-release"
    } else {
        $BuildDir = Join-Path (Join-Path $RepoRoot "cpp") "build-windows-release"
    }
}

# ===== 鐗堟湰淇℃伅 =====
$VersionFile = Join-Path (Join-Path $RepoRoot "cpp") "CMakeLists.txt"
$Version = "1.0.4"
if (Test-Path $VersionFile) {
    $m = Select-String -Path $VersionFile -Pattern 'VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)'
    if ($m) { $Version = $m.Matches[0].Groups[1].Value }
}

# ===== 杈撳嚭澶翠俊鎭?=====
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  xlsOne Windows MSI 鎵撳寘鑴氭湰" -ForegroundColor Cyan
Write-Host "  鐗堟湰: $Version" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

if ($Domestic) {
    Write-Host "[鍥藉唴鐗圿 婵€娲绘湇鍔″櫒: https://api.z-pulse.cn" -ForegroundColor Magenta
} else {
    Write-Host "[鍥介檯鐗圿 婵€娲绘湇鍔″櫒: https://api.xlsone.com" -ForegroundColor Cyan
}
Write-Host "Qt 璺緞:    $QtRoot"
Write-Host "WiX 璺緞:   $WiXRoot"
Write-Host "MinGW 璺緞: $MingwRoot"
Write-Host "鏋勫缓鐩綍:   $BuildDir"
Write-Host ""

