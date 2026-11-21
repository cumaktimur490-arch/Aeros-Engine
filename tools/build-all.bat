@echo off
setlocal
set "VER=%~1"
if "%VER%"=="" (
    if exist "..\VERSION" (
        set /p VER=<"..\VERSION"
    ) else (
        set "VER=1.0.0"
    )
)
powershell -ExecutionPolicy Bypass -File "%~dp0build-all.ps1" -Version %VER%
pause
