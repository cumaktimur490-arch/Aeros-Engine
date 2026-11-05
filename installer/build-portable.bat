@echo off
setlocal
rem Aeros Engine — build portable (BAT wrapper)
rem Usage: build-portable.bat [x64|x86|both] [version]

set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=both"
set "VER=%~2"
if "%VER%"=="" (
    if exist "..\VERSION" (
        set /p VER=<"..\VERSION"
    ) else (
        set "VER=1.0.0"
    )
)

echo [Aeros] Portable build Arch=%ARCH% Version=%VER%

powershell -ExecutionPolicy Bypass -File "%~dp0build-portable.ps1" -Arch %ARCH% -Version %VER%
if errorlevel 1 (
    echo [ERROR] Portable build failed
    pause
    exit /b 1
)
echo [Aeros] Done.
pause
