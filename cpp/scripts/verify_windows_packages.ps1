<#
.SYNOPSIS
    Verifies that the Windows MSI and portable ZIP are self-contained.

.DESCRIPTION
    Both packages are extracted into isolated temporary directories. The script
    checks the executable, the Qt runtime, the Windows platform plugin and the
    MinGW runtime, verifies the packaged DLL dependency closure with objdump,
    and launches each packaged executable briefly. A missing loader dependency
    therefore fails packaging before an artifact can be published.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$MsiPath,

    [Parameter(Mandatory = $true)]
    [string]$ZipPath,

    [Parameter(Mandatory = $true)]
    [string]$ObjdumpPath,

    [Parameter(Mandatory = $true)]
    [ValidateSet(5, 6)]
    [int]$QtMajor,

    [ValidateRange(2, 30)]
    [int]$StartupTimeoutSeconds = 5
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$MsiPath = (Resolve-Path -LiteralPath $MsiPath).Path
$ZipPath = (Resolve-Path -LiteralPath $ZipPath).Path
$ObjdumpPath = (Resolve-Path -LiteralPath $ObjdumpPath).Path

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw "Windows package verification must run on Windows."
}

$requiredRelativePaths = @(
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

# Only DLLs that ship as part of supported Windows installations belong here.
# Toolchain redistributables such as vcruntime*, msvcp* and MinGW DLLs must be
# present in the package and are deliberately excluded from this allowlist.
$windowsInboxDlls = [System.Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase
)
@(
    "advapi32.dll",
    "api-ms-win-core-synch-l1-2-0.dll",
    "api-ms-win-core-winrt-l1-1-0.dll",
    "api-ms-win-core-winrt-string-l1-1-0.dll",
    "authz.dll",
    "avrt.dll",
    "bcrypt.dll",
    "comdlg32.dll",
    "crypt32.dll",
    "d3d9.dll",
    "d3d11.dll",
    "dnsapi.dll",
    "dwmapi.dll",
    "dxgi.dll",
    "dwrite.dll",
    "gdi32.dll",
    "imm32.dll",
    "imagehlp.dll",
    "iphlpapi.dll",
    "kernel32.dll",
    "mpr.dll",
    "msvcrt.dll",
    "netapi32.dll",
    "ncrypt.dll",
    "ntdll.dll",
    "ole32.dll",
    "oleaut32.dll",
    "propsys.dll",
    "rpcrt4.dll",
    "secur32.dll",
    "setupapi.dll",
    "shcore.dll",
    "shell32.dll",
    "shlwapi.dll",
    "user32.dll",
    "userenv.dll",
    "uxtheme.dll",
    "version.dll",
    "winhttp.dll",
    "winmm.dll",
    "winspool.drv",
    "ws2_32.dll",
    "wtsapi32.dll"
) | ForEach-Object { [void]$windowsInboxDlls.Add($_) }

function Get-PackagedBinDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,

        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    $executables = @(Get-ChildItem -LiteralPath $Root -Recurse -File -Filter "xlsOneQt.exe")
    if ($executables.Count -ne 1) {
        throw "$Label must contain exactly one xlsOneQt.exe; found $($executables.Count)."
    }

    return $executables[0].Directory.FullName
}

function Assert-RequiredRuntime {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BinDirectory,

        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    foreach ($relativePath in $requiredRelativePaths) {
        $path = Join-Path $BinDirectory $relativePath
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "$Label is missing required runtime file: $relativePath"
        }
        if ((Get-Item -LiteralPath $path).Length -le 0) {
            throw "$Label contains an empty runtime file: $relativePath"
        }
    }

    Write-Host "[OK] $Label contains the Qt${QtMajor}, MinGW and platform runtimes." -ForegroundColor Green
}

function Assert-DllDependencyClosure {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PackageRoot,

        [Parameter(Mandatory = $true)]
        [string]$BinDirectory,

        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    $binaries = @(
        Get-ChildItem -LiteralPath $PackageRoot -Recurse -File |
            Where-Object { $_.Extension -in ".exe", ".dll" }
    )
    if ($binaries.Count -eq 0) {
        throw "$Label contains no Windows binaries."
    }

    $missing = @{}
    foreach ($binary in $binaries) {
        $imports = [System.Collections.Generic.HashSet[string]]::new(
            [StringComparer]::OrdinalIgnoreCase
        )

        # Process one PE at a time and retain only import names. Keeping full
        # objdump output for all Qt DLLs can otherwise consume hundreds of MB.
        & $ObjdumpPath -p $binary.FullName 2>$null | ForEach-Object {
            if ($_ -match '^\s*DLL Name:\s*(.+?)\s*$') {
                [void]$imports.Add($Matches[1])
            }
        }
        if ($LASTEXITCODE -ne 0) {
            throw "objdump failed for $($binary.FullName) in $Label (exit $LASTEXITCODE)."
        }

        foreach ($dependency in $imports) {
            $dependencyKey = $dependency.ToLowerInvariant()
            $besideBinary = Join-Path $binary.Directory.FullName $dependency
            $inApplicationBin = Join-Path $BinDirectory $dependency
            if (
                (Test-Path -LiteralPath $besideBinary -PathType Leaf) -or
                (Test-Path -LiteralPath $inApplicationBin -PathType Leaf)
            ) {
                continue
            }
            if ($windowsInboxDlls.Contains($dependencyKey)) {
                continue
            }

            if (-not $missing.ContainsKey($dependencyKey)) {
                $missing[$dependencyKey] = [System.Collections.Generic.HashSet[string]]::new(
                    [StringComparer]::OrdinalIgnoreCase
                )
            }
            [void]$missing[$dependencyKey].Add($binary.Name)
        }
    }

    if ($missing.Count -gt 0) {
        $details = @(
            foreach ($entry in ($missing.GetEnumerator() | Sort-Object Name)) {
                "{0} (required by {1})" -f $entry.Key, (($entry.Value | Sort-Object) -join ", ")
            }
        ) -join "; "
        throw "$Label has non-system DLL dependencies missing from the package: $details"
    }

    Write-Host "[OK] $Label DLL dependency closure is complete ($($binaries.Count) binaries)." -ForegroundColor Green
}

function Test-PackagedStartup {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BinDirectory,

        [Parameter(Mandatory = $true)]
        [string]$Label,

        [Parameter(Mandatory = $true)]
        [string]$LogDirectory
    )

    if (-not ("XlsOne.PackageVerifier.NativeMethods" -as [type])) {
        Add-Type -TypeDefinition @"
namespace XlsOne.PackageVerifier {
    public static class NativeMethods {
        [System.Runtime.InteropServices.DllImport("kernel32.dll")]
        public static extern uint SetErrorMode(uint mode);
    }
}
"@
    }

    $safeLabel = $Label -replace '[^A-Za-z0-9_.-]', '_'
    $stdoutPath = Join-Path $LogDirectory "$safeLabel.stdout.log"
    $stderrPath = Join-Path $LogDirectory "$safeLabel.stderr.log"
    $exePath = Join-Path $BinDirectory "xlsOneQt.exe"
    $previousErrorMode = [XlsOne.PackageVerifier.NativeMethods]::SetErrorMode(0x8003)
    $previousPlatform = $env:QT_QPA_PLATFORM
    $process = $null

    try {
        # The real Windows plugin exercises plugin discovery. Suppressing OS
        # loader dialogs makes a missing DLL return an actionable exit code.
        $env:QT_QPA_PLATFORM = "windows"
        $startArguments = @{
            FilePath = $exePath
            WorkingDirectory = $BinDirectory
            WindowStyle = "Hidden"
            RedirectStandardOutput = $stdoutPath
            RedirectStandardError = $stderrPath
            PassThru = $true
        }
        $process = Start-Process @startArguments

        if ($process.WaitForExit($StartupTimeoutSeconds * 1000)) {
            $stderr = if (Test-Path -LiteralPath $stderrPath) {
                (Get-Content -LiteralPath $stderrPath -Raw -ErrorAction SilentlyContinue).Trim()
            } else {
                ""
            }
            throw "$Label executable exited during startup smoke test (exit $($process.ExitCode)). $stderr"
        }

        Write-Host "[OK] $Label executable started with its packaged runtime." -ForegroundColor Green
    } finally {
        if ($null -ne $process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            $process.WaitForExit()
        }
        if ($null -ne $process) {
            $process.Dispose()
        }
        $env:QT_QPA_PLATFORM = $previousPlatform
        [void][XlsOne.PackageVerifier.NativeMethods]::SetErrorMode($previousErrorMode)
    }
}

$verificationRoot = Join-Path ([IO.Path]::GetTempPath()) ("xlsone-package-verification-" + [guid]::NewGuid().ToString("N"))
$zipRoot = Join-Path $verificationRoot "zip"
$msiRoot = Join-Path $verificationRoot "msi"
$msiLog = Join-Path $verificationRoot "msiexec.log"

New-Item -ItemType Directory -Path $zipRoot, $msiRoot -Force | Out-Null
try {
    # ZipFile is dramatically faster than Expand-Archive in Windows PowerShell
    # 5.1 and keeps local/CI verification time predictable.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($ZipPath, $zipRoot)
    $zipBin = Get-PackagedBinDirectory -Root $zipRoot -Label "portable ZIP"
    Assert-RequiredRuntime -BinDirectory $zipBin -Label "portable ZIP"
    Assert-DllDependencyClosure -PackageRoot $zipRoot -BinDirectory $zipBin -Label "portable ZIP"
    Test-PackagedStartup -BinDirectory $zipBin -Label "portable ZIP" -LogDirectory $verificationRoot

    $msiArguments = @(
        "/a",
        ('"{0}"' -f $MsiPath),
        "/qn",
        ('TARGETDIR="{0}"' -f $msiRoot),
        "/l*v",
        ('"{0}"' -f $msiLog)
    )
    $msiStartArguments = @{
        FilePath = "msiexec.exe"
        ArgumentList = $msiArguments
        WindowStyle = "Hidden"
        Wait = $true
        PassThru = $true
    }
    $msiProcess = Start-Process @msiStartArguments
    $msiExitCode = $msiProcess.ExitCode
    $msiProcess.Dispose()
    if ($msiExitCode -ne 0) {
        $tail = if (Test-Path -LiteralPath $msiLog) {
            (Get-Content -LiteralPath $msiLog -Tail 80) -join [Environment]::NewLine
        } else {
            "msiexec did not create a log"
        }
        throw "MSI administrative extraction failed (exit $msiExitCode).`n$tail"
    }

    $msiBin = Get-PackagedBinDirectory -Root $msiRoot -Label "MSI"
    Assert-RequiredRuntime -BinDirectory $msiBin -Label "MSI"
    Assert-DllDependencyClosure -PackageRoot $msiRoot -BinDirectory $msiBin -Label "MSI"
    Test-PackagedStartup -BinDirectory $msiBin -Label "MSI" -LogDirectory $verificationRoot

    Write-Host "[OK] Windows MSI and ZIP package verification passed." -ForegroundColor Green
} finally {
    # The directory is generated above and never accepts caller-controlled input.
    for ($attempt = 1; $attempt -le 5; $attempt++) {
        try {
            Remove-Item -LiteralPath $verificationRoot -Recurse -Force -ErrorAction Stop
            break
        } catch {
            if ($attempt -eq 5) {
                Write-Warning "Could not remove package verification directory: $verificationRoot"
            } else {
                Start-Sleep -Milliseconds 200
            }
        }
    }
}
