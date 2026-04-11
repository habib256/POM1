@echo off
echo POM1 - Apple 1 Emulator
echo ============================================

REM Find the executable (MSVC puts it in Release/ or Debug/, other generators in build/)
set EXE=
if exist "build\Release\pom1_imgui.exe" set EXE=build\Release\pom1_imgui.exe
if exist "build\Debug\pom1_imgui.exe" if "%EXE%"=="" set EXE=build\Debug\pom1_imgui.exe
if exist "build\pom1_imgui.exe" if "%EXE%"=="" set EXE=build\pom1_imgui.exe

if "%EXE%"=="" (
    echo Emulator not found. Building...
    if not exist "build" mkdir build
    cd build
    cmake .. && cmake --build . --config Release
    cd ..
    if exist "build\Release\pom1_imgui.exe" set EXE=build\Release\pom1_imgui.exe
    if exist "build\pom1_imgui.exe" if "%EXE%"=="" set EXE=build\pom1_imgui.exe
)

if "%EXE%"=="" (
    echo ERROR: Build failed. Could not find pom1_imgui.exe
    exit /b 1
)

REM Get the directory where the exe lives
for %%F in ("%EXE%") do set EXE_DIR=%%~dpF

REM Copy ROMs next to the executable if needed
for %%R in (basic.rom krusader-1.3.rom WozMonitor.rom charmap.rom applesoft-lite-cffa1.rom applesoft-lite-microsd.rom cffa1.rom) do (
    if not exist "%EXE_DIR%%%R" (
        if exist "roms\%%R" (
            echo Copying %%R...
            copy "roms\%%R" "%EXE_DIR%" >nul
        )
    )
)

REM Copy Font Awesome next to the .exe (main_imgui charge fonts\fa-solid-900.ttf depuis le dossier de l'exe)
if exist "fonts" (
    if not exist "%EXE_DIR%fonts\fa-solid-900.ttf" (
        echo Copying fonts...
        xcopy /E /I /Q "fonts" "%EXE_DIR%fonts\" >nul
    )
)
REM Ancien emplacement (build\fonts) pour compatibilite si le CWD est build\Release
if not exist "build\fonts\fa-solid-900.ttf" (
    if exist "fonts" (
        if not exist "build\fonts" mkdir "build\fonts"
        xcopy /E /I /Q "fonts" "build\fonts\" >nul
    )
)

echo Launching %EXE%...
echo.
"%EXE%"
