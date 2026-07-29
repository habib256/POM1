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

REM Download Dear ImGui if not present
if not exist "imgui" (
    echo Downloading Dear ImGui...
    git clone --depth 1 --branch v1.92.9-docking https://github.com/ocornut/imgui.git
    echo.
) else (
    echo Dear ImGui already present.
)

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
