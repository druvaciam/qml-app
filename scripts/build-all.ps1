<#
.SYNOPSIS
    Everything: both Windows trees, both Windows dist folders, the Linux
    Docker image, and the Linux artifacts on the host.

.DESCRIPTION
    Runs build-windows.ps1 and then build-linux.ps1. Stops at the first
    failure. Every step verifies its output by content, so a green finish
    means every artifact was rebuilt from the sources as they are now.

.EXAMPLE
    .\scripts\build-all.ps1
    .\scripts\build-all.ps1 -Clean -NoCache     # from scratch, both sides
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$NoCache
)

$ErrorActionPreference = "Stop"
$sw = [System.Diagnostics.Stopwatch]::StartNew()

& "$PSScriptRoot\build-windows.ps1" -Clean:$Clean
& "$PSScriptRoot\build-linux.ps1"   -NoCache:$NoCache

Write-Host ""
Write-Host ("=== all artifacts rebuilt and verified in {0:n0} s" -f $sw.Elapsed.TotalSeconds) -ForegroundColor Green
