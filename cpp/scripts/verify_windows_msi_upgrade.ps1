#requires -Version 5.1
<#
.SYNOPSIS
    Verifies a real xlsOne MSI major upgrade on a clean Windows machine.

.DESCRIPTION
    Administrative extraction (/a) proves that files exist in an MSI, but it
    does not exercise Windows Installer file-version costing. This CI-only test
    installs an older xlsOne MSI, upgrades it with the candidate, verifies the
    registered product and installed Qt runtime, checks the real Start Menu
    shortcut, and starts the installed executable.

    The previous package intentionally contains a newer Qt runtime than the
    candidate. This catches regressions where Windows Installer skips candidate
    DLLs before RemoveExistingProducts removes the older package.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PreviousMsiPath,

    [Parameter(Mandatory = $true)]
    [string]$CandidateMsiPath,

    [Parameter(Mandatory = $true)]
    [string]$ExpectedVersion,

    [Parameter(Mandatory = $true)]
    [ValidateSet(5, 6)]
    [int]$QtMajor,

    [ValidateRange(2, 30)]
    [int]$StartupTimeoutSeconds = 5,

    # A real MSI test modifies machine-wide Windows Installer state. Keep local
    # execution opt-in so this script cannot replace a developer's installation
    # accidentally. GitHub-hosted runners are disposable and opt in implicitly.
    [switch]$AllowLocalMachineMutation
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw "The MSI upgrade verification must run on Windows."
}
if ($env:GITHUB_ACTIONS -ne "true" -and -not $AllowLocalMachineMutation) {
    throw "This script performs real machine-wide installs. Run it in GitHub Actions or pass -AllowLocalMachineMutation explicitly."
}

$principal = [Security.Principal.WindowsPrincipal]::new(
    [Security.Principal.WindowsIdentity]::GetCurrent()
)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "The MSI upgrade verification requires an elevated Administrator session."
}

$PreviousMsiPath = (Resolve-Path -LiteralPath $PreviousMsiPath).Path
$CandidateMsiPath = (Resolve-Path -LiteralPath $CandidateMsiPath).Path

function Release-ComObject {
    param([AllowNull()][object]$Value)

    if ($null -ne $Value -and [Runtime.InteropServices.Marshal]::IsComObject($Value)) {
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($Value)
    }
}

function Get-MsiScalar {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Query,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $installer = $null
    $database = $null
    $view = $null
    $record = $null
    try {
        $installer = New-Object -ComObject WindowsInstaller.Installer
        $database = $installer.GetType().InvokeMember(
            "OpenDatabase",
            [Reflection.BindingFlags]::InvokeMethod,
            $null,
            $installer,
            @($Path, 0)
        )
        $view = $database.GetType().InvokeMember(
            "OpenView",
            [Reflection.BindingFlags]::InvokeMethod,
            $null,
            $database,
            @($Query)
        )
        [void]$view.GetType().InvokeMember(
            "Execute",
            [Reflection.BindingFlags]::InvokeMethod,
            $null,
            $view,
            @()
        )
        $record = $view.GetType().InvokeMember(
            "Fetch",
            [Reflection.BindingFlags]::InvokeMethod,
            $null,
            $view,
            @()
        )
        if ($null -eq $record) {
            throw "$Description was not found in $Path"
        }
        return [string]$record.GetType().InvokeMember(
            "StringData",
            [Reflection.BindingFlags]::GetProperty,
            $null,
            $record,
            @(1)
        )
    } finally {
        Release-ComObject $record
        Release-ComObject $view
        Release-ComObject $database
        Release-ComObject $installer
    }
}

function Get-MsiProperty {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $escapedName = $Name.Replace("'", "''")
    return Get-MsiScalar `
        -Path $Path `
        -Query "SELECT ``Value`` FROM ``Property`` WHERE ``Property``='$escapedName'" `
        -Description "MSI property $Name"
}

function Get-MsiFileVersion {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$FileName
    )

    # CPack stores names longer than 8.3 as "generated-short|LongName.dll".
    # Windows Installer SQL has no portable suffix-match operator, so inspect
    # the small File table and compare the long-name half explicitly.
    $installer = $null
    $database = $null
    $view = $null
    $record = $null
    try {
        $installer = New-Object -ComObject WindowsInstaller.Installer
        $database = $installer.GetType().InvokeMember(
            "OpenDatabase",
            [Reflection.BindingFlags]::InvokeMethod,
            $null,
            $installer,
            @($Path, 0)
        )
        $view = $database.GetType().InvokeMember(
            "OpenView",
            [Reflection.BindingFlags]::InvokeMethod,
            $null,
            $database,
            @("SELECT ``FileName``, ``Version`` FROM ``File``")
        )
        [void]$view.GetType().InvokeMember(
            "Execute",
            [Reflection.BindingFlags]::InvokeMethod,
            $null,
            $view,
            @()
        )
        while ($true) {
            $record = $view.GetType().InvokeMember(
                "Fetch",
                [Reflection.BindingFlags]::InvokeMethod,
                $null,
                $view,
                @()
            )
            if ($null -eq $record) {
                break
            }
            $storedName = [string]$record.GetType().InvokeMember(
                "StringData",
                [Reflection.BindingFlags]::GetProperty,
                $null,
                $record,
                @(1)
            )
            $longName = ($storedName -split '\|', 2)[-1]
            if ($longName.Equals($FileName, [StringComparison]::OrdinalIgnoreCase)) {
                return [string]$record.GetType().InvokeMember(
                    "StringData",
                    [Reflection.BindingFlags]::GetProperty,
                    $null,
                    $record,
                    @(2)
                )
            }
            Release-ComObject $record
            $record = $null
        }
        throw "MSI file version for $FileName was not found in $Path"
    } finally {
        Release-ComObject $record
        Release-ComObject $view
        Release-ComObject $database
        Release-ComObject $installer
    }
}

function ConvertTo-NormalizedVersion {
    param(
        [Parameter(Mandatory = $true)][string]$Value,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $match = [regex]::Match($Value, '\d+(?:\.\d+){1,3}')
    if (-not $match.Success) {
        throw "$Description is not a numeric file version: $Value"
    }
    $parsed = [Version]$match.Value
    $build = if ($parsed.Build -ge 0) { $parsed.Build } else { 0 }
    $revision = if ($parsed.Revision -ge 0) { $parsed.Revision } else { 0 }
    return [Version]::new($parsed.Major, $parsed.Minor, $build, $revision)
}

function Get-RelatedProductCodes {
    param([Parameter(Mandatory = $true)][string]$UpgradeCode)

    $installer = $null
    $related = $null
    try {
        $installer = New-Object -ComObject WindowsInstaller.Installer
        $related = $installer.GetType().InvokeMember(
            "RelatedProducts",
            [Reflection.BindingFlags]::GetProperty,
            $null,
            $installer,
            @($UpgradeCode)
        )
        $products = @()
        foreach ($product in $related) {
            $products += [string]$product
        }
        return $products
    } finally {
        Release-ComObject $related
        Release-ComObject $installer
    }
}

function Get-InstalledProductInfo {
    param(
        [Parameter(Mandatory = $true)][string]$ProductCode,
        [Parameter(Mandatory = $true)][string]$PropertyName
    )

    $installer = $null
    try {
        $installer = New-Object -ComObject WindowsInstaller.Installer
        return [string]$installer.GetType().InvokeMember(
            "ProductInfo",
            [Reflection.BindingFlags]::GetProperty,
            $null,
            $installer,
            @($ProductCode, $PropertyName)
        )
    } finally {
        Release-ComObject $installer
    }
}

function Get-LogTail {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return "msiexec did not create a log"
    }
    return (Get-Content -LiteralPath $Path -Tail 100) -join [Environment]::NewLine
}

function Invoke-MsiExec {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet("Install", "Uninstall")]
        [string]$Operation,

        [Parameter(Mandatory = $true)][string]$Target,
        [Parameter(Mandatory = $true)][string]$LogPath,
        [int[]]$AllowedExitCodes = @(0, 3010)
    )

    $verb = if ($Operation -eq "Install") { "/i" } else { "/x" }
    $arguments = @(
        $verb,
        ('"{0}"' -f $Target),
        "/qn",
        "/norestart",
        "/l*v",
        ('"{0}"' -f $LogPath)
    )
    $process = Start-Process `
        -FilePath (Join-Path $env:SystemRoot "System32\msiexec.exe") `
        -ArgumentList $arguments `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    try {
        $exitCode = $process.ExitCode
    } finally {
        $process.Dispose()
    }

    if ($exitCode -notin $AllowedExitCodes) {
        $tail = Get-LogTail -Path $LogPath
        throw "MSI $Operation failed for $Target (exit $exitCode).`n$tail"
    }
    Write-Host "[OK] MSI $Operation completed (exit $exitCode)." -ForegroundColor Green
}

function Assert-RequiredRuntime {
    param(
        [Parameter(Mandatory = $true)][string]$InstallRoot,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $binDirectory = Join-Path $InstallRoot "bin"
    $required = @(
        "xlsOneQt.exe",
        "Qt${QtMajor}Core.dll",
        "Qt${QtMajor}Gui.dll",
        "Qt${QtMajor}Widgets.dll",
        "Qt${QtMajor}Network.dll",
        "libgcc_s_seh-1.dll",
        "libstdc++-6.dll",
        "libwinpthread-1.dll",
        "platforms\qwindows.dll"
    )
    foreach ($relativePath in $required) {
        $path = Join-Path $binDirectory $relativePath
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "$Label is missing installed runtime file: $relativePath"
        }
        if ((Get-Item -LiteralPath $path).Length -le 0) {
            throw "$Label contains an empty installed runtime file: $relativePath"
        }
    }
    Write-Host "[OK] $Label contains the installed Qt${QtMajor}, MinGW and platform runtimes." -ForegroundColor Green
    return $binDirectory
}

function Get-StartMenuShortcutTarget {
    $roots = @(
        [Environment]::GetFolderPath([Environment+SpecialFolder]::Programs),
        [Environment]::GetFolderPath([Environment+SpecialFolder]::CommonPrograms)
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) }

    $links = @(
        foreach ($root in $roots) {
            Get-ChildItem -LiteralPath $root -Recurse -File -Filter "xlsOne.lnk" -ErrorAction SilentlyContinue
        }
    )
    if ($links.Count -eq 0) {
        throw "The installed xlsOne Start Menu shortcut was not found."
    }

    $shell = $null
    try {
        $shell = New-Object -ComObject WScript.Shell
        foreach ($link in $links) {
            $shortcut = $null
            try {
                $shortcut = $shell.CreateShortcut($link.FullName)
                if ($shortcut.TargetPath) {
                    return [pscustomobject]@{
                        LinkPath = $link.FullName
                        TargetPath = [string]$shortcut.TargetPath
                    }
                }
            } finally {
                Release-ComObject $shortcut
            }
        }
    } finally {
        Release-ComObject $shell
    }
    throw "The installed xlsOne Start Menu shortcut has no target."
}

function Test-InstalledStartup {
    param(
        [Parameter(Mandatory = $true)][string]$ExecutablePath,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string]$LogDirectory
    )

    if (-not ("XlsOne.MsiUpgradeVerifier.NativeMethods" -as [type])) {
        Add-Type -TypeDefinition @"
namespace XlsOne.MsiUpgradeVerifier {
    public static class NativeMethods {
        [System.Runtime.InteropServices.DllImport("kernel32.dll")]
        public static extern uint SetErrorMode(uint mode);
    }
}
"@
    }

    $stdoutPath = Join-Path $LogDirectory "installed-startup.stdout.log"
    $stderrPath = Join-Path $LogDirectory "installed-startup.stderr.log"
    $previousErrorMode = [XlsOne.MsiUpgradeVerifier.NativeMethods]::SetErrorMode(0x8003)
    $previousPath = $env:PATH
    $savedQtEnvironment = @{}
    foreach ($entry in Get-ChildItem Env:) {
        if ($entry.Name -match '^(QT|QML)') {
            $savedQtEnvironment[$entry.Name] = $entry.Value
            [Environment]::SetEnvironmentVariable(
                $entry.Name,
                $null,
                [EnvironmentVariableTarget]::Process
            )
        }
    }
    $process = $null
    try {
        # install-qt-action adds its development Qt to PATH and QT_PLUGIN_PATH.
        # A package missing a DLL could otherwise start by borrowing that copy.
        # Restrict loader discovery to the installed candidate and Windows.
        $env:PATH = @(
            $WorkingDirectory,
            (Join-Path $env:SystemRoot "System32"),
            $env:SystemRoot,
            (Join-Path $env:SystemRoot "System32\Wbem")
        ) -join ";"
        $env:QT_QPA_PLATFORM = "windows"
        $process = Start-Process `
            -FilePath $ExecutablePath `
            -WorkingDirectory $WorkingDirectory `
            -WindowStyle Hidden `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath `
            -PassThru
        if ($process.WaitForExit($StartupTimeoutSeconds * 1000)) {
            $stderr = if (Test-Path -LiteralPath $stderrPath) {
                (Get-Content -LiteralPath $stderrPath -Raw -ErrorAction SilentlyContinue).Trim()
            } else {
                ""
            }
            throw "The installed executable exited during startup (exit $($process.ExitCode)). $stderr"
        }
        Write-Host "[OK] Installed xlsOne started with the real Windows platform plugin." -ForegroundColor Green
    } finally {
        if ($null -ne $process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            $process.WaitForExit()
        }
        if ($null -ne $process) {
            $process.Dispose()
        }
        foreach ($entry in Get-ChildItem Env:) {
            if ($entry.Name -match '^(QT|QML)') {
                [Environment]::SetEnvironmentVariable(
                    $entry.Name,
                    $null,
                    [EnvironmentVariableTarget]::Process
                )
            }
        }
        foreach ($name in $savedQtEnvironment.Keys) {
            [Environment]::SetEnvironmentVariable(
                $name,
                $savedQtEnvironment[$name],
                [EnvironmentVariableTarget]::Process
            )
        }
        $env:PATH = $previousPath
        [void][XlsOne.MsiUpgradeVerifier.NativeMethods]::SetErrorMode($previousErrorMode)
    }
}

$previousProductCode = Get-MsiProperty -Path $PreviousMsiPath -Name "ProductCode"
$candidateProductCode = Get-MsiProperty -Path $CandidateMsiPath -Name "ProductCode"
$previousUpgradeCode = Get-MsiProperty -Path $PreviousMsiPath -Name "UpgradeCode"
$candidateUpgradeCode = Get-MsiProperty -Path $CandidateMsiPath -Name "UpgradeCode"
$previousProductVersion = Get-MsiProperty -Path $PreviousMsiPath -Name "ProductVersion"
$candidateProductVersion = Get-MsiProperty -Path $CandidateMsiPath -Name "ProductVersion"
$reinstallMode = Get-MsiProperty -Path $CandidateMsiPath -Name "REINSTALLMODE"
$qtCoreName = "Qt${QtMajor}Core.dll"
$previousQtVersion = ConvertTo-NormalizedVersion `
    -Value (Get-MsiFileVersion -Path $PreviousMsiPath -FileName $qtCoreName) `
    -Description "Previous $qtCoreName"
$candidateQtVersion = ConvertTo-NormalizedVersion `
    -Value (Get-MsiFileVersion -Path $CandidateMsiPath -FileName $qtCoreName) `
    -Description "Candidate $qtCoreName"
$candidateRuntimeVersions = @{}
foreach ($fileName in @(
    "Qt${QtMajor}Core.dll",
    "Qt${QtMajor}Gui.dll",
    "Qt${QtMajor}Widgets.dll",
    "Qt${QtMajor}Network.dll",
    "qwindows.dll"
)) {
    $candidateRuntimeVersions[$fileName] = ConvertTo-NormalizedVersion `
        -Value (Get-MsiFileVersion -Path $CandidateMsiPath -FileName $fileName) `
        -Description "Candidate $fileName"
}

if ($previousProductCode -eq $candidateProductCode) {
    throw "Previous and candidate MSIs unexpectedly use the same ProductCode: $candidateProductCode"
}
if ($previousUpgradeCode -ne $candidateUpgradeCode) {
    throw "UpgradeCode mismatch: previous $previousUpgradeCode, candidate $candidateUpgradeCode"
}
if ((ConvertTo-NormalizedVersion $previousProductVersion "Previous ProductVersion") -ge
    (ConvertTo-NormalizedVersion $candidateProductVersion "Candidate ProductVersion")) {
    throw "Candidate product version $candidateProductVersion must be newer than fixture $previousProductVersion."
}
if ($candidateProductVersion -ne $ExpectedVersion) {
    throw "Candidate MSI version $candidateProductVersion does not match expected version $ExpectedVersion."
}
if ($reinstallMode -ne "amus") {
    throw "Candidate MSI must contain REINSTALLMODE=amus; found '$reinstallMode'."
}
if ($previousQtVersion -le $candidateQtVersion) {
    throw "The regression fixture no longer exercises a Qt downgrade: previous $previousQtVersion, candidate $candidateQtVersion."
}

Write-Host "[INFO] Upgrade regression: xlsOne $previousProductVersion / Qt $previousQtVersion -> xlsOne $candidateProductVersion / Qt $candidateQtVersion"

$alreadyInstalled = @(Get-RelatedProductCodes -UpgradeCode $candidateUpgradeCode)
if ($alreadyInstalled.Count -gt 0) {
    throw "Refusing to replace an existing xlsOne installation: $($alreadyInstalled -join ', ')"
}
$defaultInstallRoots = @(
    if ($env:ProgramFiles) { Join-Path $env:ProgramFiles "xlsOne" }
    if (${env:ProgramFiles(x86)}) { Join-Path ${env:ProgramFiles(x86)} "xlsOne" }
) | Select-Object -Unique
$existingInstallRoots = @(
    $defaultInstallRoots | Where-Object { Test-Path -LiteralPath $_ }
)
if ($existingInstallRoots.Count -gt 0) {
    throw "Refusing to overwrite an unregistered or residual xlsOne directory: $($existingInstallRoots -join ', ')"
}

$verificationBase = if ($env:GITHUB_WORKSPACE) {
    Join-Path $env:GITHUB_WORKSPACE ".build"
} else {
    [IO.Path]::GetTempPath()
}
$verificationRoot = Join-Path $verificationBase ("xlsone-msi-upgrade-" + [guid]::NewGuid().ToString("N"))
$previousInstallLog = Join-Path $verificationRoot "previous-install.log"
$candidateInstallLog = Join-Path $verificationRoot "candidate-upgrade.log"
$candidateUninstallLog = Join-Path $verificationRoot "candidate-uninstall.log"
$previousUninstallLog = Join-Path $verificationRoot "previous-uninstall.log"
$installationAttempted = $false
$testPassed = $false
$cleanupProblems = @()

New-Item -ItemType Directory -Path $verificationRoot -Force | Out-Null
try {
    $installationAttempted = $true
    Invoke-MsiExec -Operation Install -Target $PreviousMsiPath -LogPath $previousInstallLog

    $relatedAfterPrevious = @(Get-RelatedProductCodes -UpgradeCode $candidateUpgradeCode)
    if ($previousProductCode -notin $relatedAfterPrevious) {
        throw "Previous product was not registered after installation: $previousProductCode"
    }
    $previousInstallRoot = Get-InstalledProductInfo -ProductCode $previousProductCode -PropertyName "InstallLocation"
    $previousBin = Assert-RequiredRuntime -InstallRoot $previousInstallRoot -Label "previous MSI"
    $installedPreviousQtVersion = ConvertTo-NormalizedVersion `
        -Value ([Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $previousBin $qtCoreName)).FileVersion) `
        -Description "Installed previous $qtCoreName"
    if ($installedPreviousQtVersion -ne $previousQtVersion) {
        throw "Previous installed $qtCoreName is $installedPreviousQtVersion; MSI declares $previousQtVersion."
    }

    Invoke-MsiExec -Operation Install -Target $CandidateMsiPath -LogPath $candidateInstallLog

    $disallowed = @(Select-String `
        -LiteralPath $candidateInstallLog `
        -SimpleMatch "Disallowing installation of component" `
        -ErrorAction SilentlyContinue)
    if ($disallowed.Count -gt 0) {
        $details = ($disallowed | Select-Object -First 10 | ForEach-Object { $_.Line }) -join [Environment]::NewLine
        throw "Candidate upgrade log rejected one or more components.`n$details"
    }

    $relatedAfterCandidate = @(Get-RelatedProductCodes -UpgradeCode $candidateUpgradeCode)
    if ($candidateProductCode -notin $relatedAfterCandidate) {
        throw "Candidate product was not registered after upgrade: $candidateProductCode"
    }
    if ($previousProductCode -in $relatedAfterCandidate) {
        throw "Previous product remained registered after the major upgrade: $previousProductCode"
    }
    if ($relatedAfterCandidate.Count -ne 1) {
        throw "Expected exactly one related product after upgrade; found $($relatedAfterCandidate -join ', ')."
    }

    $registeredVersion = Get-InstalledProductInfo -ProductCode $candidateProductCode -PropertyName "VersionString"
    if ($registeredVersion -ne $ExpectedVersion) {
        throw "Registered candidate version $registeredVersion does not match $ExpectedVersion."
    }
    $candidateInstallRoot = Get-InstalledProductInfo -ProductCode $candidateProductCode -PropertyName "InstallLocation"
    $candidateBin = Assert-RequiredRuntime -InstallRoot $candidateInstallRoot -Label "upgraded candidate MSI"
    foreach ($fileName in $candidateRuntimeVersions.Keys) {
        $relativePath = if ($fileName -eq "qwindows.dll") {
            "platforms\qwindows.dll"
        } else {
            $fileName
        }
        $installedVersion = ConvertTo-NormalizedVersion `
            -Value ([Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $candidateBin $relativePath)).FileVersion) `
            -Description "Installed candidate $fileName"
        $declaredVersion = $candidateRuntimeVersions[$fileName]
        if ($installedVersion -ne $declaredVersion) {
            throw "Installed $fileName is $installedVersion; candidate MSI declares $declaredVersion."
        }
    }
    Write-Host "[OK] Installed Qt libraries and qwindows match the candidate MSI file versions." -ForegroundColor Green

    $shortcut = Get-StartMenuShortcutTarget
    $installedExecutable = Join-Path $candidateBin "xlsOneQt.exe"
    $expectedExecutable = [IO.Path]::GetFullPath($installedExecutable).TrimEnd('\')
    $shortcutExecutable = [IO.Path]::GetFullPath($shortcut.TargetPath).TrimEnd('\')
    if (-not $expectedExecutable.Equals($shortcutExecutable, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Start Menu shortcut targets '$shortcutExecutable'; expected '$expectedExecutable'."
    }
    Write-Host "[OK] Start Menu shortcut targets the installed bin\xlsOneQt.exe." -ForegroundColor Green

    Test-InstalledStartup `
        -ExecutablePath $shortcutExecutable `
        -WorkingDirectory $candidateBin `
        -LogDirectory $verificationRoot

    Write-Host "[OK] Real MSI major-upgrade verification passed." -ForegroundColor Green
    $testPassed = $true
} finally {
    if ($installationAttempted) {
        try {
            Invoke-MsiExec `
                -Operation Uninstall `
                -Target $candidateProductCode `
                -LogPath $candidateUninstallLog `
                -AllowedExitCodes @(0, 1605, 1614, 3010)
        } catch {
            $cleanupProblems += "Candidate cleanup failed: $($_.Exception.Message)"
        }
        try {
            Invoke-MsiExec `
                -Operation Uninstall `
                -Target $previousProductCode `
                -LogPath $previousUninstallLog `
                -AllowedExitCodes @(0, 1605, 1614, 3010)
        } catch {
            $cleanupProblems += "Previous-package cleanup failed: $($_.Exception.Message)"
        }
        try {
            $remainingProducts = @(Get-RelatedProductCodes -UpgradeCode $candidateUpgradeCode)
            if ($remainingProducts.Count -gt 0) {
                $cleanupProblems += "Related products remain registered: $($remainingProducts -join ', ')"
            }
        } catch {
            $cleanupProblems += "Could not verify Windows Installer cleanup: $($_.Exception.Message)"
        }
    }

    if ($testPassed -and $cleanupProblems.Count -eq 0) {
        for ($attempt = 1; $attempt -le 5; $attempt++) {
            try {
                Remove-Item -LiteralPath $verificationRoot -Recurse -Force -ErrorAction Stop
                break
            } catch {
                if ($attempt -eq 5) {
                    Write-Warning "Could not remove verification logs: $verificationRoot"
                } else {
                    Start-Sleep -Milliseconds 200
                }
            }
        }
    } else {
        Write-Warning "MSI verification logs were retained at $verificationRoot"
    }
    if ($cleanupProblems.Count -gt 0) {
        throw "MSI cleanup was incomplete.`n$($cleanupProblems -join [Environment]::NewLine)"
    }
}
