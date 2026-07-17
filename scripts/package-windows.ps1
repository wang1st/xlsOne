#Requires -Version 5.1
<#
.SYNOPSIS
    xlsOne Windows 安装包一键打包脚本

.DESCRIPTION
    生成 .msi (WiX) 和 .zip 两种安装包。
    本脚本会自动探测运行环境：
      - Windows / WSL / MSYS / Git Bash -> 直接调用 PowerShell 打包脚本
      - Linux + 配置 WINDOWS_BUILD_HOST -> 通过 SSH 在远程 Windows 机器上构建
      - 其他情况 -> 输出手动打包指引

    用法：.\scripts\package-windows.ps1 [选项]

.PARAMETER Clean
    清理构建目录后重新构建

.PARAMETER DryRun
    只显示将要执行的命令，不实际运行

.PARAMETER Sign
    使用 PFX 证书代码签名（需同时提供 -Password）

.PARAMETER Password
    代码签名证书密码

.PARAMETER Help
    显示本帮助
#>

[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$DryRun,
    [string]$Sign,
    [string]$Password,
    [switch]$Help
)

$ErrorActionPreference = "Stop"

# -----------------------------------------------------------------------------
# 颜色与 UI
# -----------------------------------------------------------------------------
function Write-Header {
    $line = "═" * 64
    Write-Host "╔$($line)╗" -ForegroundColor Blue
    Write-Host "║$(' ' * 64)║" -ForegroundColor Blue
    Write-Host "║  xlsOne  Windows 安装包一键打包$((' ' * 33))║" -ForegroundColor Blue
    Write-Host "║  生成 .msi 与 .zip 安装包$((' ' * 39))║" -ForegroundColor Blue
    Write-Host "║$(' ' * 64)║" -ForegroundColor Blue
    Write-Host "╚$($line)╝`n" -ForegroundColor Blue
}

function Write-Info { param([string]$Message) Write-Host "ℹ  $Message" -ForegroundColor Cyan }
function Write-Success { param([string]$Message) Write-Host "✓  $Message" -ForegroundColor Green }
function Write-WarningText { param([string]$Message) Write-Host "⚠  $Message" -ForegroundColor Yellow }
function Write-ErrorText { param([string]$Message) Write-Host "✗  $Message" -ForegroundColor Red }

# -----------------------------------------------------------------------------
# 路径配置
# -----------------------------------------------------------------------------
$ScriptDir = $PSScriptRoot
$ProjectRoot = Split-Path -Parent $ScriptDir
$CppDir = Join-Path $ProjectRoot "cpp"
$PsScript = Join-Path $CppDir "scripts\package_windows_full.ps1"
$BuildDir = Join-Path $CppDir "build-windows-cn-release"

# 远程构建默认配置（可通过环境变量覆盖）
$WindowsBuildHost = if ($env:WINDOWS_BUILD_HOST) { $env:WINDOWS_BUILD_HOST } else { "" }
$WindowsBuildUser = if ($env:WINDOWS_BUILD_USER) { $env:WINDOWS_BUILD_USER } else { "root" }
$WindowsBuildDir = if ($env:WINDOWS_BUILD_DIR) { $env:WINDOWS_BUILD_DIR } else { "C:\xlsone" }
$WindowsBuildPassword = if ($env:WINDOWS_BUILD_PASSWORD) { $env:WINDOWS_BUILD_PASSWORD } else { "" }

# 选项
$CleanFlag = if ($Clean) { "-Clean" } else { "" }
$SignArgs = @()
if ($Sign) {
    if (-not $Password) {
        Write-ErrorText "-Sign 需要同时使用 -Password 指定证书密码"
        exit 1
    }
    $SignArgs = @("-Sign", "-CertFile", $Sign, "-CertPassword", $Password)
}

# -----------------------------------------------------------------------------
# 环境检测
# -----------------------------------------------------------------------------
function Get-OSFamily {
    switch ($env:OS) {
        "Windows_NT" { return "windows" }
    }
    if ($IsWindows) { return "windows" }
    if ($IsLinux) {
        # 检测 WSL
        $unameRelease = (uname -r 2>$null)
        $wslInterop = "/proc/sys/fs/binfmt_misc/WSLInterop"
        if ((Test-Path $wslInterop) -or
            ($unameRelease -match "Microsoft|microsoft") -or
            $env:WSL_DISTRO_NAME) {
            return "wsl"
        }
        return "linux"
    }
    if ($IsMacOS) { return "macos" }
    return "unknown"
}

function Find-PowerShell {
    param([string]$OS)

    if ($OS -eq "wsl") {
        # WSL 优先用 Windows 的 powershell.exe
        $wslPs = Get-Command powershell.exe -ErrorAction SilentlyContinue
        if ($wslPs) { return $wslPs.Source }
    }

    $candidates = @("pwsh", "powershell", "powershell.exe")
    foreach ($cmd in $candidates) {
        $found = Get-Command $cmd -ErrorAction SilentlyContinue
        if ($found) { return $found.Source }
    }
    return $null
}

# -----------------------------------------------------------------------------
# 本地 / WSL 构建
# -----------------------------------------------------------------------------
function Invoke-LocalBuild {
    $os = Get-OSFamily
    $ps = Find-PowerShell -OS $os
    if (-not $ps) {
        Write-ErrorText "未找到 PowerShell，无法运行打包脚本"
        Write-Info "请安装 PowerShell 后重试：https://aka.ms/powershell"
        exit 1
    }

    if (-not (Test-Path $PsScript)) {
        Write-ErrorText "未找到 PowerShell 打包脚本: $PsScript"
        exit 1
    }

    $psScriptPath = $PsScript
    # WSL 调用 Windows 版 powershell.exe 时需要 Windows 路径
    if (($ps -match "powershell\.exe$") -and (Get-Command wslpath -ErrorAction SilentlyContinue)) {
        $psScriptPath = (& wslpath -w $PsScript)
    }

    Write-Info "调用 PowerShell 打包脚本: $psScriptPath"
    Write-Info "PowerShell: $ps"

    $argsList = @("-ExecutionPolicy", "Bypass", "-File", $psScriptPath, "-Domestic")
    if ($CleanFlag) { $argsList += "-Clean" }
    if ($SignArgs) { $argsList += $SignArgs }

    if ($DryRun) {
        Write-Info "[DRY-RUN] 将执行命令:"
        Write-Host "$ps $argsList"
        return
    }

    & $ps @argsList
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Success "Windows 安装包构建完成"
    if (Test-Path $BuildDir) {
        Get-ChildItem -Path $BuildDir -Depth 0 |
            Where-Object { $_.Extension -in ".msi", ".zip" } |
            ForEach-Object { Write-Info "产物: $($_.FullName)" }
    }
}

# -----------------------------------------------------------------------------
# 远程 SSH 构建（Linux 无 WSL 时的备选）
# -----------------------------------------------------------------------------
function Invoke-RemoteBuild {
    if (-not $WindowsBuildHost) { return $false }

    foreach ($cmd in @("ssh", "scp")) {
        if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) {
            Write-ErrorText "远程构建需要 $cmd，请先安装"
            exit 1
        }
    }

    Write-Info "远程 Windows 构建机: ${WindowsBuildUser}@${WindowsBuildHost}"
    Write-Info "远程仓库目录: ${WindowsBuildDir}"

    $sshPrefix = @()
    $scpPrefix = @()
    if ($WindowsBuildPassword) {
        if (-not (Get-Command sshpass -ErrorAction SilentlyContinue)) {
            Write-ErrorText "远程构建配置了密码，需要 sshpass"
            exit 1
        }
        $env:SSHPASS = $WindowsBuildPassword
        $sshPrefix = @("sshpass", "-e")
        $scpPrefix = @("sshpass", "-e")
    }

    $target = "${WindowsBuildUser}@${WindowsBuildHost}"
    $sshOpts = @("-o", "StrictHostKeyChecking=no", "-o", "ConnectTimeout=10")

    if ($DryRun) {
        Write-Info "[DRY-RUN] 将执行远程构建:"
        Write-Host "ssh $sshOpts ${target} `"cd ${WindowsBuildDir} && git pull && powershell -ExecutionPolicy Bypass -File cpp\\scripts\\package_windows_full.ps1 -Domestic ${CleanFlag}`""
        Write-Host "scp ${target}:`"${WindowsBuildDir}\\cpp\\build-windows-cn-release\\*.msi`" ${BuildDir}\"
        Write-Host "scp ${target}:`"${WindowsBuildDir}\\cpp\\build-windows-cn-release\\*.zip`" ${BuildDir}\"
        return $true
    }

    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

    Write-Info "同步远程仓库代码..."
    $pullArgs = $sshPrefix + @("ssh") + $sshOpts + @($target, "cd ${WindowsBuildDir} && git pull")
    & cmd /c ($pullArgs -join " ")
    if ($LASTEXITCODE -ne 0) {
        Write-ErrorText "远程代码同步失败，请检查 WINDOWS_BUILD_DIR 路径和仓库"
        exit 1
    }

    Write-Info "在远程机器上构建 Windows 安装包..."
    $buildArgs = $sshPrefix + @("ssh") + $sshOpts + @($target, "cd ${WindowsBuildDir} && powershell -ExecutionPolicy Bypass -File cpp\scripts\package_windows_full.ps1 -Domestic ${CleanFlag}")
    & cmd /c ($buildArgs -join " ")
    if ($LASTEXITCODE -ne 0) {
        Write-ErrorText "远程构建失败"
        exit 1
    }

    Write-Info "下载构建产物..."
    $msiArgs = $scpPrefix + @("scp") + $sshOpts + @("${target}:${WindowsBuildDir}\cpp\build-windows-cn-release\*.msi", "${BuildDir}\")
    & cmd /c ($msiArgs -join " ")
    if ($LASTEXITCODE -ne 0) { Write-WarningText "未下载到 .msi" }

    $zipArgs = $scpPrefix + @("scp") + $sshOpts + @("${target}:${WindowsBuildDir}\cpp\build-windows-cn-release\*.zip", "${BuildDir}\")
    & cmd /c ($zipArgs -join " ")
    if ($LASTEXITCODE -ne 0) { Write-WarningText "未下载到 .zip" }

    Write-Success "远程构建完成，产物已下载到: $BuildDir"
    return $true
}

# -----------------------------------------------------------------------------
# 帮助信息
# -----------------------------------------------------------------------------
function Show-Help {
    @"
xlsOne Windows 安装包一键打包脚本

用法:
  .\scripts\package-windows.ps1 [选项]

选项:
  -Clean               清理构建目录后重新构建
  -DryRun              只显示将要执行的命令，不实际运行
  -Sign FILE           使用 PFX 证书代码签名（需同时提供 -Password）
  -Password PASS       代码签名证书密码
  -Help                显示本帮助

环境变量（Linux 远程构建时使用）:
  WINDOWS_BUILD_HOST       远程 Windows 构建机 IP/域名
  WINDOWS_BUILD_USER       远程用户名（默认: root）
  WINDOWS_BUILD_DIR        远程仓库路径（默认: C:\xlsone）
  WINDOWS_BUILD_PASSWORD   远程用户密码（留空则使用 SSH key）

说明:
  本脚本优先在 Windows/WSL 本地执行 PowerShell 打包脚本。
  在纯 Linux 上，需要配置 WINDOWS_BUILD_HOST 通过 SSH 调用远程 Windows。
  生成的安装包位于: cpp/build-windows-cn-release/
"@
}

# -----------------------------------------------------------------------------
# 主流程
# -----------------------------------------------------------------------------
if ($Help) {
    Show-Help
    exit 0
}

Write-Header

$os = Get-OSFamily
Write-Info "检测到运行环境: $os"

switch ($os) {
    "windows" { Invoke-LocalBuild }
    "wsl" { Invoke-LocalBuild }
    { $_ -in "linux", "macos", "unknown" } {
        if (-not (Invoke-RemoteBuild)) {
            Write-WarningText "当前系统 ($os) 无法直接生成 Windows 安装包"
            Write-Info "可选方案："
            Write-Info "  1. 在 Windows 或 WSL 中运行本脚本"
            Write-Info "  2. 配置远程 Windows 构建机后重试："
            @"

`$env:WINDOWS_BUILD_HOST = "192.168.1.100"
`$env:WINDOWS_BUILD_USER = "admin"
`$env:WINDOWS_BUILD_DIR = "C:\xlsone"
# 如使用密码登录：
`$env:WINDOWS_BUILD_PASSWORD = "your-password"

"@ | Write-Host
            Write-Info "  3. 手动在 Windows 上运行："
            Write-Info "     powershell -ExecutionPolicy Bypass -File cpp\scripts\package_windows_full.ps1 -Domestic"
            exit 1
        }
    }
    default {
        Write-WarningText "未知运行环境: $os"
        exit 1
    }
}
