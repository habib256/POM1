@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

echo ============================================
echo  POM1 — Package Windows pour distribution
echo ============================================
echo.

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

set "OUTDIR=dist\POM1-Windows"
set "ZIPNAME=POM1-Windows-v1.8.6.zip"
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

if exist "cassettes\" (
    echo Copie cassettes\ ...
    xcopy /E /I /Q "cassettes" "%OUTDIR%\cassettes\" >nul
) else (
    echo AVERTISSEMENT: dossier cassettes\ absent — omis.
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
