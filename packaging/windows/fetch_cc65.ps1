<#
.SYNOPSIS
    Download the official cc65 Windows snapshot and stage a relocatable bundle
    next to POM1.exe so the DevBench (Pom1BenchHost) compiles asm (ca65/ld65)
    AND C (cl65/cc65) with no system cc65 on PATH.

.DESCRIPTION
    Output layout (mirrors tools/build_cc65_bundle.sh — the POSIX builder used on
    Linux/macOS), so POM1 finds it exe-relative at <exe>\cc65\bin and points
    CC65_HOME at <exe>\cc65\share\cc65 (see ensureCc65Home in Pom1BenchHost.cpp):

        <Out>\cc65\bin\{ca65,ld65,cl65,cc65,ar65,co65}.exe
        <Out>\cc65\share\cc65\{asminc,cfg,include,lib,target}
        <Out>\cc65\LICENSE

    The official Windows snapshots ship the data dirs (asminc/include/lib/target)
    directly under the cc65 root with exe-relative defaults; we re-home them under
    share\cc65 so a single CC65_HOME convention works on every platform.

    cc65 is under the zlib license — redistributable.

.PARAMETER Out
    Destination dir that will contain the staged 'cc65' tree.
    Default: dist\cc65-bundle

.PARAMETER Url
    Snapshot URL. Default: $env:POM1_CC65_WIN_URL, else the canonical nightly
    https://cc65.github.io/cc65-snapshot-win64.zip

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File packaging\windows\fetch_cc65.ps1
#>
param(
    [string]$Out = "dist\cc65-bundle",
    [string]$Url = $env:POM1_CC65_WIN_URL
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

if ([string]::IsNullOrEmpty($Url)) {
    $Url = "https://cc65.github.io/cc65-snapshot-win64.zip"
}

$dst = Join-Path $Out "cc65"
Write-Host "[fetch_cc65] url   : $Url"
Write-Host "[fetch_cc65] out   : $dst"

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("cc65-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$zip = Join-Path $tmp "cc65-snapshot.zip"

try {
    Write-Host "[fetch_cc65] downloading..."
    Invoke-WebRequest -Uri $Url -OutFile $zip -UseBasicParsing
    Write-Host "[fetch_cc65] extracting..."
    Expand-Archive -Path $zip -DestinationPath $tmp -Force

    # The snapshot extracts either to a top-level cc65\ dir or straight to bin\.
    $ca65 = Get-ChildItem -Path $tmp -Recurse -Filter "ca65.exe" | Select-Object -First 1
    if (-not $ca65) { throw "ca65.exe not found inside the downloaded snapshot." }
    $src = Split-Path -Parent (Split-Path -Parent $ca65.FullName)   # <root> (bin\ca65.exe -> <root>)

    if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
    New-Item -ItemType Directory -Force -Path (Join-Path $dst "bin") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $dst "share\cc65") | Out-Null

    # Binaries the DevBench shells out to (asm: ca65+ld65, C: cl65+cc65; ar65/co65
    # round out the toolchain).
    foreach ($b in @("ca65","ld65","cl65","cc65","ar65","co65")) {
        $p = Join-Path (Join-Path $src "bin") "$b.exe"
        if (Test-Path $p) { Copy-Item $p (Join-Path $dst "bin") -Force }
        else { Write-Host "[fetch_cc65] WARN: bin\$b.exe missing — skipped" }
    }

    # Runtime data: present either at <root>\<d> (typical win snapshot) or already
    # under <root>\share\cc65\<d>. Re-home both layouts to share\cc65\<d>.
    foreach ($d in @("asminc","cfg","include","lib","target")) {
        $cand = @((Join-Path $src $d), (Join-Path $src "share\cc65\$d"))
        $found = $cand | Where-Object { Test-Path $_ } | Select-Object -First 1
        if ($found) { Copy-Item $found (Join-Path $dst "share\cc65\$d") -Recurse -Force }
        else { Write-Host "[fetch_cc65] WARN: data dir '$d' missing — skipped" }
    }

    # LICENSE (zlib) — copy if present, else drop a redistribution note.
    $lic = @((Join-Path $src "LICENSE"), (Join-Path $src "license.txt")) |
           Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($lic) {
        Copy-Item $lic (Join-Path $dst "LICENSE") -Force
    } else {
        @"
cc65 is distributed under the zlib license. This is a redistribution of the
cc65 cross-development package (https://cc65.github.io/). See the cc65 project
for the full license text:
  https://github.com/cc65/cc65/blob/master/LICENSE
"@ | Set-Content -Path (Join-Path $dst "LICENSE") -Encoding ascii
    }

    # Sanity: the staged ca65 must run.
    $stagedCa65 = Join-Path $dst "bin\ca65.exe"
    & $stagedCa65 --version 2>$null | Out-Null
    Write-Host "[fetch_cc65] OK -> $dst"
}
finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
