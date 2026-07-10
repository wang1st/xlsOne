#requires -Version 5.1
param(
    [switch]$Domestic
)
$ErrorActionPreference = "Stop"

function Find-QtRoot {
    $candidates = @(
        "C:\Qt\6.11.1\mingw_64",
        "C:\Qt\6.8.3\mingw_64"
    )
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c "bin\qmake.exe")) {
            return $c
        }
    }
    throw "Qt not found"
}

$QtRoot = Find-QtRoot
Write-Host "Qt: $QtRoot"

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Write-Host "Repo: $RepoRoot"

if ($Domestic) {
    Write-Host "Domestic build"
} else {
    Write-Host "International build"
}
