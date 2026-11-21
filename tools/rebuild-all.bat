@echo off
setlocal enabledelayedexpansion
rem =====================================================
rem Aeros Engine — Full Rebuild All (v1.7.0)
rem Полная пересборка для гарантии что изменения аэродинамики видны
rem Удаляет все артефакты и собирает заново x64/x86/arm64
rem =====================================================

set "ROOT=%~dp0.."
set "AEROS=%ROOT%\aeros"
set "RELEASE=%ROOT%\release"

echo ==========================================
echo  Aeros Engine — FULL REBUILD ALL v1.7.0
echo  Realistic Aero — полная пересборка
echo ==========================================

echo [1/4] Cleaning...
if exist "%AEROS%\bin\*.obj" del /Q "%AEROS%\bin\*.obj"
if exist "%AEROS%\bin\*.exe" del /Q "%AEROS%\bin\*.exe"
if exist "%AEROS%\bin\*.dll" del /Q "%AEROS%\bin\*.dll"
if exist "%AEROS%\build" rmdir /S /Q "%AEROS%\build"
if exist "%RELEASE%\*" del /Q "%RELEASE%\*"

echo [2/4] Building x64 (full rebuild)...
cd /D "%AEROS%"
call build.bat x64
if !errorlevel! neq 0 (
    echo [WARN] x64 CUDA build failed, trying CPU...
    call build-cpu.bat x64
    if !errorlevel! neq 0 (
        echo [ERROR] x64 build failed
        exit /b 1
    )
)

echo [3/4] Building x86 (full rebuild)...
call build.bat x86
if !errorlevel! neq 0 (
    echo [WARN] x86 build failed, trying CPU...
    call build-cpu.bat x86
)

echo [4/4] Building arm64 (full rebuild)...
call build.bat arm64
if !errorlevel! neq 0 (
    echo [WARN] arm64 build failed, trying CPU...
    call build-cpu.bat arm64
)

echo.
echo ==========================================
echo  FULL REBUILD DONE — aerodynamics updated
echo ==========================================
echo  Binaries:
dir "%AEROS%\bin\main-*.exe" 2>nul
echo.
echo  To build release artifacts:
echo    powershell -File tools\build-all.ps1
echo.
