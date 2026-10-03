@echo off
setlocal enabledelayedexpansion
rem =====================================================
rem Aeros Engine — универсальный build.bat
rem Поддерживает: x64 (по умолчанию) и x86 (Win32)
rem Использование:
rem   build.bat         -> x64
rem   build.bat x64     -> x64
rem   build.bat x86     -> Win32
rem   build.bat x32     -> Win32 (alias)
rem   build.bat clean   -> очистка
rem =====================================================

set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=x64"
if /I "%ARCH%"=="x32" set "ARCH=x86"
if /I "%ARCH%"=="Win32" set "ARCH=x86"
if /I "%ARCH%"=="clean" goto :clean

if /I "%ARCH%"=="x64" (
    set "VCVARS=vcvars64.bat"
    set "GLFW_LIBDIR=lib-vc2022"
    set "OUT_ARCH=x64"
) else if /I "%ARCH%"=="x86" (
    set "VCVARS=vcvars32.bat"
    set "GLFW_LIBDIR=lib-vc2022-x86"
    set "OUT_ARCH=x86"
) else (
    echo [ERROR] Unknown arch "%ARCH%". Use x64 or x86.
    exit /b 1
)

echo [Aeros] Building for %OUT_ARCH% using %VCVARS% ...

taskkill /IM main.exe /F 2>nul

rem --- Поиск vcvars через vswhere ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS_PATH="

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\%VCVARS%" (
            set "VCVARS_PATH=%%i\VC\Auxiliary\Build\%VCVARS%"
        )
    )
)

rem Fallback пути
if not defined VCVARS_PATH (
    if exist "D:\c++\VC\Auxiliary\Build\%VCVARS%" set "VCVARS_PATH=D:\c++\VC\Auxiliary\Build\%VCVARS%"
)
if not defined VCVARS_PATH (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\%VCVARS%" set "VCVARS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\%VCVARS%"
)
if not defined VCVARS_PATH (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\%VCVARS%" set "VCVARS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\%VCVARS%"
)
if not defined VCVARS_PATH (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\%VCVARS%" set "VCVARS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\%VCVARS%"
)
if not defined VCVARS_PATH (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\%VCVARS%" set "VCVARS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\%VCVARS%"
)

if not defined VCVARS_PATH (
    echo [ERROR] vcvars not found: %VCVARS%
    echo Install Visual Studio 2022 with C++ workload or set path manually.
    pause
    exit /b 1
)

echo [Aeros] Using VC: %VCVARS_PATH%
call "%VCVARS_PATH%"
if errorlevel 1 (
    echo [ERROR] vcvars call failed
    pause
    exit /b 1
)

if not exist bin mkdir bin
set "LIBDIR=%~dp0..\libs"

rem --- Проверка GLFW lib для выбранной архитектуры ---
if not exist "%LIBDIR%\glfw\%GLFW_LIBDIR%\glfw3.lib" (
    echo [WARN] GLFW lib not found: %LIBDIR%\glfw\%GLFW_LIBDIR%\glfw3.lib
    if /I "%ARCH%"=="x86" (
        echo [INFO] Trying to use x64 lib as fallback (will fail for x86) or download 32-bit GLFW.
        echo [INFO] Run tools\get-glfw-x86.ps1 or download from https://www.glfw.org/download.html
        if exist "%LIBDIR%\glfw\lib-vc2022\glfw3.lib" (
            echo [WARN] Fallback to lib-vc2022 (x64) — build may fail for x86 target.
            set "GLFW_LIBDIR=lib-vc2022"
        )
    )
)

rem --- Версия из VERSION файла ---
set "VERSION_FILE=%~dp0..\VERSION"
set "APP_VERSION=1.0.0"
if exist "%VERSION_FILE%" (
    set /p APP_VERSION=<"%VERSION_FILE%"
)

echo [Aeros] Version: %APP_VERSION%
echo [Aeros] Libs: %LIBDIR%
echo [Aeros] GLFW lib dir: %GLFW_LIBDIR%

rem --- Сборка ---
nvcc -arch=sm_75 -gencode arch=compute_75,code=sm_75 -gencode arch=compute_86,code=sm_86 -gencode arch=compute_89,code=sm_89 -std=c++17 -Xcompiler /MD -Xcompiler /DVERSION=\"%APP_VERSION%\" ^
  -I "%LIBDIR%\glfw\include" ^
  -I "%LIBDIR%\glad\include" ^
  -I "%LIBDIR%\glm" ^
  -I src ^
  -I src\imgui ^
  src\main.cpp src\globals.cpp src\input.cpp src\gl_utils.cpp src\stl_loader.cpp ^
  src\voxel_grid.cpp src\flow_field.cpp src\particles.cpp src\streamlines.cpp ^
  src\forces.cpp src\model.cpp src\ui.cpp ^
  src\glad.c src\kernel.cu ^
  src\imgui\imgui.cpp src\imgui\imgui_draw.cpp src\imgui\imgui_tables.cpp src\imgui\imgui_widgets.cpp ^
  src\imgui\imgui_impl_glfw.cpp src\imgui\imgui_impl_opengl3.cpp ^
  -L "%LIBDIR%\glfw\%GLFW_LIBDIR%" ^
  -lglfw3 -lopengl32 -luser32 -lgdi32 -lshell32 -lcomdlg32 ^
  -Xlinker /SUBSYSTEM:WINDOWS -Xlinker /ENTRY:mainCRTStartup ^
  -o bin\main-%OUT_ARCH%.exe

if errorlevel 1 (
    echo [ERROR] Build failed for %OUT_ARCH%
    pause
    exit /b 1
)

rem Копируем как main.exe для обратной совместимости + arch-specific
copy /Y bin\main-%OUT_ARCH%.exe bin\main.exe >nul
echo [Aeros] Build OK: bin\main-%OUT_ARCH%.exe (and bin\main.exe)

if /I "%ARCH%"=="x64" (
    rem Для x64 также копируем x64 DLL если есть отдельная папка
    if exist "%LIBDIR%\glfw\%GLFW_LIBDIR%\glfw3.dll" (
        copy /Y "%LIBDIR%\glfw\%GLFW_LIBDIR%\glfw3.dll" bin\ >nul
    )
) else (
    if exist "%LIBDIR%\glfw\%GLFW_LIBDIR%\glfw3.dll" (
        copy /Y "%LIBDIR%\glfw\%GLFW_LIBDIR%\glfw3.dll" bin\ >nul
    )
)

echo [Aeros] Done.
goto :eof

:clean
echo [Aeros] Cleaning...
if exist bin\main.exe del /Q bin\main.exe
if exist bin\main-x64.exe del /Q bin\main-x64.exe
if exist bin\main-x86.exe del /Q bin\main-x86.exe
if exist bin\*.obj del /Q bin\*.obj
echo [Aeros] Clean done.
goto :eof
