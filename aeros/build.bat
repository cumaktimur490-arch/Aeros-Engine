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
) else if /I "%ARCH%"=="arm64" (
    set "VCVARS=vcvarsamd64_arm64.bat"
    set "GLFW_LIBDIR=lib-vc2022-arm64"
    set "OUT_ARCH=arm64"
) else if /I "%ARCH%"=="arm" (
    set "VCVARS=vcvarsamd64_arm64.bat"
    set "GLFW_LIBDIR=lib-vc2022-arm64"
    set "OUT_ARCH=arm64"
) else (
    echo [ERROR] Unknown arch "%ARCH%". Use x64, x86, or arm64.
    exit /b 1
)

echo [Aeros] Building for %OUT_ARCH% using %VCVARS% ...

taskkill /IM main.exe /F 2>nul
taskkill /IM main-%OUT_ARCH%.exe /F 2>nul

rem --- Проверка: если cl.exe уже в PATH (CI окружение), пропускаем vcvars ---
where cl.exe >nul 2>&1
if !errorlevel! equ 0 (
    echo [Aeros] MSVC already in PATH, skipping vcvars detection
    goto :skip_vcvars
)

rem --- Поиск vcvars через vswhere ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS_PATH="

if exist "!VSWHERE!" (
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\!VCVARS!" (
            set "VCVARS_PATH=%%i\VC\Auxiliary\Build\!VCVARS!"
        )
    )
)

rem Fallback пути — используем !VAR! чтобы избежать проблем с (x86) в пути
if not defined VCVARS_PATH (
    if exist "D:\c++\VC\Auxiliary\Build\!VCVARS!" set "VCVARS_PATH=D:\c++\VC\Auxiliary\Build\!VCVARS!"
)
if not defined VCVARS_PATH (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\!VCVARS!" set "VCVARS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\!VCVARS!"
)
if not defined VCVARS_PATH (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\!VCVARS!" set "VCVARS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\!VCVARS!"
)
if not defined VCVARS_PATH (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\!VCVARS!" set "VCVARS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\!VCVARS!"
)
if not defined VCVARS_PATH (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\!VCVARS!" set "VCVARS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\!VCVARS!"
)
if not defined VCVARS_PATH (
    if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\!VCVARS!" set "VCVARS_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\!VCVARS!"
)
if not defined VCVARS_PATH (
    if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\!VCVARS!" set "VCVARS_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\!VCVARS!"
)

if not defined VCVARS_PATH (
    echo [WARN] vcvars not found: !VCVARS!, trying to continue with existing environment...
    echo [WARN] If build fails, install Visual Studio 2022 with C++ workload
    goto :skip_vcvars
)

echo [Aeros] Using VC: !VCVARS_PATH!
call "!VCVARS_PATH!"
if !errorlevel! neq 0 (
    echo [WARN] vcvars call failed, trying to continue...
)

:skip_vcvars

where cl.exe >nul 2>&1
if !errorlevel! neq 0 (
    echo [ERROR] cl.exe not found after vcvars setup. MSVC not installed?
    exit /b 1
)

where nvcc.exe >nul 2>&1
if !errorlevel! neq 0 (
    echo [WARN] nvcc.exe not found, trying CPU-only build...
    call "%~dp0build-cpu.bat" !OUT_ARCH!
    if !errorlevel! neq 0 (
        echo [ERROR] CPU build also failed
        exit /b 1
    ) else (
        echo [Aeros] CPU build succeeded - nvcc not found fallback
        goto :done
    )
)

if not exist bin mkdir bin
set "LIBDIR=%~dp0..\libs"

rem v1.7.0: Full rebuild always — clean old artifacts to ensure aerodynamics changes visible
echo [Aeros] Cleaning previous build artifacts for full rebuild...
del /Q bin\*.obj 2>nul
del /Q bin\main-%OUT_ARCH%.exe 2>nul
del /Q bin\main.exe 2>nul

rem --- Проверка GLFW lib для выбранной архитектуры ---
if not exist "!LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.lib" (
    echo [WARN] GLFW lib not found: !LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.lib
    if /I "!OUT_ARCH!"=="x86" (
        echo [INFO] Trying to use x64 lib as fallback or download 32-bit GLFW.
        echo [INFO] Run tools\get-glfw-x86.ps1 or download from https://www.glfw.org/download.html
        if exist "!LIBDIR!\glfw\lib-vc2022\glfw3.lib" (
            echo [WARN] Fallback to lib-vc2022 - x64 - build may fail for x86 target.
            set "GLFW_LIBDIR=lib-vc2022"
        ) else (
            echo [ERROR] No GLFW lib found at all
            exit /b 1
        )
    ) else if /I "!OUT_ARCH!"=="arm64" (
        echo [INFO] Trying to build GLFW for arm64 from source...
        powershell -ExecutionPolicy Bypass -File "%~dp0..\tools\build-glfw-arm64.ps1" -Arch arm64
        if not exist "!LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.lib" (
            echo [WARN] Failed to build GLFW arm64, trying fallback to x64 lib
            if exist "!LIBDIR!\glfw\lib-vc2022\glfw3.lib" set "GLFW_LIBDIR=lib-vc2022"
        )
    ) else (
        echo [ERROR] GLFW lib not found for x64: !LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.lib
        exit /b 1
    )
)
if not exist "!LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.lib" (
    echo [ERROR] GLFW lib still not found: !LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.lib
    if /I "!OUT_ARCH!"=="arm64" (
        echo [INFO] For arm64, ensure tools\build-glfw-arm64.ps1 succeeded or manually build GLFW
    )
    exit /b 1
)

rem --- Версия из VERSION файла ---
set "VERSION_FILE=%~dp0..\VERSION"
set "APP_VERSION=1.0.0"
if exist "!VERSION_FILE!" (
    set /p APP_VERSION=<"!VERSION_FILE!"
)

echo [Aeros] Version: !APP_VERSION!
echo [Aeros] Libs: !LIBDIR!
echo [Aeros] GLFW lib dir: !GLFW_LIBDIR!
echo [Aeros] Checking tools...

rem --- Сборка иконки ---
if exist src\app_icon.rc (
    echo [Aeros] Compiling icon resource...
    rc /fo bin\app_icon.res src\app_icon.rc
    if !errorlevel! neq 0 (
        echo [WARN] Icon resource compile failed, continuing without icon
        set "ICON_RES="
    ) else (
        set "ICON_RES=bin\app_icon.res"
        echo [Aeros] Icon resource compiled: !ICON_RES!
    )
) else (
    set "ICON_RES="
)

rem --- Сборка ---
echo [Aeros] Starting nvcc compilation for !OUT_ARCH!...

set "NVCC_ARCH=-arch=sm_75"
rem Для поддержки новых MSVC (19.51+) добавляем allow-unsupported-compiler
set "NVCC_FLAGS=-allow-unsupported-compiler -O3 --use_fast_math -Xcompiler /openmp:llvm -Xcompiler /arch:AVX2 -Xcompiler /O2 -Xcompiler /Ot -Xcompiler /fp:fast"

echo nvcc !NVCC_ARCH! !NVCC_FLAGS! -std=c++17 -Xcompiler /MD -Xcompiler /EHsc -I ... -o bin\main-!OUT_ARCH!.exe (OPTIMIZED)

nvcc !NVCC_ARCH! !NVCC_FLAGS! -std=c++17 -Xcompiler /MD -Xcompiler /EHsc ^
  -I "!LIBDIR!\glfw\include" ^
  -I "!LIBDIR!\glad\include" ^
  -I "!LIBDIR!\glm" ^
  -I src ^
  -I src\imgui ^
  src\main.cpp src\globals.cpp src\input.cpp src\gl_utils.cpp src\stl_loader.cpp ^
  src\voxel_grid.cpp src\flow_field.cpp src\particles.cpp src\streamlines.cpp ^
  src\forces.cpp src\model.cpp src\ui.cpp src\atmosphere.cpp src\test_mode.cpp src\lbm.cpp src\lang.cpp src\fsr.cpp src\framegen.cpp src\interesting.cpp ^
  src\glad.c src\kernel.cu ^
  src\imgui\imgui.cpp src\imgui\imgui_draw.cpp src\imgui\imgui_tables.cpp src\imgui\imgui_widgets.cpp ^
  src\imgui\imgui_impl_glfw.cpp src\imgui\imgui_impl_opengl3.cpp ^
  -L "!LIBDIR!\glfw\!GLFW_LIBDIR!" ^
  -lglfw3 -lopengl32 -luser32 -lgdi32 -lshell32 -lcomdlg32 !ICON_RES! ^
  -Xlinker /SUBSYSTEM:WINDOWS -Xlinker /ENTRY:mainCRTStartup ^
  -o bin\main-!OUT_ARCH!.exe

if !errorlevel! neq 0 (
    echo [WARN] CUDA build failed for !OUT_ARCH!, trying CPU-only fallback...
    call "%~dp0build-cpu.bat" !OUT_ARCH!
    if !errorlevel! neq 0 (
        echo [ERROR] Both CUDA and CPU builds failed for !OUT_ARCH!
        exit /b 1
    ) else (
        echo [Aeros] CPU fallback build succeeded for !OUT_ARCH!
        goto :done
    )
)

copy /Y bin\main-!OUT_ARCH!.exe bin\main.exe >nul
echo [Aeros] Build OK: bin\main-!OUT_ARCH!.exe

if exist "!LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.dll" (
    copy /Y "!LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.dll" bin\ >nul
    echo [Aeros] Copied glfw3.dll from !GLFW_LIBDIR!
) else (
    echo [WARN] glfw3.dll not found in !LIBDIR!\glfw\!GLFW_LIBDIR!
)

:done
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
