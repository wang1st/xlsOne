#Requires -Version 5.1
<#
.SYNOPSIS
    xlsOne 一键部署脚本（PowerShell 版本）

.DESCRIPTION
    交互式选择平台、版本，输入 z-pulse.cn root 密码后自动：
      1. 更新 cpp/CMakeLists.txt、site/api/version.json、站点页面缓存戳
      2. 构建（Linux）或定位（Windows/macOS）安装包
      3. 上传安装包与站点文件到服务器

    用法：.\scripts\deploy\deploy.ps1
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

# -----------------------------------------------------------------------------
# 颜色与 UI
# -----------------------------------------------------------------------------
$ScriptDir = Split-Path -Parent $PSCommandPath
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
$SiteDir = Join-Path $ProjectRoot 'site'
$CppDir = Join-Path $ProjectRoot 'cpp'

$Server = 'z-pulse.cn'
$ServerUser = 'root'
$RemoteRoot = '/var/www/z-pulse.cn'
$RemoteBackendDir = '/opt/xlsone-activation'

$PuttyDir = 'C:\Program Files\PuTTY'
$GitBinDir = 'C:\Program Files\Git\usr\bin'
$pathsToAdd = @($PuttyDir, $GitBinDir) | Where-Object { Test-Path $_ -PathType Container }
foreach ($p in $pathsToAdd) {
    if ($env:PATH -notlike "*$p*") {
        $env:PATH = "$p;$env:PATH"
    }
}

function Clear-Screen {
    Clear-Host
}

function Write-Header {
    Clear-Screen
    Write-Host '╔════════════════════════════════════════════════════════════════╗' -ForegroundColor Blue
    Write-Host '║                                                                ║' -ForegroundColor Blue
    Write-Host '║  xlsOne  一键部署脚本                                          ║' -ForegroundColor Blue
    Write-Host '║  自动更新版本信息并上传安装包到 z-pulse.cn                     ║' -ForegroundColor Blue
    Write-Host '║                                                                ║' -ForegroundColor Blue
    Write-Host '╚════════════════════════════════════════════════════════════════╝' -ForegroundColor Blue
    Write-Host ''
}

function Write-Step {
    param(
        [int]$Step,
        [int]$Total,
        [string]$Message
    )
    Write-Host ''
    Write-Host "[$Step/$Total] " -NoNewline -ForegroundColor Blue
    Write-Host $Message -ForegroundColor White
    Write-Host ('─' * 64) -ForegroundColor DarkGray
}

function Write-Info {
    param([string]$Message)
    Write-Host "ℹ  $Message" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "✓  $Message" -ForegroundColor Green
}

function Write-WarningMsg {
    param([string]$Message)
    Write-Host "⚠  $Message" -ForegroundColor Yellow
}

function Write-ErrorMsg {
    param([string]$Message)
    Write-Host "✗  $Message" -ForegroundColor Red
}

function Write-Box {
    param(
        [string]$Title,
        [string[]]$Lines
    )
    $width = 64
    Write-Host ''
    Write-Host "┌$('─' * $width)┐" -ForegroundColor Blue
    Write-Host "│ $Title".PadRight($width + 2) "│" -ForegroundColor Blue
    Write-Host "├$('─' * $width)┤" -ForegroundColor Blue
    foreach ($line in $Lines) {
        Write-Host "│ $line".PadRight($width + 2) "│" -ForegroundColor Blue
    }
    Write-Host "└$('─' * $width)┘" -ForegroundColor Blue
}

# -----------------------------------------------------------------------------
# 依赖检查
# -----------------------------------------------------------------------------
function Test-Dependencies {
    $missing = @()
    foreach ($cmd in @('pscp', 'plink', 'tar', 'python3')) {
        if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) {
            $missing += $cmd
        }
    }
    if ($missing.Count -gt 0) {
        Write-ErrorMsg "缺少必要工具：$($missing -join ' ')"
        Write-Info "请安装后重试`n  - PuTTY (pscp/plink): https://www.chiark.greenend.org.uk/~sgtatham/putty/latest.html"
        Write-Info "  - tar: 通常随 Git for Windows 安装，位于 C:\Program Files\Git\usr\bin"
        exit 1
    }
}

# -----------------------------------------------------------------------------
# 平台选择
# -----------------------------------------------------------------------------
function Select-Platform {
    while ($true) {
        Write-Box -Title '请选择要部署的平台' -Lines @(
            '[1] Linux AMD64 (.deb)',
            '[2] Linux ARM64 (.deb)',
            '[3] Windows AMD64 (.msi + .zip)',
            '[4] macOS (.dmg)',
            '[5] 全部平台（仅上传本地已存在的包）',
            '[q] 退出'
        )
        Write-Host ''
        $choice = Read-Host '请输入选项 [1-5/q]'
        switch ($choice.Trim()) {
            '1' { return 'linux_amd64' }
            '2' { return 'linux_arm64' }
            '3' { return 'windows' }
            '4' { return 'macos' }
            '5' { return 'all' }
            'q' { exit 0 }
            'Q' { exit 0 }
            default { Write-WarningMsg '无效选项，请重新输入' }
        }
    }
}

# -----------------------------------------------------------------------------
# 版本号输入
# -----------------------------------------------------------------------------
function Read-Version {
    $cmakePath = Join-Path $CppDir 'CMakeLists.txt'
    $currentVersion = ''
    if (Test-Path $cmakePath) {
        $content = Get-Content $cmakePath -Raw
        if ($content -match 'VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
            $currentVersion = $Matches[1]
        }
    }

    while ($true) {
        Write-Host ''
        $version = Read-Host "请输入版本号 [当前: $currentVersion]"
        if ([string]::IsNullOrWhiteSpace($version)) {
            $version = $currentVersion
        }
        if ($version -match '^[0-9]+\.[0-9]+\.[0-9]+$') {
            return $version
        }
        Write-WarningMsg '版本号格式应为 x.y.z，例如 1.0.7'
    }
}

# -----------------------------------------------------------------------------
# 升级说明输入
# -----------------------------------------------------------------------------
function Read-Changelog {
    Write-Host ''
    $changelog = Read-Host '请输入升级说明 [默认: 修复了某些已知错误]'
    if ([string]::IsNullOrWhiteSpace($changelog)) {
        $changelog = '修复了某些已知错误'
    }
    return $changelog
}

# -----------------------------------------------------------------------------
# 密码输入
# -----------------------------------------------------------------------------
function Read-Password {
    Write-Host ''
    $securePassword = Read-Host "请输入 ${ServerUser}@${Server} 的密码（输入不显示）" -AsSecureString
    $password = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($securePassword))
    if ([string]::IsNullOrWhiteSpace($password)) {
        Write-ErrorMsg '密码不能为空'
        exit 1
    }
    return $password
}

# -----------------------------------------------------------------------------
# 更新版本信息
# -----------------------------------------------------------------------------
function Update-VersionFiles {
    param(
        [string]$Version,
        [string]$Changelog
    )

    $cmakePath = Join-Path $CppDir 'CMakeLists.txt'
    Write-Info "更新 cpp/CMakeLists.txt: $Version"
    $pythonScript = @"
import re
import sys
from pathlib import Path
version, path = sys.argv[1:3]
p = Path(path)
text = p.read_text(encoding='utf-8')
text = re.sub(
    r'(project\([^)]*?VERSION\s+)[0-9]+\.[0-9]+\.[0-9]+',
    r'\g<1>' + version,
    text,
    count=1,
    flags=re.DOTALL
)
p.write_text(text, encoding='utf-8')
"@
    $pythonScript | python3 - $Version $cmakePath

    Write-Info "更新站点页面 CSS/JS 缓存戳: v${Version}-1"
    $htmlFiles = Get-ChildItem -Path $SiteDir -Recurse -Filter '*.html' -File
    foreach ($file in $htmlFiles) {
        $content = Get-Content $file.FullName -Raw
        $content = $content -replace '\?v=[0-9]+\.[0-9]+\.[0-9]+-[0-9]+', "?v=${Version}-1"
        Set-Content $file.FullName $content -NoNewline -Encoding UTF8
    }

    Write-Info '更新 site/api/version.json'
    $apiPath = Join-Path $SiteDir 'api/version.json'
    $pythonScript = @"
import json
import sys
from pathlib import Path

version, changelog, path = sys.argv[1:4]
api_path = Path(path)

if api_path.exists():
    data = json.loads(api_path.read_text(encoding='utf-8'))
else:
    data = {
        'latest_version': version,
        'changelog': changelog,
        'downloads': {},
        'checksums': {}
    }

old_version = data.get('latest_version', version)
data['latest_version'] = version
data['changelog'] = changelog

new_downloads = {}
for key, url in data.get('downloads', {}).items():
    new_downloads[key] = url.replace(old_version, version)

if 'linux_arm64' not in new_downloads:
    new_downloads['linux_arm64'] = f'https://z-pulse.cn/downloads/xlsOne-{version}-linux-arm64.deb'
if 'linux_amd64' not in new_downloads:
    new_downloads['linux_amd64'] = f'https://z-pulse.cn/downloads/xlsOne-{version}-linux-amd64.deb'
if 'windows_amd64' not in new_downloads:
    new_downloads['windows_amd64'] = f'https://z-pulse.cn/downloads/xlsone-{version}-windows-amd64.msi'
if 'windows_amd64_zip' not in new_downloads:
    new_downloads['windows_amd64_zip'] = f'https://z-pulse.cn/downloads/xlsone-{version}-windows-amd64.zip'
if 'macos' not in new_downloads:
    new_downloads['macos'] = f'https://z-pulse.cn/downloads/xlsOne-{version}-macos.dmg'

data['downloads'] = new_downloads

new_checksums = {}
for fname, checksum in data.get('checksums', {}).items():
    new_fname = fname.replace(old_version, version)
    new_checksums[new_fname] = checksum

data['checksums'] = new_checksums

api_path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
"@
    $pythonScript | python3 - $Version $Changelog $apiPath

    Write-Success '版本信息更新完成'
}

# -----------------------------------------------------------------------------
# 构建或定位安装包
# -----------------------------------------------------------------------------
function Find-OrBuildPackage {
    param(
        [string]$Platform,
        [string]$Version
    )

    switch ($Platform) {
        'linux_amd64' {
            $packagePath = Join-Path $CppDir "build-linux-release/xlsone-${Version}-linux-amd64.deb"
            if (Test-Path $packagePath) {
                Write-Info "找到已构建的 Linux AMD64 包: $packagePath"
            }
            else {
                Write-Host ''
                Write-WarningMsg "未找到 Linux AMD64 包: $packagePath"
                $build = Read-Host '是否立即构建？(需要 ninja) [y/N]'
                if ($build -match '^[yY]$') {
                    $buildDir = Join-Path $CppDir 'build-linux-release'
                    if (-not (Test-Path $buildDir)) {
                        Write-ErrorMsg '未找到 build-linux-release 目录，请先配置 CMake 构建'
                        exit 1
                    }
                    Push-Location $buildDir
                    try {
                        ninja
                        ninja package
                    }
                    finally {
                        Pop-Location
                    }
                    if (-not (Test-Path $packagePath)) {
                        Write-ErrorMsg "构建后仍未找到包: $packagePath"
                        exit 1
                    }
                }
                else {
                    Write-Info '跳过 Linux AMD64 包'
                    return
                }
            }
            Normalize-PackageName -Platform 'linux_amd64' -SourcePath $packagePath
            return
        }
        'linux_arm64' {
            $packagePath = Join-Path $CppDir "build-linux-release/xlsone-${Version}-linux-arm64.deb"
            if (Test-Path $packagePath) {
                Write-Info "找到已构建的 Linux ARM64 包: $packagePath"
            }
            else {
                Write-WarningMsg 'Linux ARM64 包需交叉编译或在 ARM64 设备上构建'
                Write-Info "请手动构建后放到: $packagePath"
                $cont = Read-Host '是否继续不上传此包？ [Y/n]'
                if ($cont -match '^[nN]$') {
                    exit 0
                }
                return
            }
            Normalize-PackageName -Platform 'linux_arm64' -SourcePath $packagePath
            return
        }
        'windows' {
            $packagePath = Join-Path $CppDir "build-windows-cn-release/xlsone-${Version}-windows-amd64.msi"
            $zipPath = Join-Path $CppDir "build-windows-cn-release/xlsone-${Version}-windows-amd64.zip"
            if ((Test-Path $packagePath) -and (Test-Path $zipPath)) {
                Write-Info "找到 Windows 安装包: $packagePath"
                Write-Info "找到 Windows 便携包: $zipPath"
                Normalize-PackageName -Platform 'windows' -SourcePath $packagePath
                Normalize-PackageName -Platform 'windows_zip' -SourcePath $zipPath
            }
            else {
                Write-WarningMsg '未找到 Windows 包，期望路径：'
                Write-Info "  $packagePath"
                Write-Info "  $zipPath"
                $cont = Read-Host '是否继续不上传 Windows 包？ [Y/n]'
                if ($cont -match '^[nN]$') {
                    exit 0
                }
            }
            return
        }
        'macos' {
            $packagePath = Join-Path $CppDir "build-macos-release/xlsOne-${Version}-macos.dmg"
            if (Test-Path $packagePath) {
                Write-Info "找到 macOS 安装包: $packagePath"
            }
            else {
                Write-WarningMsg "未找到 macOS 包，期望路径: $packagePath"
                $cont = Read-Host '是否继续不上传 macOS 包？ [Y/n]'
                if ($cont -match '^[nN]$') {
                    exit 0
                }
                return
            }
            Normalize-PackageName -Platform 'macos' -SourcePath $packagePath
            return
        }
    }
}

# -----------------------------------------------------------------------------
# 规范化安装包文件名，使其与 version.json 中的 URL 一致
# -----------------------------------------------------------------------------
function Normalize-PackageName {
    param(
        [string]$Platform,
        [string]$SourcePath
    )

    $versionJson = Join-Path $SiteDir 'api/version.json'

    $jsonKey = switch ($Platform) {
        'linux_amd64' { 'linux_amd64' }
        'linux_arm64' { 'linux_arm64' }
        'windows' { 'windows_amd64' }
        'windows_zip' { 'windows_amd64_zip' }
        'macos' { 'macos' }
        default {
            Write-Output $SourcePath
            return
        }
    }

    $pythonScript = @"
import json, sys
from pathlib import Path
from urllib.parse import urlparse
key, path = sys.argv[1:3]
try:
    data = json.loads(Path(path).read_text(encoding='utf-8'))
    url = data.get('downloads', {}).get(key, '')
    if url:
        fname = urlparse(url).path.split('/')[-1]
        if fname:
            print(fname)
except Exception:
    pass
"@
    $expectedFname = ($pythonScript | python3 - $jsonKey $versionJson | Out-String).Trim()
    $srcFname = Split-Path -Leaf $SourcePath

    if (-not $expectedFname -or $srcFname -eq $expectedFname) {
        Write-Output $SourcePath
        return
    }

    $destDir = Join-Path $env:TEMP 'xlsone-deploy-packages'
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    $destPath = Join-Path $destDir $expectedFname
    Copy-Item -Path $SourcePath -Destination $destPath -Force
    Write-Info "规范化文件名: $srcFname -> $expectedFname"
    Write-Output $destPath
}

# -----------------------------------------------------------------------------
# 计算并写入 checksum
# -----------------------------------------------------------------------------
function Update-Checksum {
    param([string]$PackagePath)

    $versionJson = Join-Path $SiteDir 'api/version.json'
    $fname = Split-Path -Leaf $PackagePath
    $checksum = (Get-FileHash -Algorithm SHA256 -Path $PackagePath).Hash.ToLower()
    Write-Info "计算 checksum: $fname = $checksum"

    $pythonScript = @"
import json
import sys
from pathlib import Path

fname, checksum, path = sys.argv[1:4]
api_path = Path(path)
data = json.loads(api_path.read_text(encoding='utf-8'))
data.setdefault('checksums', {})[fname] = checksum
api_path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
"@
    $pythonScript | python3 - $fname $checksum $versionJson
}

# -----------------------------------------------------------------------------
# 上传到服务器
# -----------------------------------------------------------------------------
function Send-ToServer {
    param(
        [string]$Version,
        [string]$Password,
        [string[]]$Packages
    )

    Write-Info '上传站点文件到服务器...'

    # 打包站点文件
    $siteTar = Join-Path $env:TEMP "xlsone-deploy-site-${Version}.tar.gz"
    $siteFiles = @(
        'index.html',
        'xlsone/index.html',
        'xlsone/download.html',
        'xlsone/buy.html',
        'products/xlsone/index.html',
        'products/xlsone/download.html',
        'support/index.html',
        'privacy/index.html',
        'api/version.json',
        'css/style.css',
        'robots.txt',
        'sitemap.xml',
        'downloads/checksums.txt'
    )

    Push-Location $SiteDir
    try {
        $tarArgs = @('-czf', $siteTar) + $siteFiles
        & tar $tarArgs 2>$null
    }
    finally {
        Pop-Location
    }

    $remoteTar = "/tmp/$(Split-Path -Leaf $siteTar)"

    # 上传站点 tar
    Write-Info "上传 $siteTar ..."
    & pscp -pw $Password -batch $siteTar "${ServerUser}@${Server}:/tmp/" 2>&1 | ForEach-Object {
        Write-Info "pscp: $_"
    }

    # 在远端解压站点文件
    $remoteCommands = @(
        "set -e",
        "cd ${RemoteRoot}",
        "tar -xzf ${remoteTar}",
        "rm -f ${remoteTar}",
        "mkdir -p ${RemoteBackendDir}/site/api",
        "cp -f ${RemoteRoot}/api/version.json ${RemoteBackendDir}/site/api/version.json",
        "echo SITE_UPLOADED"
    ) -join '; '

    & plink -pw $Password -batch -ssh "${ServerUser}@${Server}" $remoteCommands 2>&1 | ForEach-Object {
        if ($_ -match 'SITE_UPLOADED') {
            Write-Success '站点文件上传完成'
        }
        else {
            Write-Info "remote: $_"
        }
    }

    Remove-Item -Path $siteTar -Force -ErrorAction SilentlyContinue

    # 上传安装包
    if ($Packages.Count -gt 0) {
        Write-Info "准备上传 $($Packages.Count) 个安装包..."
        foreach ($pkg in $Packages) {
            if (Test-Path $pkg) {
                $fname = Split-Path -Leaf $pkg
                Write-Info "上传 $fname ..."
                & pscp -pw $Password -batch $pkg "${ServerUser}@${Server}:${RemoteRoot}/downloads/" 2>&1 | ForEach-Object {
                    Write-Info "pscp: $_"
                }
                Write-Success "$fname 上传完成"
            }
        }
    }
    else {
        Write-WarningMsg '没有需要上传的安装包'
    }
}

# -----------------------------------------------------------------------------
# 重新生成 downloads/checksums.txt
# -----------------------------------------------------------------------------
function New-ChecksumsTxt {
    $versionJson = Join-Path $SiteDir 'api/version.json'
    $checksumsTxt = Join-Path $SiteDir 'downloads/checksums.txt'

    Write-Info '重新生成 downloads/checksums.txt'
    $pythonScript = @"
import json
import sys
from pathlib import Path

version_json, checksums_txt = sys.argv[1:3]
data = json.loads(Path(version_json).read_text(encoding='utf-8'))
checksums = data.get('checksums', {})

lines = []
for fname in sorted(checksums.keys()):
    lines.append(f'{checksums[fname]}  {fname}')

Path(checksums_txt).write_text('\n'.join(lines) + ('\n' if lines else ''), encoding='utf-8')
"@
    $pythonScript | python3 - $versionJson $checksumsTxt
    Write-Success 'checksums.txt 已生成'
}

# -----------------------------------------------------------------------------
# 主流程
# -----------------------------------------------------------------------------
function Invoke-Deploy {
    Write-Header
    Test-Dependencies

    $platform = Select-Platform
    $version = Read-Version
    $changelog = Read-Changelog

    Write-Box -Title '部署摘要' -Lines @(
        "平台: $platform",
        "版本: $version",
        "说明: $changelog",
        "服务器: ${ServerUser}@${Server}"
    )

    Write-Host ''
    $confirm = Read-Host '确认开始部署？ [y/N]'
    if ($confirm -notmatch '^[yY]$') {
        Write-Info '已取消部署'
        exit 0
    }

    $password = Read-Password

    # 测试连接（首次会自动缓存主机密钥）
    Write-Info '测试服务器连接...'
    $testResult = & plink -pw $password -ssh "${ServerUser}@${Server}" 'echo OK' 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-ErrorMsg '无法连接到服务器，请检查密码和网络'
        Write-Info '提示: 如果提示主机密钥确认，请运行:'
        Write-Info "  echo y | plink -pw 密码 -ssh ${ServerUser}@${Server} exit"
        exit 1
    }
    Write-Success '服务器连接成功'

    # 步骤 1: 更新版本信息
    Write-Step -Step 1 -Total 4 -Message '更新版本信息'
    Update-VersionFiles -Version $version -Changelog $changelog

    # 步骤 2: 定位/构建安装包
    Write-Step -Step 2 -Total 4 -Message '定位或构建安装包'
    $packages = @()
    switch ($platform) {
        'all' {
            foreach ($p in @('linux_amd64', 'linux_arm64', 'windows', 'macos')) {
                $paths = Find-OrBuildPackage -Platform $p -Version $version
                if ($paths) {
                    $packages += $paths
                }
            }
        }
        default {
            $paths = Find-OrBuildPackage -Platform $platform -Version $version
            if ($paths) {
                $packages += $paths
            }
        }
    }

    # 步骤 3: 计算 checksum
    Write-Step -Step 3 -Total 4 -Message '计算安装包 checksum'
    if ($packages.Count -gt 0) {
        foreach ($pkg in $packages) {
            Update-Checksum -PackagePath $pkg
        }
        New-ChecksumsTxt
    }
    else {
        Write-WarningMsg '没有本地包需要计算 checksum'
    }

    # 步骤 4: 上传
    Write-Step -Step 4 -Total 4 -Message '上传到服务器'
    Send-ToServer -Version $version -Password $password -Packages $packages

    # 完成
    Write-Host ''
    Write-Host '╔════════════════════════════════════════════════════════════════╗' -ForegroundColor Green
    Write-Host '║                                                                ║' -ForegroundColor Green
    Write-Host '║  部署完成！                                                    ║' -ForegroundColor Green
    Write-Host "║  版本: $version".PadRight(65) '║' -ForegroundColor Green
    Write-Host "║  平台: $platform".PadRight(65) '║' -ForegroundColor Green
    Write-Host '║  请访问 https://z-pulse.cn 查看效果                            ║' -ForegroundColor Green
    Write-Host '║                                                                ║' -ForegroundColor Green
    Write-Host '╚════════════════════════════════════════════════════════════════╝' -ForegroundColor Green
    Write-Host ''
}

Invoke-Deploy
