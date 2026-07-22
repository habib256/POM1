@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

echo ============================================
echo  POM1 — Package Windows pour distribution
echo ============================================
echo.

REM ---------------------------------------------------------------------------
REM Optional Authenticode signing
REM
REM This packager can sign the produced binaries if you provide a code-signing
REM certificate via environment variables. No secrets are stored in the repo.
REM
REM Modes:
REM   1) PFX file:
REM      set POM1_CODESIGN_PFX=C:\path\to\cert.pfx
REM      set POM1_CODESIGN_PFX_PASSWORD=yourPassword
REM
REM   2) Certificate in Windows cert store (recommended for EV tokens):
REM      set POM1_CODESIGN_SHA1=THUMBPRINTWITHOUTSPACES
REM
REM Optional:
REM   set POM1_CODESIGN_TIMESTAMP_URL=http://timestamp.digicert.com
REM   set POM1_CODESIGN_DESC=POM1 Apple 1 Emulator
REM   set POM1_CODESIGN_URL=https://github.com/habib256/POM1
REM
REM If none is set, packaging proceeds unsigned (SmartScreen may warn).
REM ---------------------------------------------------------------------------

set "EXE="
if exist "build\Release\POM1.exe" set "EXE=build\Release\POM1.exe"
if exist "build\Debug\POM1.exe" if not defined EXE set "EXE=build\Debug\POM1.exe"
if exist "build\POM1.exe" if not defined EXE set "EXE=build\POM1.exe"

if not defined EXE (
    echo ERREUR: POM1.exe introuvable. Compilez en Release :
    echo   cd build
    echo   cmake --build . --config Release
    exit /b 1
)

if not exist "fonts\fa-solid-900.ttf" (
    echo ERREUR: fonts\fa-solid-900.ttf introuvable.
    exit /b 1
)

if not exist "roms\WozMonitor.rom" (
    echo ERREUR: roms\WozMonitor.rom introuvable — le package sera incomplet.
    exit /b 1
)

REM ---- ROMs DevBench --------------------------------------------------------
REM CODETANKDEV.rom : cartouche de flash DevBench (deux banks $FF, generee,
REM jamais commitee — l'Applesoft TMS vit dans Codetank_BASIC_LOGO.rom).
REM Sa generation ne requiert AUCUNE toolchain.
REM applesoft-gen2.rom : interpreteur Applesoft GEN2 HGR (DevBench BASIC .apf).
if not exist "roms\codetank\CODETANKDEV.rom" (
    echo Generation de roms\codetank\CODETANKDEV.rom...
    python tools\build_codetank_rom.py --rom dev || (
        echo ERREUR: echec generation CODETANKDEV.rom
        exit /b 1
    )
)
if not exist "roms\applesoft-gen2.rom" (
    echo AVERTISSEMENT: roms\applesoft-gen2.rom absente — DevBench Applesoft GEN2 limite.
)

REM Version: honor POM1_VERSION (release workflow sets it from the git tag),
REM else read the repo-root VERSION file (the single source of truth). Never
REM hardcode the version here — see CLAUDE.md.
set "POM1_VER=%POM1_VERSION%"
if not defined POM1_VER set /p POM1_VER=<"%~dp0VERSION"
set "OUTDIR=dist\POM1-Windows"
set "ZIPNAME=POM1-Windows-v%POM1_VER%.zip"
set "ZIPPATH=dist\%ZIPNAME%"

if exist "%OUTDIR%" rd /s /q "%OUTDIR%"
mkdir "%OUTDIR%" || exit /b 1

echo Copie de l'executable...
copy /Y "%EXE%" "%OUTDIR%\POM1.exe" >nul || exit /b 1

REM GLFW via vcpkg (triplet dynamique) : glfw3.dll doit etre a cote de l'exe
set "GLFW_DLL_SRC="
set "TRIPLET=x64-windows"
set "VPKGINST="
set "ISDBG=0"
if not "%EXE:\Debug\=%"=="%EXE%" set "ISDBG=1"

if exist "build\CMakeCache.txt" (
    for /f "usebackq tokens=2 delims==" %%i in (`findstr /B /C:"VCPKG_TARGET_TRIPLET:" "build\CMakeCache.txt" 2^>nul`) do set "TRIPLET=%%i"
    for /f "usebackq tokens=2 delims==" %%i in (`findstr /B /C:"VCPKG_INSTALLED_DIR:" "build\CMakeCache.txt" 2^>nul`) do set "VPKGINST=%%i"
)
set "TRIPLET=!TRIPLET: =!"

if defined VCPKG_ROOT (
    if "!ISDBG!"=="1" (
        if exist "!VCPKG_ROOT!\installed\!TRIPLET!\debug\bin\glfw3.dll" set "GLFW_DLL_SRC=!VCPKG_ROOT!\installed\!TRIPLET!\debug\bin\glfw3.dll"
    )
    if not defined GLFW_DLL_SRC if exist "!VCPKG_ROOT!\installed\!TRIPLET!\bin\glfw3.dll" set "GLFW_DLL_SRC=!VCPKG_ROOT!\installed\!TRIPLET!\bin\glfw3.dll"
)

if not defined GLFW_DLL_SRC if defined VPKGINST (
    if "!ISDBG!"=="1" (
        if exist "!VPKGINST!\!TRIPLET!\debug\bin\glfw3.dll" set "GLFW_DLL_SRC=!VPKGINST!\!TRIPLET!\debug\bin\glfw3.dll"
    )
    if not defined GLFW_DLL_SRC if exist "!VPKGINST!\!TRIPLET!\bin\glfw3.dll" set "GLFW_DLL_SRC=!VPKGINST!\!TRIPLET!\bin\glfw3.dll"
)

if not defined GLFW_DLL_SRC if exist "dist\POM1-Windows\glfw3.dll" (
    set "GLFW_DLL_SRC=dist\POM1-Windows\glfw3.dll"
)

if not defined GLFW_DLL_SRC (
    echo ERREUR: glfw3.dll introuvable pour le package.
    echo   Definissez VCPKG_ROOT ^(racine vcpkg^), ou reconfigurez CMake avec le toolchain vcpkg
    echo   pour que build\CMakeCache.txt contienne VCPKG_INSTALLED_DIR.
    echo   Fichier attendu : ...\installed\!TRIPLET!\bin\glfw3.dll
    echo   ^(build Debug : ...\debug\bin\glfw3.dll^)
    rd /s /q "%OUTDIR%"
    exit /b 1
)

echo Copie glfw3.dll ^(!GLFW_DLL_SRC!^)...
copy /Y "!GLFW_DLL_SRC!" "%OUTDIR%\glfw3.dll" >nul || (
    echo ERREUR: copie de glfw3.dll echouee.
    rd /s /q "%OUTDIR%"
    exit /b 1
)

REM ---- Optional signing (POM1.exe + glfw3.dll) -------------------------------
set "SIGNTOOL="
for /f "usebackq delims=" %%S in (`where signtool 2^>nul`) do (
    if not defined SIGNTOOL set "SIGNTOOL=%%S"
)
if not defined SIGNTOOL (
    REM Common Windows Kits fallback (best-effort)
    if exist "%ProgramFiles(x86)%\Windows Kits\10\bin\x64\signtool.exe" set "SIGNTOOL=%ProgramFiles(x86)%\Windows Kits\10\bin\x64\signtool.exe"
)

set "TSURL=%POM1_CODESIGN_TIMESTAMP_URL%"
if not defined TSURL set "TSURL=http://timestamp.digicert.com"

set "SIGNDESC=%POM1_CODESIGN_DESC%"
if not defined SIGNDESC set "SIGNDESC=POM1 - Apple 1 Emulator"

set "SIGNURL=%POM1_CODESIGN_URL%"
if not defined SIGNURL set "SIGNURL=https://github.com/habib256/POM1"

set "DO_SIGN=0"
if defined POM1_CODESIGN_PFX set "DO_SIGN=1"
if defined POM1_CODESIGN_SHA1 set "DO_SIGN=1"

if "!DO_SIGN!"=="1" (
    if not defined SIGNTOOL (
        echo AVERTISSEMENT: signature demandee mais signtool.exe introuvable. Packaging non signe.
    ) else (
        echo.
        echo Signature Authenticode...

        set "SIGNARGS=/fd SHA256 /td SHA256 /tr !TSURL! /d "!SIGNDESC!" /du "!SIGNURL!""
        set "CERTARGS="
        if defined POM1_CODESIGN_PFX (
            if not exist "!POM1_CODESIGN_PFX!" (
                echo ERREUR: POM1_CODESIGN_PFX pointe vers un fichier inexistant: !POM1_CODESIGN_PFX!
                rd /s /q "%OUTDIR%"
                exit /b 1
            )
            set "CERTARGS=/f "!POM1_CODESIGN_PFX!""
            if defined POM1_CODESIGN_PFX_PASSWORD (
                set "CERTARGS=!CERTARGS! /p "!POM1_CODESIGN_PFX_PASSWORD!""
            )
        ) else if defined POM1_CODESIGN_SHA1 (
            set "CERTARGS=/sha1 !POM1_CODESIGN_SHA1!"
        )

        "%SIGNTOOL%" sign !SIGNARGS! !CERTARGS! "%OUTDIR%\POM1.exe" || (
            echo ERREUR: echec signature POM1.exe
            rd /s /q "%OUTDIR%"
            exit /b 1
        )
        "%SIGNTOOL%" sign !SIGNARGS! !CERTARGS! "%OUTDIR%\glfw3.dll" || (
            echo ERREUR: echec signature glfw3.dll
            rd /s /q "%OUTDIR%"
            exit /b 1
        )

        "%SIGNTOOL%" verify /pa /q "%OUTDIR%\POM1.exe" || (
            echo ERREUR: verification signature POM1.exe echouee
            rd /s /q "%OUTDIR%"
            exit /b 1
        )
        "%SIGNTOOL%" verify /pa /q "%OUTDIR%\glfw3.dll" || (
            echo ERREUR: verification signature glfw3.dll echouee
            rd /s /q "%OUTDIR%"
            exit /b 1
        )
        echo Signature OK.
    )
)

REM ---- Runtime Microsoft Visual C++ (deploiement app-local) -----------------
REM POM1.exe et glfw3.dll dependent du runtime VC++ (vcruntime140 / msvcp140).
REM L'UCRT est in-box (Win10/11) mais PAS ces DLL : on les place a cote de l'exe
REM (app-local, autorise par la licence du redistribuable) pour que POM1 demarre
REM sur un Windows fraichement installe, sans VC++ Redistributable a installer.
set "VCRT_DLLS=vcruntime140.dll vcruntime140_1.dll msvcp140.dll"
set "VCRT_SRC="
REM 1) Dossier redist exact du toolset MSVC (present en invite developpeur VS).
if defined VCToolsRedistDir for /d %%D in ("%VCToolsRedistDir%x64\Microsoft.VC*.CRT") do (
    if exist "%%D\vcruntime140.dll" set "VCRT_SRC=%%D"
)
REM 2) Repli : la copie installee dans System32 (>= version du toolset, compatible).
if not defined VCRT_SRC if exist "%SystemRoot%\System32\vcruntime140.dll" set "VCRT_SRC=%SystemRoot%\System32"
if defined VCRT_SRC (
    echo Copie runtime VC++ ^(!VCRT_SRC!^)...
    for %%F in (%VCRT_DLLS%) do (
        if exist "!VCRT_SRC!\%%F" (
            copy /Y "!VCRT_SRC!\%%F" "%OUTDIR%\%%F" >nul
        ) else (
            echo AVERTISSEMENT: %%F absente de !VCRT_SRC! — POM1 pourra exiger le VC++ Redistributable.
        )
    )
) else (
    echo AVERTISSEMENT: runtime VC++ introuvable — POM1 pourra exiger le VC++ Redistributable sur un Windows nu.
)

echo Copie fonts\ ...
xcopy /E /I /Q "fonts" "%OUTDIR%\fonts\" >nul || exit /b 1

if not exist "pic\schlumberger-2-apple-1.jpg" (
    echo ERREUR: pic\schlumberger-2-apple-1.jpg introuvable ^(photo About^).
    rd /s /q "%OUTDIR%"
    exit /b 1
)
echo Copie pic\ ...
xcopy /E /I /Q "pic" "%OUTDIR%\pic\" >nul || exit /b 1

echo Copie roms\ ...
xcopy /E /I /Q "roms" "%OUTDIR%\roms\" >nul || exit /b 1

echo Copie ini_defaults\ ...
xcopy /E /I /Q "ini_defaults" "%OUTDIR%\ini_defaults\" >nul || exit /b 1

if exist "software\" (
    echo Copie software\ ...
    xcopy /E /I /Q "software" "%OUTDIR%\software\" >nul
) else (
    echo AVERTISSEMENT: dossier software\ absent — omis.
)

if exist "sdcard\" (
    echo Copie sdcard\ ...
    xcopy /E /I /Q "sdcard" "%OUTDIR%\sdcard\" >nul
) else (
    echo AVERTISSEMENT: dossier sdcard\ absent — omis.
)

if exist "cfcard\" (
    echo Copie cfcard\ ...
    xcopy /E /I /Q "cfcard" "%OUTDIR%\cfcard\" >nul
) else (
    echo AVERTISSEMENT: dossier cfcard\ absent — omis.
)

REM disks\ : image .d64 du 1541 virtuel (carte IEC), montee au demarrage depuis
REM disks\iec\dev8.d64 (chemin relatif au cwd — cf. Memory.cpp). Parite AppImage.
if exist "disks\" (
    echo Copie disks\ ...
    xcopy /E /I /Q "disks" "%OUTDIR%\disks\" >nul
) else (
    echo AVERTISSEMENT: dossier disks\ absent — omis.
)

if exist "cassettes\" (
    echo Copie cassettes\ ...
    xcopy /E /I /Q "cassettes" "%OUTDIR%\cassettes\" >nul
) else (
    echo AVERTISSEMENT: dossier cassettes\ absent — omis.
)

REM DevBench source tree: le selecteur sketchs/ et les exemples integres
REM ouvrent des chemins relatifs au cwd (sketchs/gen2/...).
if exist "sketchs\" (
    echo Copie sketchs\ ...
    xcopy /E /I /Q "sketchs" "%OUTDIR%\sketchs\" >nul
) else (
    echo AVERTISSEMENT: dossier sketchs\ absent — omis.
)

REM Arbre dev/ complet (linker cfgs, libs, projects) pour DevBench asm/C.
if exist "dev\" (
    echo Copie dev\ ...
    xcopy /E /I /Q "dev" "%OUTDIR%\dev\" >nul
) else (
    echo AVERTISSEMENT: dossier dev\ absent — omis.
)

REM ---- Optional cc65 toolchain bundle (self-contained DevBench) --------------
REM Stage a relocatable cc65 tree (bin\ + share\cc65\) so the DevBench builds
REM asm/C with no system cc65 on PATH. POM1 finds it exe-relative at cc65\bin
REM and points CC65_HOME at cc65\share\cc65 (no launcher script needed).
REM Default: compile cc65 from source (packaging\windows\build_cc65.ps1).
REM Fallback: snapshot download (fetch_cc65.ps1) when POM1_CC65_FETCH=1.
REM Override tree: set POM1_CC65_BUNDLE to a dir holding bin\ + share\cc65\.
set "CC65_TREE="
if defined POM1_CC65_BUNDLE if exist "%POM1_CC65_BUNDLE%\bin" set "CC65_TREE=%POM1_CC65_BUNDLE%"
if not defined CC65_TREE if exist "dist\cc65-bundle\cc65\bin" set "CC65_TREE=dist\cc65-bundle\cc65"

if not defined CC65_TREE if "%POM1_CC65_FETCH%"=="1" (
    echo Telechargement du toolchain cc65 ^(snapshot officiel Windows^)...
    powershell -NoProfile -ExecutionPolicy Bypass -File "packaging\windows\fetch_cc65.ps1" -Out "dist\cc65-bundle" || (
        echo AVERTISSEMENT: telechargement cc65 echoue — DevBench limite.
    )
    if exist "dist\cc65-bundle\cc65\bin\ca65.exe" set "CC65_TREE=dist\cc65-bundle\cc65"
) else if not defined CC65_TREE (
    echo Compilation du toolchain cc65 pour POM1 ^(sources GitHub^)...
    powershell -NoProfile -ExecutionPolicy Bypass -File "packaging\windows\build_cc65.ps1" -Out "dist\cc65-bundle" || (
        echo AVERTISSEMENT: compilation cc65 echouee — DevBench limite.
    )
    if exist "dist\cc65-bundle\cc65\bin\ca65.exe" set "CC65_TREE=dist\cc65-bundle\cc65"
)

if defined CC65_TREE (
    echo Copie cc65 bundle ^(!CC65_TREE!^)...
    xcopy /E /I /Q "!CC65_TREE!" "%OUTDIR%\cc65\" >nul
) else (
    echo AVERTISSEMENT: pas de cc65 bundle — DevBench limite au Woz-hex sans cc65 systeme.
)

REM Verifie que le toolchain embarque couvre asm (ca65+ld65) ET C (cl65+cc65).
REM POM1_REQUIRE_CC65=1 (workflow release) -> echec dur si absent/incomplet.
set "CC65_OK=1"
for %%T in (ca65 ld65 cl65 cc65 ar65) do if not exist "%OUTDIR%\cc65\bin\%%T.exe" set "CC65_OK=0"
if "!CC65_OK!"=="0" (
    if "%POM1_REQUIRE_CC65%"=="1" (
        echo ERREUR: POM1_REQUIRE_CC65=1 mais le bundle cc65 est absent/incomplet ^(asm+C requis^).
        rd /s /q "%OUTDIR%"
        exit /b 1
    ) else (
        echo AVERTISSEMENT: bundle cc65 incomplet — DevBench limite au Woz-hex.
    )
)

copy /Y "packaging\windows\README.txt" "%OUTDIR%\README.txt" >nul

if not exist "dist" mkdir "dist"
if exist "%ZIPPATH%" del /f /q "%ZIPPATH%"

echo Creation de l'archive %ZIPNAME% ...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '.\%OUTDIR%\*' -DestinationPath '.\dist\%ZIPNAME%' -Force"

if not exist "%ZIPPATH%" (
    echo ERREUR: echec de Compress-Archive.
    exit /b 1
)

echo.
echo Termine.
echo   Dossier : %OUTDIR%\
echo   ZIP     : %ZIPPATH%
echo.
endlocal
exit /b 0
