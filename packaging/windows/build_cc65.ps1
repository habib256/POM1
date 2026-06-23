<#
.SYNOPSIS
    Compile cc65 from source for POM1 and stage a minimal relocatable bundle.

.DESCRIPTION
    Builds only what the POM1 DevBench needs (no full cc65 snapshot):

        bin\     ca65, ld65, cl65, cc65, ar65
        share\cc65\asminc, include, lib\none.lib

    POM1 linker cfgs live under dev/cc65; stock cc65 cfg/ and other target libs
    are omitted.

    Requires a MinGW toolchain on PATH: LLVM MinGW (winget install
    MartinStorsjo.LLVM-MinGW.UCRT), MSYS2 mingw64, or set POM1_MINGW_BIN.

.PARAMETER Out
    Parent dir for the staged tree (creates <Out>\cc65\). Default: dist\cc65-bundle

.PARAMETER Src
    cc65 source checkout. Default: build\cc65-src (cloned on first run).

.PARAMETER Rev
    Git revision to build. Default: $env:CC65_REV or master.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File packaging\windows\build_cc65.ps1
#>
param(
    [string]$Out = "dist\cc65-bundle",
    [string]$Src = "",
    [string]$Rev = $env:CC65_REV
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrEmpty($Rev)) { $Rev = "master" }

$repo = (Get-Location).Path
if (-not [IO.Path]::IsPathRooted($Out)) {
    $Out = Join-Path $repo $Out
}
if ([string]::IsNullOrEmpty($Src)) {
    $Src = Join-Path $repo "build\cc65-src"
} elseif (-not [IO.Path]::IsPathRooted($Src)) {
    $Src = Join-Path $repo $Src
}
$dst = [IO.Path]::GetFullPath((Join-Path $Out "cc65"))

$bins = @("ca65", "ld65", "cl65", "cc65", "ar65")

function Find-MingwBin {
    if ($env:POM1_MINGW_BIN -and (Test-Path (Join-Path $env:POM1_MINGW_BIN "mingw32-make.exe"))) {
        return [IO.Path]::GetFullPath($env:POM1_MINGW_BIN)
    }
  foreach ($cand in @(
        (Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\llvm-mingw-20260602-ucrt-x86_64\bin"),
        "C:\msys64\mingw64\bin",
        "C:\msys64\ucrt64\bin"
    )) {
        if ($cand -and (Test-Path (Join-Path $cand "mingw32-make.exe"))) {
            return [IO.Path]::GetFullPath($cand)
        }
    }
    $make = (Get-Command mingw32-make.exe -ErrorAction SilentlyContinue)
    if ($make) { return [IO.Path]::GetFullPath((Split-Path -Parent $make.Source)) }
    throw @"
MinGW not found (need gcc + mingw32-make).
Install LLVM MinGW:  winget install -e --id MartinStorsjo.LLVM-MinGW.UCRT
Or MSYS2 mingw64, then set POM1_MINGW_BIN to its bin directory.
"@
}

function Invoke-Native([string]$exe, [string[]]$cmdArgs) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $exe @cmdArgs 2>&1 | Out-Null
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prev
    }
}

function Copy-Tree([string]$from, [string]$to) {
    $from = [IO.Path]::GetFullPath($from)
    $to = [IO.Path]::GetFullPath($to)
    New-Item -ItemType Directory -Force -Path $to | Out-Null
    Copy-Item -Path (Join-Path $from "*") -Destination $to -Recurse -Force
}

function Stage-Pom1Bundle([string]$cc65Root, [string]$bundleRoot) {
    $builtBin = Join-Path $cc65Root "bin"
    if (Test-Path $bundleRoot) { Remove-Item -Recurse -Force $bundleRoot }
    New-Item -ItemType Directory -Force -Path (Join-Path $bundleRoot "bin") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $bundleRoot "share\cc65\lib") | Out-Null

    foreach ($b in $bins) {
        $p = Join-Path $builtBin "$b.exe"
        if (-not (Test-Path $p)) { throw "build missing $p" }
        Copy-Item $p (Join-Path $bundleRoot "bin") -Force
    }
    foreach ($d in @("asminc", "include")) {
        Copy-Tree (Join-Path $cc65Root $d) (Join-Path $bundleRoot "share\cc65\$d")
    }
    $noneLib = Join-Path $cc65Root "lib\none.lib"
    if (-not (Test-Path $noneLib)) { throw "build missing $noneLib" }
    Copy-Item $noneLib (Join-Path $bundleRoot "share\cc65\lib\none.lib") -Force
    Copy-Item (Join-Path $cc65Root "LICENSE") (Join-Path $bundleRoot "LICENSE") -Force
}

function Test-Pom1Bundle([string]$bundleRoot, [string]$cc65Root) {
    $ca65 = Join-Path $bundleRoot "bin\ca65.exe"
    cmd /c "`"$ca65`" --version >nul 2>&1"
    if ($LASTEXITCODE -ne 0) { throw "ca65 --version failed" }

    foreach ($need in @(
        (Join-Path $bundleRoot "share\cc65\asminc\longbranch.mac"),
        (Join-Path $bundleRoot "share\cc65\include\stddef.h"),
        (Join-Path $bundleRoot "share\cc65\lib\none.lib")
    )) {
        if (-not (Test-Path $need)) { throw "bundle incomplete: $need" }
    }

    $testDir = Join-Path ([IO.Path]::GetTempPath()) ("pom1-cc65-test-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $testDir | Out-Null
    try {
        $testC = Join-Path $testDir "t.c"
        $testBin = Join-Path $testDir "t.bin"
        $noneCfg = Join-Path $cc65Root "cfg\none.cfg"
        if (-not (Test-Path $noneCfg)) { throw "self-test cfg missing: $noneCfg" }
        Set-Content -Path $testC -Value "int main(void){ return 0; }" -Encoding ascii
        $homeAbs = (Resolve-Path (Join-Path $bundleRoot "share\cc65")).Path
        $cl65 = Join-Path $bundleRoot "bin\cl65.exe"
        cmd /c "set CC65_HOME=$homeAbs&& `"$cl65`" -t none -C `"$noneCfg`" -o `"$testBin`" `"$testC`" >nul 2>&1"
        if ($LASTEXITCODE -ne 0) { throw "cl65 self-test failed (exit $LASTEXITCODE)" }
        if (-not (Test-Path $testBin)) { throw "cl65 self-test produced no output" }
    } finally {
        Remove-Item -Recurse -Force $testDir -ErrorAction SilentlyContinue
    }
}

Write-Host "[build_cc65] out : $dst"
Write-Host "[build_cc65] src : $Src"
Write-Host "[build_cc65] rev : $Rev"

$mingw = Find-MingwBin
Write-Host "[build_cc65] mingw: $mingw"

if (-not (Test-Path (Join-Path $Src ".git"))) {
    Write-Host "[build_cc65] cloning cc65..."
    New-Item -ItemType Directory -Force -Path (Split-Path $Src -Parent) | Out-Null
    git clone --depth 1 --branch $Rev https://github.com/cc65/cc65 $Src
    if ($LASTEXITCODE -ne 0) {
        # Shallow clone may fail for non-branch revs; fall back to full clone + checkout.
        if (Test-Path $Src) { Remove-Item -Recurse -Force $Src }
        git clone https://github.com/cc65/cc65 $Src
        git -C $Src checkout --quiet $Rev
    }
} else {
    Write-Host "[build_cc65] updating cc65 source..."
    Invoke-Native git @("-C", $Src, "fetch", "--depth", "1", "origin", $Rev) | Out-Null
    $rc = Invoke-Native git @("-C", $Src, "checkout", "--quiet", $Rev)
    if ($rc -ne 0) { Invoke-Native git @("-C", $Src, "checkout", "--quiet", "master") | Out-Null }
}

$env:Path = "$(Join-Path $Src bin);$mingw;$env:Path"
$make = Join-Path $mingw "mingw32-make.exe"

Write-Host "[build_cc65] building tools..."
& $make -C (Join-Path $Src "src") @bins
if ($LASTEXITCODE -ne 0) { throw "cc65 tool build failed" }

if (-not (Test-Path (Join-Path $Src "lib"))) {
    New-Item -ItemType Directory -Force -Path (Join-Path $Src "lib") | Out-Null
}
Write-Host "[build_cc65] building none.lib..."
& $make -C (Join-Path $Src "libsrc") none
if ($LASTEXITCODE -ne 0) { throw "none.lib build failed" }

Write-Host "[build_cc65] staging POM1 bundle..."
Stage-Pom1Bundle $Src $dst
Test-Pom1Bundle $dst $Src

Write-Host "[build_cc65] OK -> $dst"
