@echo off
echo ============================================
echo  POM1 - Apple 1 Emulator - Windows Setup
echo ============================================
echo.

REM Check for CMake
where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake not found. Install CMake and add to PATH.
    echo        https://cmake.org/download/
    exit /b 1
)

REM Check for Git
where git >nul 2>nul
if errorlevel 1 (
    echo ERROR: git not found. Install Git and add to PATH.
    echo        https://git-scm.com/download/win
    exit /b 1
)

REM Install GLFW via vcpkg if available.
REM Static triplet: POM1 links the CRT statically on MSVC (/MT, cf.
REM POM1_WIN_STATIC_RUNTIME in CMakeLists.txt) so the shipped exe needs no DLL
REM beside it — see issue #34. GLFW must be built with the same CRT linkage,
REM which x64-windows-static provides (VCPKG_CRT_LINKAGE=static).
REM Aucun argument de paquet : la dependance et sa version exacte vivent dans
REM vcpkg.json a la racine du depot (mode manifeste, version epinglee par
REM builtin-baseline). vcpkg REFUSE un argument de paquet en mode manifeste,
REM donc "install glfw3:x64-windows-static" serait desormais une erreur.
REM A lancer depuis la racine du depot, la ou se trouve vcpkg.json.
if defined VCPKG_ROOT (
    echo Installing GLFW via vcpkg ^(manifest mode, static triplet^)...
    "%VCPKG_ROOT%\vcpkg" install --triplet x64-windows-static
    echo.
) else (
    echo WARNING: VCPKG_ROOT not set.
    echo To install GLFW automatically:
    echo   1. Install vcpkg: https://vcpkg.io/
    echo   2. Set VCPKG_ROOT environment variable
    echo   3. Run this script again
    echo.
    echo Or install GLFW manually: https://www.glfw.org/download.html
    echo.
)

REM Dear ImGui -- Windows counterpart of tools/ensure_imgui.sh (see the long
REM rationale there). imgui\ is .gitignored, so it is a local checkout, not
REM vendored code, and BOTH of these must hold or the build dies far from the
REM real cause: the DOCKING branch, and IMGUI_VERSION_NUM at or above the pin
REM (an old docking tag has ImGuiWindowFlags_NoDocking but lacks
REM ImGuiChildFlags_Borders, which src\bench\CodeBench.cpp uses).
REM
REM Written with labels rather than nested parenthesised blocks on purpose:
REM cmd.exe expands %VAR% when it PARSES a block, so a variable set inside one
REM reads as its pre-block value unless delayed expansion is enabled.
set "IMGUI_TAG=v1.92.9-docking"
set "IMGUI_MIN=19290"
set "IMGUI_URL=https://github.com/ocornut/imgui.git"

if not exist "imgui\imgui.h" goto imgui_clone

set "IMGUI_HAVE="
for /f "tokens=3" %%v in ('findstr /b /c:"#define IMGUI_VERSION_NUM" "imgui\imgui.h"') do set "IMGUI_HAVE=%%v"
findstr /c:"ImGuiWindowFlags_NoDocking" "imgui\imgui.h" >nul 2>&1
if errorlevel 1 goto imgui_upgrade
if not defined IMGUI_HAVE goto imgui_upgrade
if %IMGUI_HAVE% LSS %IMGUI_MIN% goto imgui_upgrade
echo Dear ImGui already present ^(%IMGUI_HAVE%, docking^).
goto imgui_done

:imgui_clone
echo Downloading Dear ImGui %IMGUI_TAG%...
git clone --depth 1 --branch %IMGUI_TAG% %IMGUI_URL% imgui
if errorlevel 1 goto imgui_fail
echo.
goto imgui_done

:imgui_upgrade
echo imgui\ is stale or not the docking branch - upgrading to %IMGUI_TAG%...
git -C imgui rev-parse --git-dir >nul 2>&1
if errorlevel 1 (
    echo ERROR: imgui\ is not a git repository, cannot upgrade.
    echo        Delete the folder and re-run this script.
    exit /b 1
)
REM --quiet HEAD, not plain --quiet: the latter compares worktree against the
REM index, so already-staged edits would slip through and fail later at the
REM checkout with a misleading message.
git -C imgui diff --quiet HEAD
if errorlevel 1 (
    echo ERROR: imgui\ has local changes - nothing was touched.
    echo        Save them, then re-run this script.
    exit /b 1
)
REM --depth 1 against a COMPLETE clone would convert it to a shallow one and
REM lose its history, so only ask for it when the repo is already shallow.
set "IMGUI_SHALLOW="
for /f %%s in ('git -C imgui rev-parse --is-shallow-repository') do set "IMGUI_SHALLOW=%%s"
if "%IMGUI_SHALLOW%"=="true" (git -C imgui fetch --depth 1 origin tag %IMGUI_TAG%) else (git -C imgui fetch origin tag %IMGUI_TAG%)
if errorlevel 1 goto imgui_fail
git -C imgui checkout --quiet %IMGUI_TAG%
if errorlevel 1 goto imgui_fail
echo Dear ImGui upgraded to %IMGUI_TAG%.
echo.
goto imgui_done

:imgui_fail
echo ERROR: could not obtain Dear ImGui %IMGUI_TAG%.
exit /b 1

:imgui_done

REM Create build directory
if not exist "build" mkdir build

REM Configure with CMake
echo Configuring CMake...
cd build
if defined VCPKG_ROOT (
    cmake .. -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static
) else (
    cmake ..
)
cd ..

echo.
echo ============================================
echo  Setup complete!
echo ============================================
echo.
echo To build:
echo   cd build
echo   cmake --build . --config Release
echo.
echo To run:
echo   run_emulator.bat
echo.
