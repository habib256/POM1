<#
.SYNOPSIS
    Download the official cc65 Windows snapshot and stage a relocatable bundle
    next to POM1.exe so the DevBench (Pom1BenchHost) compiles asm (ca65/ld65)
    AND C (cl65/cc65) with no system cc65 on PATH.

.DESCRIPTION
    Output layout (mirrors tools/build_cc65_bundle.sh — the POSIX builder used on
    Linux/macOS), so POM1 finds it exe-relative at <exe>\cc65\bin and points
    CC65_HOME at <exe>\cc65\share\cc65 (see ensureCc65Home in Pom1BenchHost.cpp):

        <Out>\cc65\bin\{ca65,ld65,cl65,cc65,ar65}.exe
        <Out>\cc65\share\cc65\{asminc,include,lib\none.lib}
        <Out>\cc65\LICENSE

    The official Windows snapshots ship the data dirs (asminc/include/lib/target)
    directly under the cc65 root with exe-relative defaults; we re-home them under
    share\cc65 so a single CC65_HOME convention works on every platform.

    cc65 is under the zlib license — redistributable.

.PARAMETER Out
    Destination dir that will contain the staged 'cc65' tree.
    Default: dist\cc65-bundle

.PARAMETER Url
    Snapshot URL. Default: $env:POM1_CC65_WIN_URL, else the official SourceForge
    snapshot (https://sourceforge.net/projects/cc65/files/cc65-snapshot-win64.zip)

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
    $Url = "https://sourceforge.net/projects/cc65/files/cc65-snapshot-win64.zip/download"
}

if (-not [IO.Path]::IsPathRooted($Out)) {
    $Out = Join-Path (Get-Location) $Out
}
$dst = [IO.Path]::GetFullPath((Join-Path $Out "cc65"))
Write-Host "[fetch_cc65] url   : $Url"
Write-Host "[fetch_cc65] out   : $dst"

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("cc65-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$zip = Join-Path $tmp "cc65-snapshot.zip"
$extract = Join-Path $tmp "snap"

function Copy-Cc65Tree([string]$from, [string]$to) {
    $from = [IO.Path]::GetFullPath($from)
    $to = [IO.Path]::GetFullPath($to)
    New-Item -ItemType Directory -Force -Path $to | Out-Null
    Copy-Item -Path (Join-Path $from "*") -Destination $to -Recurse -Force
}

try {
    Write-Host "[fetch_cc65] downloading..."
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($curl) {
        & curl.exe -L --retry 5 --retry-delay 3 -o $zip $Url
        if ($LASTEXITCODE -ne 0) { throw "curl download failed (exit $LASTEXITCODE)" }
    } else {
        Invoke-WebRequest -Uri $Url -OutFile $zip -UseBasicParsing
    }
    if ((Get-Item $zip).Length -lt 1MB) { throw "downloaded file too small (expected zip, got HTML error page?)" }
    Write-Host "[fetch_cc65] extracting..."
    New-Item -ItemType Directory -Force -Path $extract | Out-Null
    Expand-Archive -Path $zip -DestinationPath $extract -Force

    # The snapshot extracts either to a top-level cc65\ dir or straight to bin\.
    $ca65 = Get-ChildItem -Path $extract -Recurse -Filter "ca65.exe" | Select-Object -First 1
    if (-not $ca65) { throw "ca65.exe not found inside the downloaded snapshot." }
    $src = Split-Path -Parent (Split-Path -Parent $ca65.FullName)   # <root> (bin\ca65.exe -> <root>)

    if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
    New-Item -ItemType Directory -Force -Path (Join-Path $dst "bin") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $dst "share\cc65") | Out-Null

    # Binaries: asm (ca65+ld65), C (cl65+cc65), ar65 (gfx-*.lib archives).
    foreach ($b in @("ca65","ld65","cl65","cc65","ar65")) {
        $p = Join-Path (Join-Path $src "bin") "$b.exe"
        if (Test-Path $p) { Copy-Item $p (Join-Path $dst "bin") -Force }
        else { Write-Host "[fetch_cc65] WARN: bin\$b.exe missing - skipped" }
    }

    # Runtime data: asminc + include for cl65; none.lib for -t none C builds.
    # POM1 linker cfgs live under dev/cc65 (stock cc65 cfg/ not needed).
    foreach ($d in @("asminc","include")) {
        $cand = @((Join-Path $src $d), (Join-Path $src "share\cc65\$d"))
        $found = $cand | Where-Object { Test-Path $_ } | Select-Object -First 1
        if ($null -ne $found -and $found -ne "") {
            Copy-Cc65Tree ([string]$found) (Join-Path $dst "share\cc65\$d")
        }
        else { Write-Host "[fetch_cc65] WARN: data dir '$d' missing - skipped" }
    }
    $libSrc = @((Join-Path $src "lib"), (Join-Path $src "share\cc65\lib")) |
              Where-Object { Test-Path (Join-Path $_ "none.lib") } | Select-Object -First 1
    if ($libSrc) {
        $libDst = Join-Path $dst "share\cc65\lib"
        New-Item -ItemType Directory -Force -Path $libDst | Out-Null
        Copy-Item (Join-Path $libSrc "none.lib") (Join-Path $libDst "none.lib") -Force
    } else {
        Write-Host "[fetch_cc65] WARN: lib\none.lib missing in snapshot"
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

    # Sanity + self-test (cl65 -t none needs asminc, include, none.lib).
    $stagedCa65 = Join-Path $dst "bin\ca65.exe"
    cmd /c "`"$stagedCa65`" --version >nul 2>&1"
    if ($LASTEXITCODE -ne 0) { throw "staged ca65.exe failed --version (exit $LASTEXITCODE)" }
    foreach ($need in @(
        (Join-Path $dst "share\cc65\asminc\longbranch.mac"),
        (Join-Path $dst "share\cc65\include\stddef.h"),
        (Join-Path $dst "share\cc65\lib\none.lib")
    )) {
        if (-not (Test-Path $need)) { throw "bundle incomplete: missing $need" }
    }
    $testDir = Join-Path $tmp "selftest"
    New-Item -ItemType Directory -Force -Path $testDir | Out-Null
    $testCfg = Join-Path $testDir "selftest.cfg"
    @"
MEMORY {
    RAM: start = `$0200, size = `$1000, type = rw, file = %O;
}
SEGMENTS {
    CODE: load = RAM, type = ro;
}
"@ | Set-Content -Path $testCfg -Encoding ascii
    $testC = Join-Path $testDir "t.c"
    Set-Content -Path $testC -Value "int main(void){ return 0; }" -Encoding ascii
    $homeAbs = (Resolve-Path (Join-Path $dst "share\cc65")).Path
    $cl65 = Join-Path $dst "bin\cl65.exe"
    $testBin = Join-Path $testDir "t.bin"
    cmd /c "set CC65_HOME=$homeAbs&& `"$cl65`" -t none -C `"$testCfg`" -o `"$testBin`" `"$testC`" >nul 2>&1"
    if ($LASTEXITCODE -ne 0) { throw "cl65 self-test failed (exit $LASTEXITCODE)" }
    Write-Host "[fetch_cc65] OK -> $dst"
}
finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
