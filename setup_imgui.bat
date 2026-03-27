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

REM Install GLFW via vcpkg if available
if defined VCPKG_ROOT (
    echo Installing GLFW via vcpkg...
    "%VCPKG_ROOT%\vcpkg" install glfw3:x64-windows
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
    git clone --depth 1 https://github.com/ocornut/imgui.git
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
    cmake .. -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
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
