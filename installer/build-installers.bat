@echo off
setlocal
rem Aeros Engine — build installers (BAT wrapper)
rem Usage: build-installers.bat [x64|x86|both] [full|update|all] [version]

set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=both"
set "TYPE=%~2"
if "%TYPE%"=="" set "TYPE=all"
set "VER=%~3"
if "%VER%"=="" (
    if exist "..\VERSION" (
        set /p VER=<"..\VERSION"
    ) else (
        set "VER=1.0.0"
    )
)

echo [Aeros] Installer build Arch=%ARCH% Type=%TYPE% Version=%VER%

powershell -ExecutionPolicy Bypass -File "%~dp0build-installers.ps1" -Arch %ARCH% -Type %TYPE% -Version %VER%
if errorlevel 1 (
    echo [ERROR] Installer build failed
    pause
    exit /b 1
)
echo [Aeros] Done.
pause
