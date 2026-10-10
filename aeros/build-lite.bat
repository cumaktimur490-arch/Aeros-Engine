@echo off
setlocal enabledelayedexpansion
rem =====================================================
rem Aeros Engine — Lite сборка для слабых устройств v1.20.1
rem i3-3xxx (Ivy Bridge 2C/4T SSE4.2, нет AVX2), Intel HD 4000 (OpenGL 4.0, 16 EUs),
rem GT 620M, AMD APU, ноутбуки с 4GB RAM, без CUDA
rem + Ultra-Lite Potato для Atom/Celeron 2GB RAM HD3000
rem Использование:
rem   build-lite.bat         -> x64 Lite
rem   build-lite.bat x64     -> x64 Lite
rem   build-lite.bat x86     -> x86 Lite (для старых ноутов)
rem   build-lite.bat ultra   -> x64 Ultra-Lite Potato
rem   build-lite.bat clean   -> очистка
rem   build-lite.bat deps    -> скачать зависимости
rem =====================================================

set "ARG1=%~1"
if "%ARG1%"=="" set "ARG1=x64"

if /I "%ARG1%"=="clean" goto :clean
if /I "%ARG1%"=="deps" goto :deps

set "ARCH=%ARG1%"
if /I "%ARCH%"=="x32" set "ARCH=x86"
if /I "%ARCH%"=="Win32" set "ARCH=x86"

if /I "%ARCH%"=="x64" (
    set "VCVARS=vcvars64.bat"
    set "GLFW_LIBDIR=lib-vc2022"
    set "OUT_ARCH=x64"
) else if /I "%ARCH%"=="x86" (
    set "VCVARS=vcvars32.bat"
    set "GLFW_LIBDIR=lib-vc2022-x86"
    set "OUT_ARCH=x86"
) else (
    echo [ERROR] Unknown arch "%ARCH%". Use x64, x86, deps, clean.
    exit /b 1
)

echo [Aeros Lite] Building v1.20.0 Lite for %OUT_ARCH% — i3-3xxx / HD 4000 / GT 620M / No CUDA
echo [Aeros Lite] Optimized for weak devices: low RAM, low VRAM, 30 FPS target, SSE2

taskkill /IM main-lite.exe /F 2>nul
taskkill /IM aeros-engine-lite.exe /F 2>nul

rem --- Авто-загрузка зависимостей ---
call :download_deps
if !errorlevel! neq 0 (
    echo [WARN] Dependency download had issues, continuing...
)

rem --- MSVC detection ---
where cl.exe >nul 2>&1
if !errorlevel! equ 0 (
    echo [Aeros Lite] MSVC already in PATH
    goto :skip_vcvars
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS_PATH="
if exist "!VSWHERE!" (
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\!VCVARS!" set "VCVARS_PATH=%%i\VC\Auxiliary\Build\!VCVARS!"
    )
)
if not defined VCVARS_PATH (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\!VCVARS!" set "VCVARS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\!VCVARS!"
)
if not defined VCVARS_PATH (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\!VCVARS!" set "VCVARS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\!VCVARS!"
)
if not defined VCVARS_PATH (
    echo [WARN] vcvars not found, trying continue...
    goto :skip_vcvars
)
echo [Aeros Lite] Using VC: !VCVARS_PATH!
call "!VCVARS_PATH!"

:skip_vcvars
where cl.exe >nul 2>&1
if !errorlevel! neq 0 (
    echo [ERROR] cl.exe not found
    exit /b 1
)

if not exist bin mkdir bin
set "LIBDIR=%~dp0..\libs"

echo [Aeros Lite] Cleaning previous Lite build...
del /Q bin\*.obj 2>nul
del /Q bin\main-lite-%OUT_ARCH%.exe 2>nul
del /Q bin\aeros-engine-lite*.exe 2>nul

if not exist "!LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.lib" (
    if exist "!LIBDIR!\glfw\lib-vc2022\glfw3.lib" set "GLFW_LIBDIR=lib-vc2022" else (
        echo [ERROR] GLFW lib not found: !LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.lib
        echo [INFO] Run build-lite.bat deps
        exit /b 1
    )
)

set "VERSION_FILE=%~dp0..\VERSION"
set "APP_VERSION=1.20.0"
if exist "!VERSION_FILE!" set /p APP_VERSION=<"!VERSION_FILE!"

echo [Aeros Lite] Version: !APP_VERSION! Arch: !OUT_ARCH! Libs: !LIBDIR! GLFW: !GLFW_LIBDIR!

rem --- Иконка ---
if exist src\app_icon.rc (
    rc /fo bin\app_icon_lite.res src\app_icon.rc
    if !errorlevel! neq 0 set "ICON_RES=" else set "ICON_RES=bin\app_icon_lite.res"
) else set "ICON_RES="

echo [Aeros Lite] Compiling for weak devices — SSE2, no AVX2, no CUDA, low memory...

rem --- Lite флаги для i3-3xxx ---
rem i3-3xxx Ivy Bridge: SSE4.2, AVX (нет AVX2), 2C/4T
rem Для совместимости — только SSE2 (/arch:SSE2), O1 для меньшего бинаря, /MD, /EHsc
rem Отключаем /arch:AVX2, используем /arch:SSE2, /O1, без /GL (LTCG тяжело), без /Ot (O1)
set "INCLUDES=/I "!LIBDIR!\glfw\include" /I "!LIBDIR!\glad\include" /I "!LIBDIR!\glm" /I src /I src\imgui"
set "DEFINES=/DAEROS_LITE /DCPU_ONLY /DLITE_MODE /DVERSION_STRING=\"!APP_VERSION! Lite\" /DGLFW_INCLUDE_NONE"
rem Lite: O1, SSE2, no AVX2, no fast math (может давать артефакты на слабых), openmp but limited
set "CXXFLAGS=/std:c++17 /EHsc /MD /W1 /O1 /arch:SSE2 /openmp /fp:precise !DEFINES! !INCLUDES!"

echo [Aeros Lite] CXXFLAGS: !CXXFLAGS!

echo [Aeros Lite] Compiling glad.c...
cl /c /MD /TC /I "!LIBDIR!\glad\include" src\glad.c /Fo:bin\glad.obj
if !errorlevel! neq 0 exit /b 1

echo [Aeros Lite] Compiling C++ files (Lite)...

cl /c !CXXFLAGS! src\main.cpp /Fo:bin\main.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\globals.cpp /Fo:bin\globals.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\input.cpp /Fo:bin\input.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\gl_utils.cpp /Fo:bin\gl_utils.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\stl_loader.cpp /Fo:bin\stl_loader.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\voxel_grid.cpp /Fo:bin\voxel_grid.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\flow_field.cpp /Fo:bin\flow_field.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\particles.cpp /Fo:bin\particles.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\streamlines.cpp /Fo:bin\streamlines.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\forces.cpp /Fo:bin\forces.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\model.cpp /Fo:bin\model.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\ui.cpp /Fo:bin\ui.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\test_mode.cpp /Fo:bin\test_mode.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\lbm.cpp /Fo:bin\lbm.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\lang.cpp /Fo:bin\lang.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\atmosphere.cpp /Fo:bin\atmosphere.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\fsr.cpp /Fo:bin\fsr.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\framegen.cpp /Fo:bin\framegen.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\interesting.cpp /Fo:bin\interesting.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\vulkan_renderer.cpp /Fo:bin\vulkan_renderer.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\lite_config.cpp /Fo:bin\lite_config.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\cuda_stub.cpp /Fo:bin\cuda_stub.obj
if !errorlevel! neq 0 exit /b 1

cl /c !CXXFLAGS! src\imgui\imgui.cpp /Fo:bin\imgui.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\imgui\imgui_draw.cpp /Fo:bin\imgui_draw.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\imgui\imgui_tables.cpp /Fo:bin\imgui_tables.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\imgui\imgui_widgets.cpp /Fo:bin\imgui_widgets.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\imgui\imgui_impl_glfw.cpp /Fo:bin\imgui_impl_glfw.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\imgui\imgui_impl_opengl3.cpp /Fo:bin\imgui_impl_opengl3.obj
if !errorlevel! neq 0 exit /b 1

echo [Aeros Lite] Linking Lite (no LTCG, small binary)...
link /OUT:bin\main-lite-%OUT_ARCH%.exe ^
  bin\main.obj bin\globals.obj bin\input.obj bin\gl_utils.obj bin\stl_loader.obj ^
  bin\voxel_grid.obj bin\flow_field.obj bin\particles.obj bin\streamlines.obj ^
  bin\forces.obj bin\model.obj bin\ui.obj bin\atmosphere.obj bin\test_mode.obj bin\lbm.obj bin\lang.obj bin\fsr.obj bin\framegen.obj bin\interesting.obj bin\vulkan_renderer.obj bin\lite_config.obj bin\cuda_stub.obj bin\glad.obj ^
  bin\imgui.obj bin\imgui_draw.obj bin\imgui_tables.obj bin\imgui_widgets.obj ^
  bin\imgui_impl_glfw.obj bin\imgui_impl_opengl3.obj !ICON_RES! ^
  /LIBPATH:"!LIBDIR!\glfw\!GLFW_LIBDIR!" glfw3.lib opengl32.lib user32.lib gdi32.lib shell32.lib comdlg32.lib ^
  /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup

if !errorlevel! neq 0 (
    echo [ERROR] Link failed
    exit /b 1
)

copy /Y bin\main-lite-%OUT_ARCH%.exe bin\main-lite.exe >nul
copy /Y bin\main-lite-%OUT_ARCH%.exe bin\aeros-engine-lite-%OUT_ARCH%.exe >nul
copy /Y bin\main-lite-%OUT_ARCH%.exe bin\aeros-engine-lite.exe >nul

if exist "!LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.dll" copy /Y "!LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.dll" bin\ >nul

echo [Aeros Lite] Build OK: bin\main-lite-%OUT_ARCH%.exe (Lite)
echo [Aeros Lite] Size:
dir bin\main-lite-%OUT_ARCH%.exe | findstr "main-lite"
echo [Aeros Lite] Lite binary is smaller and uses SSE2, no AVX2, no CUDA, 30 FPS target, low RAM
echo [Aeros Lite] For i3-3xxx, Intel HD 4000, GT 620M, 4GB RAM laptops

goto :done

:download_deps
echo [Aeros Lite] Downloading dependencies for Lite...
powershell -ExecutionPolicy Bypass -File "%~dp0..\installer\download_deps.ps1" -Arch %OUT_ARCH% -Quiet
goto :eof

:clean
echo [Aeros Lite] Cleaning Lite...
if exist bin\main-lite.exe del /Q bin\main-lite.exe
if exist bin\main-lite-x64.exe del /Q bin\main-lite-x64.exe
if exist bin\main-lite-x86.exe del /Q bin\main-lite-x86.exe
if exist bin\aeros-engine-lite*.exe del /Q bin\aeros-engine-lite*.exe
if exist bin\app_icon_lite.res del /Q bin\app_icon_lite.res
if exist bin\*.obj del /Q bin\*.obj
echo [Aeros Lite] Clean done.
goto :eof

:done
echo [Aeros Lite] Done. Lite version ready for weak devices i3-3xxx / HD 4000 / 4GB RAM
goto :eof
