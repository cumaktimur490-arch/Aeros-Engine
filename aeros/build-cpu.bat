@echo off
setlocal enabledelayedexpansion
rem =====================================================
rem Aeros Engine — CPU-only сборка без CUDA (для CI и fallback)
rem Использование:
rem   build-cpu.bat [x64|x86]
rem =====================================================

set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=x64"
if /I "%ARCH%"=="x32" set "ARCH=x86"
if /I "%ARCH%"=="Win32" set "ARCH=x86"

if /I "%ARCH%"=="x64" (
    set "VCVARS=vcvars64.bat"
    set "GLFW_LIBDIR=lib-vc2022"
    set "OUT_ARCH=x64"
) else (
    set "VCVARS=vcvars32.bat"
    set "GLFW_LIBDIR=lib-vc2022-x86"
    set "OUT_ARCH=x86"
)

echo [Aeros CPU] Building for %OUT_ARCH% ...

taskkill /IM main.exe /F 2>nul

where cl.exe >nul 2>&1
if %errorlevel% neq 0 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    set "VCVARS_PATH="
    if exist "%VSWHERE%" (
        for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do (
            if exist "%%i\VC\Auxiliary\Build\%VCVARS%" set "VCVARS_PATH=%%i\VC\Auxiliary\Build\%VCVARS%"
        )
    )
    if defined VCVARS_PATH (
        echo [Aeros CPU] Using VC: %VCVARS_PATH%
        call "%VCVARS_PATH%"
    )
)

where cl.exe >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cl.exe not found
    exit /b 1
)

if not exist bin mkdir bin
set "LIBDIR=%~dp0..\libs"

if not exist "%LIBDIR%\glfw\%GLFW_LIBDIR%\glfw3.lib" (
    if exist "%LIBDIR%\glfw\lib-vc2022\glfw3.lib" set "GLFW_LIBDIR=lib-vc2022"
)

set "VERSION_FILE=%~dp0..\VERSION"
set "APP_VERSION=1.0.0"
if exist "%VERSION_FILE%" set /p APP_VERSION=<"%VERSION_FILE%"

echo [Aeros CPU] Version: %APP_VERSION% Arch: %OUT_ARCH%

rem Компилируем через cl.exe напрямую
cl /std:c++17 /EHsc /MD /DVERSION=\"%APP_VERSION%\" /DCPU_ONLY ^
  /I "%LIBDIR%\glfw\include" /I "%LIBDIR%\glad\include" /I "%LIBDIR%\glm" /I src /I src\imgui ^
  src\main.cpp src\globals.cpp src\input.cpp src\gl_utils.cpp src\stl_loader.cpp ^
  src\voxel_grid.cpp src\flow_field.cpp src\particles.cpp src\streamlines.cpp ^
  src\forces.cpp src\model.cpp src\ui.cpp ^
  src\glad.c src\cuda_stub.cpp ^
  src\imgui\imgui.cpp src\imgui\imgui_draw.cpp src\imgui\imgui_tables.cpp src\imgui\imgui_widgets.cpp ^
  src\imgui\imgui_impl_glfw.cpp src\imgui\imgui_impl_opengl3.cpp ^
  /link /LIBPATH:"%LIBDIR%\glfw\%GLFW_LIBDIR%" glfw3.lib opengl32.lib user32.lib gdi32.lib shell32.lib comdlg32.lib ^
  /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /OUT:bin\main-%OUT_ARCH%.exe

if errorlevel 1 (
    echo [ERROR] CPU build failed
    exit /b 1
)

copy /Y bin\main-%OUT_ARCH%.exe bin\main.exe >nul
if exist "%LIBDIR%\glfw\%GLFW_LIBDIR%\glfw3.dll" copy /Y "%LIBDIR%\glfw\%GLFW_LIBDIR%\glfw3.dll" bin\ >nul

echo [Aeros CPU] Build OK: bin\main-%OUT_ARCH%.exe
