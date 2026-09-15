<#
.SYNOPSIS
    Build both Windows trees and refresh both distribution folders.

.DESCRIPTION
    One command for what used to be five manual steps:
      1. configure build-release and build-debug (only if not yet configured)
      2. compile both with Ninja
      3. copy each exe into its dist folder
      4. run windeployqt there, so DLLs, QML modules and plugins match the
         Qt the exe was built against
      5. verify by CONTENT - the hash of the exe in build-* must equal the
         hash of the exe in dist-* - and refuse to report success otherwise.
    Timestamps are never trusted; a stale copy once cost three rounds of
    re-fixing code that was already fixed.

.PARAMETER Configuration
    Release, Debug, or Both (default).

.PARAMETER Clean
    Delete the build trees first and configure from scratch.

.PARAMETER SkipDeploy
    Compile and copy the exe, but do not run windeployqt (faster when only
    C++ or QML changed and the Qt DLLs are already in place).

.EXAMPLE
    .\scripts\build-windows.ps1
    .\scripts\build-windows.ps1 -Configuration Release
    .\scripts\build-windows.ps1 -Clean
#>
[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug", "Both")]
    [string]$Configuration = "Both",
    [switch]$Clean,
    [switch]$SkipDeploy
)

$ErrorActionPreference = "Stop"

# ---- where things are ---------------------------------------------------------
$QtDir     = "D:\Qt\6.8.2\mingw_64"
$MinGWBin  = "D:\Qt\Tools\mingw1310_64\bin"
$NinjaBin  = "D:\Qt\Tools\Ninja"
$CMakeBin  = "D:\Qt\Tools\CMake_64\bin"
$Project   = Split-Path -Parent $PSScriptRoot

foreach ($p in @("$QtDir\bin\windeployqt.exe", "$MinGWBin\g++.exe", "$NinjaBin\ninja.exe", "$CMakeBin\cmake.exe")) {
    if (-not (Test-Path $p)) { throw "not found: $p - adjust the paths at the top of this script" }
}
$env:PATH = "$QtDir\bin;$MinGWBin;$NinjaBin;$CMakeBin;$env:PATH"
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = "1"

# windeployqt's --release/--debug describes the Qt libraries to ship, not our
# exe. This MinGW kit has no debug Qt (no Qt6Cored.dll, no qwindowsd.dll), so
# the debug exe links the release DLLs and both folders get the same set;
# "--debug" would look for files that do not exist and stop with "Unable to
# find the platform plugin".
$targets = @()
if ($Configuration -in @("Release", "Both")) { $targets += @{ Name = "Release"; Build = "build-release"; Dist = "dist-windows" } }
if ($Configuration -in @("Debug",   "Both")) { $targets += @{ Name = "Debug";   Build = "build-debug";   Dist = "dist-windows-debug" } }

function Invoke-Checked([string]$What, [scriptblock]$Cmd) {
    & $Cmd
    if ($LASTEXITCODE -ne 0) { throw "$What failed with exit code $LASTEXITCODE" }
}

Push-Location $Project
try {
    $results = @()
    foreach ($t in $targets) {
        Write-Host ""
        Write-Host "=== $($t.Name): $($t.Build) -> $($t.Dist)" -ForegroundColor Cyan

        if ($Clean -and (Test-Path $t.Build)) {
            Write-Host "  removing $($t.Build)"
            Remove-Item -Recurse -Force $t.Build
        }
        if (-not (Test-Path "$($t.Build)\build.ninja")) {
            Write-Host "  configuring"
            Invoke-Checked "cmake configure ($($t.Name))" {
                cmake -S . -B $t.Build -G Ninja "-DCMAKE_BUILD_TYPE=$($t.Name)" "-DCMAKE_PREFIX_PATH=$QtDir" | Out-Null
            }
        }

        Write-Host "  compiling"
        Invoke-Checked "ninja ($($t.Name))" { cmake --build $t.Build }

        New-Item -ItemType Directory -Force $t.Dist | Out-Null
        Copy-Item "$($t.Build)\appQmlCommander.exe" "$($t.Dist)\appQmlCommander.exe" -Force
        Copy-Item "resources\app_icon.png" "$($t.Dist)\app_icon.png" -Force

        if (-not $SkipDeploy) {
            Write-Host "  windeployqt"
            Invoke-Checked "windeployqt ($($t.Name))" {
                windeployqt --release --qmldir qml --no-translations --compiler-runtime "$($t.Dist)\appQmlCommander.exe" | Out-Null
            }
        }

        # ---- verify by content, not by date ----------------------------------
        $built  = (Get-FileHash "$($t.Build)\appQmlCommander.exe" -Algorithm MD5).Hash.ToLower()
        $shipped = (Get-FileHash "$($t.Dist)\appQmlCommander.exe" -Algorithm MD5).Hash.ToLower()
        if ($built -ne $shipped) { throw "$($t.Dist)\appQmlCommander.exe does not match $($t.Build) ($shipped vs $built)" }
        $results += "{0,-8} {1}  {2} = {3}" -f $t.Name, $built.Substring(0, 12), $t.Build, $t.Dist
    }

    Write-Host ""
    Write-Host "=== verified (md5 of exe, build tree = dist folder)" -ForegroundColor Green
    $results | ForEach-Object { Write-Host "  $_" }
}
finally {
    Pop-Location
}
