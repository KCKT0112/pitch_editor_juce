@echo off
setlocal enabledelayedexpansion

echo ========================================
echo Pitch Editor (JUCE) Build Script
echo ========================================
echo.

cd /d "%~dp0"

:: Setup Visual Studio environment if cmake not found
where cmake >nul 2>nul
if errorlevel 1 (
    echo Setting up Visual Studio environment...
    if exist "F:\vs\VC\Auxiliary\Build\vcvars64.bat" (
        call "F:\vs\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    ) else (
        echo Cannot find Visual Studio. Please run from Developer Command Prompt.
        exit /b 1
    )
)

:: Check if JUCE exists
if not exist "JUCE" (
    echo JUCE not found, cloning...
    git clone --depth 1 https://github.com/juce-framework/JUCE.git
    if errorlevel 1 (
        echo Failed to clone JUCE
        exit /b 1
    )
)

:: Create build directory
if not exist "build" mkdir build
cd build

:: Configure with CMake
echo.
echo Configuring with CMake...
set ONNXRUNTIME_ROOT=%~dp0..\onnxruntime-win-x64-1.18.0
cmake -G "Visual Studio 17 2022" -A x64 -DONNXRUNTIME_ROOT="%ONNXRUNTIME_ROOT%" ..
if errorlevel 1 (
    echo CMake configuration failed
    exit /b 1
)

:: Build
echo.
echo Building...
cmake --build . --config Release
if errorlevel 1 (
    echo Build failed
    exit /b 1
)

echo.
echo ========================================
echo Build successful!
echo Executable: build\PitchEditor_artefacts\Release\PitchEditor.exe
echo ========================================

endlocal
