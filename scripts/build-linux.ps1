<#
.SYNOPSIS
    Build the Linux release in Docker and pull the results back to the host.

.DESCRIPTION
      1. make sure the Docker daemon is running (start Docker Desktop and
         wait for it if not - a build launched against a stopped daemon once
         reported success while doing nothing)
      2. docker build -f Dockerfile.linux -t qmlcommander-linux:latest .
         The Dockerfile compiles, packages, and runs two start-up smoke tests.
      3. copy dist-linux/ and QmlCommander-Linux-x64.tar.gz out of the image
         into the project folder, replacing the previous ones
      4. verify by CONTENT: the sha256 of the binary inside the image must
         equal the sha256 of the copy on the host.

.PARAMETER NoCache
    Rebuild every layer, ignoring Docker's cache.

.PARAMETER SkipExtract
    Build the image only; leave dist-linux/ and the tarball as they are.

.EXAMPLE
    .\scripts\build-linux.ps1
    .\scripts\build-linux.ps1 -NoCache
#>
[CmdletBinding()]
param(
    [switch]$NoCache,
    [switch]$SkipExtract
)

$ErrorActionPreference = "Stop"
$Project = Split-Path -Parent $PSScriptRoot
$Image   = "qmlcommander-linux:latest"

function Invoke-Checked([string]$What, [scriptblock]$Cmd) {
    & $Cmd
    if ($LASTEXITCODE -ne 0) { throw "$What failed with exit code $LASTEXITCODE" }
}

# Is the daemon answering? Judged by the exit code alone.
#
# Not "docker info 2>$null": redirecting a native command's stderr turns every
# line it writes there into a PowerShell error record, and $ErrorActionPreference
# = "Stop" then makes it fatal. "docker info" writes warnings to stderr while
# succeeding - "No blkio throttle.read_bps_device support" on this machine - so
# the probe killed the script while the daemon was perfectly healthy.
function Test-DockerUp {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & docker info *>$null
        return ($LASTEXITCODE -eq 0)
    } finally {
        $ErrorActionPreference = $prev
    }
}

# ---- 1. the daemon -----------------------------------------------------------
if (-not (Test-DockerUp)) {
    $desktop = "$env:ProgramFiles\Docker\Docker\Docker Desktop.exe"
    if (-not (Test-Path $desktop)) { throw "Docker daemon is not running and Docker Desktop was not found at $desktop" }
    Write-Host "Docker daemon not running - starting Docker Desktop" -ForegroundColor Yellow
    Start-Process $desktop | Out-Null
    $waited = 0
    $up = $false
    do {
        Start-Sleep -Seconds 3
        $waited += 3
        $up = Test-DockerUp
    } until ($up -or $waited -ge 180)
    if (-not $up) { throw "Docker daemon did not come up within $waited s" }
    Write-Host "  daemon up after $waited s"
}

Push-Location $Project
try {
    # ---- 2. build ------------------------------------------------------------
    Write-Host ""
    Write-Host "=== docker build $Image" -ForegroundColor Cyan
    # A fingerprint of everything the Dockerfile copies in. It goes into the
    # image as a label and is read back afterwards: if the two differ, the
    # build did not run on this tree, whatever "docker build" printed.
    $files = Get-ChildItem src, qml, resources, i18n, CMakeLists.txt, Dockerfile.linux, scripts\seed_testdata.sh -Recurse -File |
             Sort-Object FullName
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $all = New-Object System.IO.MemoryStream
    foreach ($f in $files) {
        $rel = [System.Text.Encoding]::UTF8.GetBytes($f.FullName.Substring($Project.Length))
        $all.Write($rel, 0, $rel.Length)
        $bytes = [System.IO.File]::ReadAllBytes($f.FullName)
        $all.Write($bytes, 0, $bytes.Length)
    }
    $srcHash = ([System.BitConverter]::ToString($sha.ComputeHash($all.ToArray())) -replace "-", "").ToLower().Substring(0, 16)
    Write-Host "  sources fingerprint $srcHash ($($files.Count) files)"

    # Not "$args": that is PowerShell's automatic parameter array, and an
    # assignment to it is silently ignored.
    $buildArgs = @("build", "-f", "Dockerfile.linux", "-t", $Image, "--build-arg", "SRC_HASH=$srcHash", ".")
    if ($NoCache) { $buildArgs += "--no-cache" }
    Invoke-Checked "docker build" { docker @buildArgs }

    # Read the labels as JSON and pick the key here. A --format template with a
    # quoted key inside it loses its quotes on the way to docker, and the Go
    # template then reads the key as a function name.
    $labels = docker image inspect $Image --format "{{json .Config.Labels}}" | ConvertFrom-Json
    $label = $labels."qmlcommander.src_hash"
    if ($label -ne $srcHash) { throw "image carries sources fingerprint '$label', expected '$srcHash' - the build did not run on this tree" }
    Write-Host ("  image created {0}, fingerprint matches" -f (docker image inspect $Image --format "{{.Created}}"))

    if ($SkipExtract) { return }

    # ---- 3. pull the artifacts out ------------------------------------------
    Write-Host ""
    Write-Host "=== extracting dist-linux and the tarball" -ForegroundColor Cyan
    $cid = docker create $Image
    if ($LASTEXITCODE -ne 0) { throw "docker create failed" }
    try {
        if (Test-Path "dist-linux") { Remove-Item -Recurse -Force "dist-linux" }
        Invoke-Checked "docker cp dist-linux" { docker cp "${cid}:/app/dist-linux" "dist-linux" }
        Invoke-Checked "docker cp tarball"    { docker cp "${cid}:/app/QmlCommander-Linux-x64.tar.gz" "QmlCommander-Linux-x64.tar.gz" }
    }
    finally {
        docker rm $cid | Out-Null
    }

    # ---- 4. verify by content -----------------------------------------------
    $inImage = (docker run --rm --entrypoint sh $Image -c "sha256sum /app/dist-linux/bin/appQmlCommander").Split(" ")[0]
    $onHost  = (Get-FileHash "dist-linux\bin\appQmlCommander" -Algorithm SHA256).Hash.ToLower()
    if ($inImage -ne $onHost) { throw "dist-linux\bin\appQmlCommander on the host does not match the image ($onHost vs $inImage)" }

    Write-Host ""
    Write-Host "=== verified (sha256 of the Linux binary, image = host)" -ForegroundColor Green
    Write-Host ("  {0}  {1}" -f $inImage.Substring(0, 12), "dist-linux\bin\appQmlCommander")
    Write-Host ("  {0,10:n0} bytes  QmlCommander-Linux-x64.tar.gz" -f (Get-Item "QmlCommander-Linux-x64.tar.gz").Length)
}
finally {
    Pop-Location
}
