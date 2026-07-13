#requires -Version 5.1
<#
.SYNOPSIS
    Clear the local xlsOne Windows license state so the app returns to unactivated.

.DESCRIPTION
    1. Backs up the current HKCU\Software\xlsOne\xlsOne registry key to
       <repo>\license-backups\xlsone-license-before-cleanup-<timestamp>.reg.
    2. Removes the HKCU\Software\xlsOne\xlsOne\license subkey (token/offline/lastSeenUtc).
    3. Prints the path of the backup file.

    The device fingerprint cache only lives in process memory, so restart xlsOne
    after running this script to ensure a clean unactivated state.
#>
$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$BackupDir = Join-Path $RepoRoot "license-backups"
$RegKey = "HKCU\Software\xlsOne\xlsOne"
$LicenseKey = "HKCU\Software\xlsOne\xlsOne\license"

function Test-RegKey {
    param([string]$Path)
    try {
        $null = Get-Item -Path "Registry::$Path" -ErrorAction Stop
        return $true
    } catch {
        return $false
    }
}

Write-Host "=== xlsOne license cleanup ===" -ForegroundColor Cyan

# Ensure backup directory exists.
if (-not (Test-Path $BackupDir)) {
    New-Item -ItemType Directory -Path $BackupDir | Out-Null
}

# Backup the whole xlsOne registry key before deleting anything.
$Timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$BackupFile = Join-Path $BackupDir "xlsone-license-before-cleanup-$Timestamp.reg"
$regExport = Start-Process -FilePath "reg.exe" -ArgumentList "export", $RegKey, "$BackupFile", "/y" -Wait -PassThru -NoNewWindow
if ($regExport.ExitCode -ne 0) {
    throw "Failed to export registry key: $RegKey"
}
Write-Host "[OK] Backup saved to: $BackupFile" -ForegroundColor Green

# Remove the license subkey if it exists.
if (Test-RegKey -Path $LicenseKey) {
    Remove-Item -Path "Registry::$LicenseKey" -Recurse -Force
    Write-Host "[OK] License registry key removed: $LicenseKey" -ForegroundColor Green
} else {
    Write-Host "[INFO] License registry key not found; nothing to remove." -ForegroundColor Yellow
}

Write-Host "`nDone. Restart xlsOne to enter the unactivated state." -ForegroundColor Cyan
