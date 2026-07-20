<#
.SYNOPSIS
    xlsOne 一键部署脚本 (PowerShell 版)
.DESCRIPTION
    交互式选择平台、版本，输入 z-pulse.cn root 密码或使用 SSH Key 后自动：
      1. 更新 cpp/CMakeLists.txt、site/api/version.json、站点页面缓存戳
      2. 构建或定位安装包
      3. 将待发布安装包收集到 .build/release-artifacts/<version>/
      4. 上传安装包与站点文件到服务器
      5. 支持一键部署所有平台版本
.NOTES
    用法：.\scripts\deploy\deploy.ps1
    在项目根目录运行，或直接运行脚本（脚本会自动定位项目根目录）。
#>

#Requires -Version 5.1
$ErrorActionPreference = "Stop"

# -----------------------------------------------------------------------------
# 路径与配置
# -----------------------------------------------------------------------------

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Resolve-Path "$ScriptDir\..\.."
$SiteDir = "$ProjectRoot\site"
$CppDir = "$ProjectRoot\cpp"
$ReleaseArtifactsDir = "$ProjectRoot\.build\release-artifacts"
$DeployArtifactDir = $null   # 稍后在运行时设置
$script:TarCommand = $null

$Server = "z-pulse.cn"
$ServerUser = "root"
$RemoteRoot = "/var/www/z-pulse.cn"
$RemoteBackendDir = "/opt/xlsone-activation"

# -----------------------------------------------------------------------------
# UI 辅助函数
# -----------------------------------------------------------------------------

function Clear-Screen {
    Clear-Host
}

function Write-Header {
    Clear-Screen
    Write-Host "╔════════════════════════════════════════════════════════════════╗" -ForegroundColor Blue
    Write-Host "║" -ForegroundColor Blue -NoNewline
    Write-Host "                                                                " -NoNewline
    Write-Host "║" -ForegroundColor Blue
    Write-Host "║  " -ForegroundColor Blue -NoNewline
    Write-Host "xlsOne" -ForegroundColor Cyan -NoNewline
    Write-Host "  一键部署脚本 (PowerShell)                                    " -NoNewline
    Write-Host "║" -ForegroundColor Blue
    Write-Host "║  " -ForegroundColor Blue -NoNewline
    Write-Host "自动更新版本信息并上传安装包到 z-pulse.cn                      " -ForegroundColor DarkGray -NoNewline
    Write-Host "║" -ForegroundColor Blue
    Write-Host "║" -ForegroundColor Blue -NoNewline
    Write-Host "                                                                " -NoNewline
    Write-Host "║" -ForegroundColor Blue
    Write-Host "╚════════════════════════════════════════════════════════════════╝" -ForegroundColor Blue
    Write-Host ""
}

function Write-Step {
    param([int]$Step, [int]$Total, [string]$Message)
    Write-Host ""
    Write-Host "[$Step/$Total]" -ForegroundColor Blue -NoNewline
    Write-Host " $Message"
    Write-Host ("─" * 64) -ForegroundColor DarkGray
}

function Write-Info {
    param([string]$Message)
    Write-Host "ℹ  $Message" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "✓  $Message" -ForegroundColor Green
}

function Write-Warning {
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
    Write-Host ""
    Write-Host ("┌" + ("─" * $width) + "┐") -ForegroundColor Blue
    Write-Host ("│ " + $Title.PadRight($width) + " │") -ForegroundColor Blue
    Write-Host ("├" + ("─" * $width) + "┤") -ForegroundColor Blue
    foreach ($line in $Lines) {
        Write-Host ("│ " + $line.PadRight($width) + " │") -ForegroundColor Blue
    }
    Write-Host ("└" + ("─" * $width) + "┘") -ForegroundColor Blue
}

# -----------------------------------------------------------------------------
# Python 内联脚本执行辅助
# -----------------------------------------------------------------------------

function Invoke-PythonScript {
    param(
        [string]$Script,
        [string[]]$Arguments
    )
    $tmpFile = [System.IO.Path]::GetTempFileName() + ".py"
    try {
        Set-Content -Path $tmpFile -Value $Script -Encoding UTF8 -NoNewline
        # 确保换行
        Add-Content -Path $tmpFile -Value "`n" -Encoding UTF8 -NoNewline
        & python $tmpFile @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Python 脚本执行失败 (exit code: $LASTEXITCODE)"
        }
    }
    finally {
        Remove-Item -Path $tmpFile -Force -ErrorAction SilentlyContinue
    }
}

# -----------------------------------------------------------------------------
# 依赖检查
# -----------------------------------------------------------------------------

function Find-TarCommand {
    # Windows 10 normally provides System32\tar.exe, but some Windows Server
    # images do not. Git for Windows and MSYS2 both ship a compatible tar that
    # may not be present on PATH, so probe their standard locations as well.
    $command = Get-Command tar.exe -ErrorAction SilentlyContinue
    if (-not $command) {
        $command = Get-Command tar -ErrorAction SilentlyContinue
    }
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        "$env:SystemRoot\System32\tar.exe",
        "$env:ProgramFiles\Git\usr\bin\tar.exe",
        "${env:ProgramFiles(x86)}\Git\usr\bin\tar.exe",
        "C:\msys64\usr\bin\tar.exe"
    )
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return $candidate
        }
    }

    return $null
}

function Test-Dependencies {
    $missing = @()
    $required = @(
        @{Name="python"; Desc="Python 3"}
        @{Name="ssh"; Desc="OpenSSH 客户端"}
        @{Name="scp"; Desc="OpenSSH SCP"}
    )

    foreach ($dep in $required) {
        if (-not (Get-Command $dep.Name -ErrorAction SilentlyContinue)) {
            $missing += $dep.Desc
        }
    }

    $script:TarCommand = Find-TarCommand
    if (-not $script:TarCommand) {
        $missing += "tar (Windows 10+、Git for Windows 或 MSYS2)"
    }

    # sshpass 检查 — Windows 上通常不可用，仅做提示
    $hasSshpass = $false
    if (Get-Command sshpass -ErrorAction SilentlyContinue) {
        $hasSshpass = $true
    }

    if ($missing.Count -gt 0) {
        Write-ErrorMsg "缺少必要工具：$($missing -join ', ')"
        Write-Info "请安装后重试。"
        Write-Info "  - Python 3: https://www.python.org/downloads/"
        Write-Info "  - OpenSSH:  Windows 10+ 可在「可选功能」中添加"
        Write-Info "  - tar:      Windows 10+ 自带，或随 Git for Windows / MSYS2 安装"
        return $false
    }

    if (-not $hasSshpass) {
        Write-Warning "未检测到 sshpass 工具（Windows 上通常不可用）。"
        Write-Info "脚本将尝试使用 SSH Key 认证方式连接服务器。"
        Write-Info "请确保已配置 SSH Key 到 $Server："
        Write-Info "  ssh-copy-id ${ServerUser}@${Server}"
        Write-Info "或者手动安装 sshpass（WSL / MSYS2 / scoop install sshpass）。"
        Write-Host ""
    }

    return @{ HasSshpass = $hasSshpass }
}

# -----------------------------------------------------------------------------
# 平台选择
# -----------------------------------------------------------------------------

function Select-Platform {
    while ($true) {
        Write-Box -Title "请选择要部署的平台" -Lines @(
            "[1] Linux AMD64 (.deb)",
            "[2] Linux ARM64 (.deb)",
            "[3] Windows AMD64 (.msi + .zip)",
            "[4] macOS Universal (.dmg)",
            "[5] 全部平台（收集本地已有的所有包）",
            "[q] 退出"
        )
        $choice = Read-Host "`n请输入选项 [1-5/q]"
        switch ($choice) {
            "1" { return "linux_amd64" }
            "2" { return "linux_arm64" }
            "3" { return "windows" }
            "4" { return "macos" }
            "5" { return "all" }
            "q" { exit 0 }
            "Q" { exit 0 }
            default { Write-Warning "无效选项，请重新输入" }
        }
    }
}

# -----------------------------------------------------------------------------
# 版本号输入
# -----------------------------------------------------------------------------

function Read-Version {
    $cmakePath = "$CppDir\CMakeLists.txt"
    $currentVersion = ""
    if (Test-Path $cmakePath) {
        $cmakeContent = Get-Content $cmakePath -Raw -Encoding UTF8
        if ($cmakeContent -match 'VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
            $currentVersion = $Matches[1]
        }
    }

    while ($true) {
        $version = Read-Host "`n请输入版本号 [当前: $currentVersion]"
        if ([string]::IsNullOrWhiteSpace($version)) {
            $version = $currentVersion
        }
        if ($version -match '^[0-9]+\.[0-9]+\.[0-9]+$') {
            return $version
        }
        Write-Warning "版本号格式应为 x.y.z，例如 1.0.7"
    }
}

# -----------------------------------------------------------------------------
# 升级说明输入
# -----------------------------------------------------------------------------

function Read-Changelog {
    $changelog = Read-Host "`n请输入升级说明 [默认: 修复了某些已知错误]"
    if ([string]::IsNullOrWhiteSpace($changelog)) {
        $changelog = "修复了某些已知错误"
    }
    return $changelog
}

# -----------------------------------------------------------------------------
# 密码输入
# -----------------------------------------------------------------------------

function Read-Password {
    Write-Host ""
    Write-Host "请输入 ${ServerUser}@${Server} 的密码" -ForegroundColor Yellow -NoNewline
    $securePass = Read-Host " (输入不显示)" -AsSecureString
    $password = [Runtime.InteropServices.Marshal]::PtrToStringAuto(
        [Runtime.InteropServices.Marshal]::SecureStringToBSTR($securePass)
    )
    if ([string]::IsNullOrWhiteSpace($password)) {
        Write-ErrorMsg "密码不能为空"
        exit 1
    }
    Write-Host ""
    return $password
}

# -----------------------------------------------------------------------------
# 发版产物目录
# -----------------------------------------------------------------------------

function Get-ArtifactStagingDir {
    if (-not $script:DeployArtifactDir) {
        $versionJson = "$SiteDir\api\version.json"
        $version = "unknown"
        if (Test-Path $versionJson) {
            $data = Get-Content $versionJson -Raw -Encoding UTF8 | ConvertFrom-Json
            $version = $data.latest_version
        }
        $script:DeployArtifactDir = "$ReleaseArtifactsDir\$version"
    }
    if (-not (Test-Path $script:DeployArtifactDir)) {
        New-Item -Path $script:DeployArtifactDir -ItemType Directory -Force | Out-Null
    }
    return $script:DeployArtifactDir
}

# -----------------------------------------------------------------------------
# 更新版本信息
# -----------------------------------------------------------------------------

function Update-VersionFiles {
    param([string]$Version, [string]$Changelog)

    # --- 更新 cpp/CMakeLists.txt ---
    Write-Info "更新 cpp/CMakeLists.txt: $Version"
    $cmakePath = "$CppDir\CMakeLists.txt"
    Invoke-PythonScript -Script @"
import re, sys
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
"@ -Arguments @($Version, $cmakePath)

    # --- 更新站点页面 CSS/JS 缓存戳 ---
    Write-Info "更新站点页面 CSS/JS 缓存戳: v${Version}-1"
    Invoke-PythonScript -Script @"
import re, sys
from pathlib import Path
version, site_dir = sys.argv[1:3]
pattern = re.compile(r'\?v=[0-9]+\.[0-9]+\.[0-9]+-[0-9]+')
replacement = f'?v={version}-1'
for path in Path(site_dir).rglob('*.html'):
    text = path.read_text(encoding='utf-8')
    updated = pattern.sub(replacement, text)
    if updated != text:
        path.write_text(updated, encoding='utf-8')
"@ -Arguments @($Version, $SiteDir)

    # --- 更新 site/api/version.json ---
    Write-Info "更新 site/api/version.json"
    $versionJsonPath = "$SiteDir\api\version.json"
    Invoke-PythonScript -Script @"
import json, sys
from pathlib import Path

version, changelog, path = sys.argv[1:4]
api_path = Path(path)

data = json.loads(api_path.read_text(encoding='utf-8')) if api_path.exists() else {
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

# macOS 直发统一使用 Qt universal DMG，单个包覆盖 Intel + Apple Silicon。
new_downloads['macos'] = f'https://z-pulse.cn/downloads/xlsOne-{version}-macos-universal.dmg'

data['downloads'] = new_downloads

# checksums 的 key 也随版本更新
new_checksums = {}
for fname, checksum in data.get('checksums', {}).items():
    new_fname = fname.replace(old_version, version)
    new_checksums[new_fname] = checksum

data['checksums'] = new_checksums

api_path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
"@ -Arguments @($Version, $Changelog, $versionJsonPath)

    Write-Success "版本信息更新完成"
}

# -----------------------------------------------------------------------------
# 定位/构建安装包
# -----------------------------------------------------------------------------

function Locate-OrBuildPackage {
    param(
        [string]$Platform,
        [string]$Version
    )

    $packagePaths = @()

    switch ($Platform) {
        "linux_amd64" {
            $debName = "xlsone-${Version}-linux-amd64.deb"
            $candidates = @(
                "$ProjectRoot\.build\$debName",
                "$CppDir\build-linux-release\$debName"
            )

            $found = $null
            foreach ($c in $candidates) {
                if (Test-Path $c) { $found = $c; break }
            }

            if ($found) {
                Write-Info "找到已构建的 Linux AMD64 包: $found"
                $normalized = Normalize-PackageName -Platform "linux_amd64" -SourcePath $found
                if ($normalized) { $packagePaths += $normalized }
            }
            else {
                Write-Warning "未找到 Linux AMD64 包，已检查："
                foreach ($c in $candidates) { Write-Info "  $c" }
                Write-Warning "Linux .deb 只能在 Linux 构建机上构建。"
                $cont = Read-Host "是否继续不上传此包？ [Y/n]"
                if ($cont -eq "n" -or $cont -eq "N") { exit 0 }
            }
        }

        "linux_arm64" {
            $debName = "xlsone-${Version}-linux-arm64.deb"
            $candidates = @(
                "$ProjectRoot\.build\$debName",
                "$CppDir\build-linux-release\$debName"
            )

            $found = $null
            foreach ($c in $candidates) {
                if (Test-Path $c) { $found = $c; break }
            }

            if ($found) {
                Write-Info "找到已构建的 Linux ARM64 包: $found"
                $normalized = Normalize-PackageName -Platform "linux_arm64" -SourcePath $found
                if ($normalized) { $packagePaths += $normalized }
            }
            else {
                Write-Warning "未找到 Linux ARM64 包，已检查："
                foreach ($c in $candidates) { Write-Info "  $c" }
                Write-Warning "Linux .deb 只能在 ARM64 Linux 构建机上构建。"
                $cont = Read-Host "是否继续不上传此包？ [Y/n]"
                if ($cont -eq "n" -or $cont -eq "N") { exit 0 }
            }
        }

        "windows" {
            $msiName = "xlsone-${Version}-windows-amd64.msi"
            $zipName = "xlsone-${Version}-windows-amd64.zip"
            $msiCandidates = @(
                "$ProjectRoot\.build\$msiName",
                "$CppDir\build-windows-cn-release\$msiName"
            )
            $zipCandidates = @(
                "$ProjectRoot\.build\$zipName",
                "$CppDir\build-windows-cn-release\$zipName"
            )

            $msiFound = $null
            $zipFound = $null
            foreach ($c in $msiCandidates) {
                if (Test-Path $c) { $msiFound = $c; break }
            }
            foreach ($c in $zipCandidates) {
                if (Test-Path $c) { $zipFound = $c; break }
            }

            if ($msiFound -and $zipFound) {
                Write-Info "找到 Windows 安装包: $msiFound"
                Write-Info "找到 Windows 便携包: $zipFound"
                $normalizedMsi = Normalize-PackageName -Platform "windows" -SourcePath $msiFound
                $normalizedZip = Normalize-PackageName -Platform "windows_zip" -SourcePath $zipFound
                if ($normalizedMsi) { $packagePaths += $normalizedMsi }
                if ($normalizedZip) { $packagePaths += $normalizedZip }
            }
            else {
                Write-Warning "未找到 Windows 包，已检查："
                foreach ($c in ($msiCandidates + $zipCandidates)) {
                    if (-not (Test-Path $c)) { Write-Info "  (缺失) $c" }
                }
                Write-Info "提示：在 Windows 上可使用 cpp/scripts/package-windows.ps1 构建安装包。"
                $cont = Read-Host "是否继续不上传 Windows 包？ [Y/n]"
                if ($cont -eq "n" -or $cont -eq "N") { exit 0 }
            }
        }

        "macos" {
            $expectedName = "xlsOne-${Version}-macos-universal.dmg"
            $stagingDir = Get-ArtifactStagingDir
            $candidates = @(
                "$stagingDir\$expectedName",
                "$CppDir\$expectedName",
                "$CppDir\build-macos-release\$expectedName",
                "$ProjectRoot\.build\$expectedName"
            )

            $found = $null
            foreach ($c in $candidates) {
                if (Test-Path $c) { $found = $c; break }
            }

            if ($found) {
                Write-Info "找到 macOS Universal 安装包: $found"
                $normalized = Normalize-PackageName -Platform "macos" -SourcePath $found
                if ($normalized) { $packagePaths += $normalized }
            }
            else {
                Write-Warning "未找到 macOS Universal DMG，已检查："
                foreach ($c in $candidates) { Write-Info "  $c" }
                Write-Warning "macOS DMG 只能在 macOS 构建机上构建。"
                $cont = Read-Host "是否继续不上传 macOS 包？ [Y/n]"
                if ($cont -eq "n" -or $cont -eq "N") { exit 0 }
            }
        }
    }

    return $packagePaths
}

# -----------------------------------------------------------------------------
# 规范化安装包文件名，使其与 version.json 中的 URL 一致，并收集到发版目录
# -----------------------------------------------------------------------------

function Normalize-PackageName {
    param(
        [string]$Platform,
        [string]$SourcePath
    )

    $versionJson = "$SiteDir\api\version.json"
    $jsonKey = switch ($Platform) {
        "linux_amd64"   { "linux_amd64" }
        "linux_arm64"   { "linux_arm64" }
        "windows"       { "windows_amd64" }
        "windows_zip"   { "windows_amd64_zip" }
        "macos"         { "macos" }
        default         { return $SourcePath }
    }

    # 从 version.json 中获取期望的文件名
    $expectedFname = Invoke-PythonScript -Script @"
import json, sys
from pathlib import Path
from urllib.parse import urlparse

key, path = sys.argv[1:3]
data = json.loads(Path(path).read_text(encoding='utf-8'))
url = data.get('downloads', {}).get(key, '')
print(urlparse(url).path.split('/')[-1])
"@ -Arguments @($jsonKey, $versionJson)

    $expectedFname = $expectedFname.Trim()
    if ([string]::IsNullOrWhiteSpace($expectedFname)) {
        Write-ErrorMsg "version.json 中缺少 downloads.$jsonKey"
        exit 1
    }

    $destDir = Get-ArtifactStagingDir
    $destPath = "$destDir\$expectedFname"

    $srcAbs = (Resolve-Path $SourcePath).Path
    $destAbs = (Resolve-Path $destDir).Path + "\" + $expectedFname

    if ($srcAbs -ne $destAbs) {
        Copy-Item -Path $SourcePath -Destination $destPath -Force
        Write-Info "收集发版文件: $(Split-Path $SourcePath -Leaf) -> $destPath"
    }
    else {
        Write-Info "发版文件已在收口目录: $destPath"
    }

    return $destPath
}

# -----------------------------------------------------------------------------
# 计算并写入 checksum
# -----------------------------------------------------------------------------

function Update-Checksum {
    param([string]$PackagePath)

    $versionJson = "$SiteDir\api\version.json"
    $fname = Split-Path $PackagePath -Leaf

    # 使用 PowerShell 原生的 Get-FileHash 替代 shasum
    $hash = Get-FileHash -Path $PackagePath -Algorithm SHA256
    $checksum = $hash.Hash.ToLower()

    Write-Info "计算 checksum: $fname = $checksum"

    Invoke-PythonScript -Script @"
import json, sys
from pathlib import Path

fname, checksum, path = sys.argv[1:4]
api_path = Path(path)
data = json.loads(api_path.read_text(encoding='utf-8'))
data.setdefault('checksums', {})[fname] = checksum
api_path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
"@ -Arguments @($fname, $checksum, $versionJson)
}

# -----------------------------------------------------------------------------
# 重新生成 downloads/checksums.txt
# -----------------------------------------------------------------------------

function Update-ChecksumsTxt {
    $versionJson = "$SiteDir\api\version.json"
    $checksumsTxt = "$SiteDir\downloads\checksums.txt"

    Write-Info "重新生成 downloads/checksums.txt"

    Invoke-PythonScript -Script @"
import json, sys
from pathlib import Path

version_json, checksums_txt = sys.argv[1:3]
data = json.loads(Path(version_json).read_text(encoding='utf-8'))
checksums = data.get('checksums', {})

lines = []
for fname in sorted(checksums.keys()):
    lines.append(f'{checksums[fname]}  {fname}')

Path(checksums_txt).write_text('\n'.join(lines) + ('\n' if lines else ''), encoding='utf-8')
"@ -Arguments @($versionJson, $checksumsTxt)

    Write-Success "checksums.txt 已生成"
}

# -----------------------------------------------------------------------------
# SSH 连接辅助 — 支持 Key 和 sshpass 两种方式
# -----------------------------------------------------------------------------

# 全局变量：认证方式与会话信息
$script:SshAuthMode = $null   # "key" 或 "sshpass"
$script:SshPassword = $null

function Invoke-RemoteCommand {
    param([string]$Command, [string[]]$ExtraArgs = @())

    if ($script:SshAuthMode -eq "sshpass") {
        $env:SSHPASS = $script:SshPassword
    }

    $sshArgs = @("-o", "StrictHostKeyChecking=no", "-o", "ConnectTimeout=10")
    if ($script:SshAuthMode -ne "sshpass") {
        $sshArgs += @("-o", "BatchMode=yes")
    }
    $sshArgs += $ExtraArgs
    $sshArgs += @("${ServerUser}@${Server}", "sh -se")

    Write-Info "执行远程命令..."
    if ($script:SshAuthMode -eq "sshpass") {
        $sshpassArgs = @("-e", "ssh") + $sshArgs
        $result = $Command | & sshpass @sshpassArgs 2>&1
    }
    else {
        $result = $Command | & ssh @sshArgs 2>&1
    }
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        Write-ErrorMsg "远程命令失败：$result"
        throw "SSH command failed"
    }
    return $result
}

function Invoke-ScpUpload {
    param(
        [string]$LocalPath,
        [string]$RemotePath
    )

    $scpArgs = @("-o", "StrictHostKeyChecking=no", "-o", "ConnectTimeout=10")
    if ($script:SshAuthMode -ne "sshpass") {
        $scpArgs += @("-o", "BatchMode=yes")
    }
    $scpArgs += @($LocalPath, "${ServerUser}@${Server}:$RemotePath")

    if ($script:SshAuthMode -eq "sshpass") {
        $env:SSHPASS = $script:SshPassword
    }

    Write-Info "上传: $(Split-Path $LocalPath -Leaf)"
    if ($script:SshAuthMode -eq "sshpass") {
        $sshpassArgs = @("-e", "scp") + $scpArgs
        $result = & sshpass @sshpassArgs 2>&1
    }
    else {
        $result = & scp @scpArgs 2>&1
    }
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        Write-ErrorMsg "SCP 上传失败：$result"
        throw "SCP upload failed"
    }
}

function Test-ServerConnection {
    Write-Info "测试服务器连接..."

    if ($script:SshAuthMode -eq "sshpass") {
        $env:SSHPASS = $script:SshPassword
    }

    $sshArgs = @("-o", "StrictHostKeyChecking=no", "-o", "ConnectTimeout=10")
    if ($script:SshAuthMode -ne "sshpass") {
        $sshArgs += @("-o", "BatchMode=yes")
    }
    $sshArgs += @("${ServerUser}@${Server}", "echo OK")

    if ($script:SshAuthMode -eq "sshpass") {
        $sshpassArgs = @("-e", "ssh") + $sshArgs
        $testResult = & sshpass @sshpassArgs 2>&1
    }
    else {
        $testResult = & ssh @sshArgs 2>&1
    }

    $exitCode = $LASTEXITCODE
    $testOutput = $testResult -join "`n"
    if ($exitCode -ne 0 -or $testOutput -notmatch "(?m)^OK\s*$") {
        Write-ErrorMsg "无法连接到服务器，请检查认证和网络"
        throw "Server connection failed"
    }

    Write-Success "服务器连接成功"
}

# -----------------------------------------------------------------------------
# 上传到服务器
# -----------------------------------------------------------------------------

function Invoke-UploadToServer {
    param(
        [string]$Version,
        [string[]]$Packages
    )

    Write-Info "准备上传站点文件..."

    # 打包站点文件
    $siteTar = "$env:TEMP\xlsone-deploy-site-${Version}.tar.gz"
    $siteFiles = @(
        "index.html",
        "xlsone/index.html",
        "xlsone/download.html",
        "xlsone/buy.html",
        "products/xlsone/index.html",
        "products/xlsone/download.html",
        "support/index.html",
        "privacy/index.html",
        "api/version.json",
        "css/style.css",
        "robots.txt",
        "sitemap.xml",
        "downloads/checksums.txt"
    )

    # 过滤实际存在的文件
    $existingFiles = $siteFiles | Where-Object { Test-Path "$SiteDir\$_" }

    Write-Info "打包 $($existingFiles.Count) 个站点文件..."
    $originalPath = $env:PATH
    $tarDirectory = Split-Path -Parent $script:TarCommand
    if ($tarDirectory) {
        # Git/MSYS2 tar starts gzip as a child process. Their usr\bin directory
        # is often absent from PATH even when tar.exe itself was discovered.
        $env:PATH = "$tarDirectory;$env:PATH"
    }

    # Create the archive from %TEMP% using a relative output name. GNU tar
    # otherwise interprets a Windows drive prefix such as "D:" as a remote host.
    Push-Location $env:TEMP
    try {
        $tarArgs = @("-czf", (Split-Path $siteTar -Leaf), "-C", $SiteDir)
        $tarArgs += @($existingFiles | ForEach-Object { $_ -replace '\\', '/' })
        & $script:TarCommand @tarArgs
        if ($LASTEXITCODE -ne 0) {
            Write-ErrorMsg "站点文件打包失败"
            throw "tar failed"
        }
    }
    finally {
        Pop-Location
        $env:PATH = $originalPath
    }

    # 上传站点 tar 包
    Invoke-ScpUpload -LocalPath $siteTar -RemotePath "/tmp/"

    # 远程解压站点文件
    $remoteTar = "/tmp/$(Split-Path $siteTar -Leaf)"
    Invoke-RemoteCommand @"
set -e
cd ${RemoteRoot}
tar -xzf ${remoteTar}
rm -f ${remoteTar}
mkdir -p ${RemoteBackendDir}/site/api
cp -f ${RemoteRoot}/api/version.json ${RemoteBackendDir}/site/api/version.json
echo 'SITE_UPLOADED'
"@

    Write-Success "站点文件上传完成"
    Remove-Item $siteTar -Force -ErrorAction SilentlyContinue

    # Keep the live activation-code console in sync with its tracked source.
    $licenseManagerPage = "$ProjectRoot\activation\admin\license-manager.html"
    if (Test-Path -LiteralPath $licenseManagerPage) {
        Invoke-RemoteCommand "mkdir -p '${RemoteBackendDir}/public' '${RemoteRoot}/xlsone/license-console'"
        Invoke-ScpUpload -LocalPath $licenseManagerPage `
            -RemotePath "${RemoteBackendDir}/public/license-manager.html"
        Invoke-ScpUpload -LocalPath $licenseManagerPage `
            -RemotePath "${RemoteRoot}/xlsone/license-console/index.html"
        Write-Success "授权码管理页面上传完成"
    }

    # 上传安装包
    if ($Packages.Count -gt 0) {
        Write-Info "准备上传 $($Packages.Count) 个安装包..."
        foreach ($pkg in $Packages) {
            if (Test-Path $pkg) {
                $fname = Split-Path $pkg -Leaf
                Write-Info "上传 $fname ..."
                Invoke-ScpUpload -LocalPath $pkg -RemotePath "${RemoteRoot}/downloads/"
                Write-Success "$fname 上传完成"
            }
        }
    }
    else {
        Write-Warning "没有需要上传的安装包"
    }
}

# -----------------------------------------------------------------------------
# 主流程
# -----------------------------------------------------------------------------

function Main {
    Write-Header

    # 依赖检查
    $depResult = Test-Dependencies
    if (-not $depResult) { exit 1 }

    # 平台选择
    $platform = Select-Platform

    # 版本号
    $version = Read-Version
    $script:DeployArtifactDir = "$ReleaseArtifactsDir\$version"

    # 升级说明
    $changelog = Read-Changelog

    # 确认摘要
    Write-Box -Title "部署摘要" -Lines @(
        "平台: $platform",
        "版本: $version",
        "说明: $changelog",
        "发版目录: $ReleaseArtifactsDir\$version",
        "服务器: ${ServerUser}@${Server}"
    )

    $confirm = Read-Host "`n确认开始部署？ [y/N]"
    if ($confirm -ne "y" -and $confirm -ne "Y") {
        Write-Info "已取消部署"
        exit 0
    }

    # --- 认证方式选择 ---
    $hasSshpass = $depResult.HasSshpass
    Write-Host ""
    if ($hasSshpass) {
        Write-Box -Title "认证方式" -Lines @(
            "[1] SSH Key 认证（推荐）",
            "[2] 密码认证（sshpass）"
        )
        $authChoice = Read-Host "请选择认证方式 [1-2]"
    }
    else {
        Write-Info "使用 SSH Key 认证方式连接服务器。"
        $authChoice = "1"
    }

    if ($authChoice -eq "2" -and $hasSshpass) {
        $script:SshAuthMode = "sshpass"
        $script:SshPassword = Read-Password
    }
    else {
        $script:SshAuthMode = "key"
        Write-Info "使用 SSH Key 认证。请确保已配置 Key 到服务器。"
    }

    # 测试连接
    Test-ServerConnection

    # ======================================================================
    # 步骤 1: 更新版本信息
    # ======================================================================
    Write-Step -Step 1 -Total 4 -Message "更新版本信息"
    Update-VersionFiles -Version $version -Changelog $changelog

    # ======================================================================
    # 步骤 2: 定位/构建安装包
    # ======================================================================
    Write-Step -Step 2 -Total 4 -Message "定位或构建安装包"
    $packages = @()

    if ($platform -eq "all") {
        Write-Info "一键部署所有平台版本..."
        $allPlatforms = @("linux_amd64", "linux_arm64", "windows", "macos")
        $platformResults = @{}
        $foundCount = 0
        $missCount = 0

        foreach ($p in $allPlatforms) {
            Write-Info "--- 处理平台: $p ---"
            $paths = Locate-OrBuildPackage -Platform $p -Version $version
            if ($paths -and $paths.Count -gt 0) {
                $platformResults[$p] = $paths
                $packages += $paths
                $foundCount++
                Write-Success "平台 $p : 找到 $($paths.Count) 个包"
            }
            else {
                $platformResults[$p] = @()
                $missCount++
                Write-Warning "平台 $p : 未找到本地包"
            }
        }

        Write-Host ""
        Write-Box -Title "全平台扫描结果" -Lines @(
            "找到包的平台: $foundCount / 4",
            "缺失包的平台: $missCount / 4",
            "总共收集到 $($packages.Count) 个安装包"
        )
    }
    else {
        $paths = Locate-OrBuildPackage -Platform $platform -Version $version
        if ($paths) {
            $packages += $paths
        }
    }

    # ======================================================================
    # 步骤 3: 计算 checksum
    # ======================================================================
    Write-Step -Step 3 -Total 4 -Message "计算安装包 checksum"
    if ($packages.Count -gt 0) {
        Write-Info "将为 $($packages.Count) 个安装包计算 SHA256 checksum..."
        foreach ($pkg in $packages) {
            Update-Checksum -PackagePath $pkg
        }
        Update-ChecksumsTxt
    }
    else {
        Write-Warning "没有本地包需要计算 checksum"
    }

    # ======================================================================
    # 步骤 4: 上传到服务器
    # ======================================================================
    Write-Step -Step 4 -Total 4 -Message "上传到服务器"
    Invoke-UploadToServer -Version $version -Packages $packages

    # ======================================================================
    # 完成
    # ======================================================================
    Write-Host ""
    Write-Host "╔════════════════════════════════════════════════════════════════╗" -ForegroundColor Green
    Write-Host "║" -ForegroundColor Green -NoNewline
    Write-Host "                                                                " -NoNewline
    Write-Host "║" -ForegroundColor Green
    Write-Host "║  " -ForegroundColor Green -NoNewline
    Write-Host "部署完成！" -NoNewline
    Write-Host (" " * 54) -NoNewline
    Write-Host "║" -ForegroundColor Green
    Write-Host "║  版本: " -ForegroundColor Green -NoNewline
    Write-Host $version -ForegroundColor Cyan -NoNewline
    Write-Host (" " * (55 - $version.Length)) -NoNewline
    Write-Host "║" -ForegroundColor Green
    Write-Host "║  平台: " -ForegroundColor Green -NoNewline
    Write-Host $platform -ForegroundColor Cyan -NoNewline
    Write-Host (" " * (55 - $platform.Length)) -NoNewline
    Write-Host "║" -ForegroundColor Green
    if ($packages.Count -gt 0) {
        foreach ($pkg in $packages) {
            $fname = Split-Path $pkg -Leaf
            Write-Host "║  包:   " -ForegroundColor Green -NoNewline
            $displayName = if ($fname.Length -gt 48) { $fname.Substring(0, 48) } else { $fname }
            Write-Host $displayName -ForegroundColor Cyan -NoNewline
            Write-Host (" " * (56 - $displayName.Length)) -NoNewline
            Write-Host "║" -ForegroundColor Green
        }
    }
    Write-Host "║  请访问 https://z-pulse.cn 查看效果                          " -ForegroundColor Green -NoNewline
    Write-Host "║" -ForegroundColor Green
    Write-Host "║" -ForegroundColor Green -NoNewline
    Write-Host "                                                                " -NoNewline
    Write-Host "║" -ForegroundColor Green
    Write-Host "╚════════════════════════════════════════════════════════════════╝" -ForegroundColor Green
    Write-Host ""

}

# -----------------------------------------------------------------------------
# 入口
# -----------------------------------------------------------------------------

try {
    Main
}
finally {
    Remove-Item Env:\SSHPASS -ErrorAction SilentlyContinue
    $script:SshPassword = $null
}
