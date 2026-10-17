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
    set "VCVARS=vcvars32.bat"
    set "GLFW_LIBDIR=lib-vc2022-x86"
    set "OUT_ARCH=x86"
)

echo [Aeros CPU] Building for !OUT_ARCH! ...

taskkill /IM main.exe /F 2>nul

rem vcvars целевой архитектуры — всегда (иначе в CI подхватывается чужой cl.exe)
if 1==1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    set "VCVARS_PATH="
    if exist "!VSWHERE!" (
        for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -property installationPath`) do (
            if exist "%%i\VC\Auxiliary\Build\!VCVARS!" set "VCVARS_PATH=%%i\VC\Auxiliary\Build\!VCVARS!"
        )
    )
    if defined VCVARS_PATH (
        echo [Aeros CPU] Using VC: !VCVARS_PATH!
        call "!VCVARS_PATH!"
    )
)

where cl.exe >nul 2>&1
if !errorlevel! neq 0 (
    echo [ERROR] cl.exe not found
    exit /b 1
)

if not exist bin mkdir bin
set "LIBDIR=%~dp0..\libs"

if not exist "!LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.lib" (
    if /I "!OUT_ARCH!"=="arm64" (
        echo [WARN] GLFW lib not found for arm64: !LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.lib
        echo [INFO] Trying to build GLFW for arm64 from source...
        powershell -ExecutionPolicy Bypass -File "%~dp0..\tools\build-glfw-arm64.ps1" -Arch arm64
        if !errorlevel! neq 0 (
            echo [WARN] Failed to build GLFW arm64, trying fallback to x64 lib
            if exist "!LIBDIR!\glfw\lib-vc2022\glfw3.lib" set "GLFW_LIBDIR=lib-vc2022"
        )
    ) else (
        if exist "!LIBDIR!\glfw\lib-vc2022\glfw3.lib" set "GLFW_LIBDIR=lib-vc2022"
    )
)
if not exist "!LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.lib" (
    echo [ERROR] GLFW lib not found: !LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.lib
    echo [INFO] For arm64, run tools\build-glfw-arm64.ps1 -Arch arm64
    exit /b 1
)

rem --- Версия: VERSION, а если его нет — авторитетный aeros\src\version.h ---
set "VERSION_FILE=%~dp0..\VERSION"
set "APP_VERSION="
if exist "!VERSION_FILE!" set /p APP_VERSION=<"!VERSION_FILE!"
if not defined APP_VERSION for /f "tokens=3" %%v in ('findstr /C:"AEROS_VERSION_STRING" "%~dp0src\version.h" 2^>nul') do set "APP_VERSION=%%~v"
if defined APP_VERSION set APP_VERSION=%APP_VERSION:"=%
if not defined APP_VERSION set "APP_VERSION=0.0.0"

echo [Aeros CPU] Version: !APP_VERSION! Arch: !OUT_ARCH!
echo [Aeros CPU] LibDir: !LIBDIR! GLFW: !GLFW_LIBDIR!

del /Q bin\*.obj 2>nul
rem Устаревший exe этой архитектуры удаляем: иначе при ошибке линковки в bin
rem останется прошлый main-!OUT_ARCH!.exe и упаковка положит его в релиз.
if exist bin\main-!OUT_ARCH!.exe del /Q bin\main-!OUT_ARCH!.exe

echo [Aeros CPU] Compiling glad.c...
cl /c /MD /TC /I "!LIBDIR!\glad\include" src\glad.c /Fo:bin\glad.obj
if !errorlevel! neq 0 (
    echo [ERROR] glad.c compile failed
    exit /b 1
)

echo [Aeros CPU] Compiling C++ files...

set "INCLUDES=/I "!LIBDIR!\glfw\include" /I "!LIBDIR!\glad\include" /I "!LIBDIR!\glm" /I src /I src\imgui"
set "DEFINES=/DCPU_ONLY /DVERSION_STRING=\"!APP_VERSION!\""
rem --- Набор инструкций CPU ---
rem Релизный бинарь ставится на любые ПК, поэтому базовый уровень:
rem   x64 и x86 -> /arch:SSE2 (SSE2 есть во всех x64 CPU и в x86 начиная с Pentium 4)
rem   arm64     -> /arch не задаётся (это x86-опция, на ARM64 она бессмысленна)
rem /arch:AVX2 делает exe непригодным для CPU до Haswell (2013): процесс падает
rem с STATUS_ILLEGAL_INSTRUCTION без внятного сообщения об ошибке.
rem Опционально для локальной сборки под современный CPU: set AEROS_AVX2=1
set "CPUARCHFLAGS=/arch:SSE2"
if /I "!OUT_ARCH!"=="arm64" set "CPUARCHFLAGS="
if defined AEROS_AVX2 if /I not "!OUT_ARCH!"=="arm64" set "CPUARCHFLAGS=/arch:AVX2"
echo [Aeros CPU] Arch flags: !CPUARCHFLAGS!

set "CXXFLAGS=/std:c++17 /EHsc /MD /W1 /O2 /Ot /GL !CPUARCHFLAGS! /openmp:llvm /fp:fast !DEFINES! !INCLUDES!"

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
cl /c !CXXFLAGS! src\benchmark.cpp /Fo:bin\benchmark.obj
if !errorlevel! neq 0 exit /b 1
cl /c !CXXFLAGS! src\debug_log.cpp /Fo:bin\debug_log.obj
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

echo [Aeros CPU] Compiling icon resource...
if exist src\app_icon.rc (
    rc /fo bin\app_icon.res src\app_icon.rc
    if !errorlevel! neq 0 (
        echo [WARN] Resource compile failed, continuing without icon
        set "ICON_RES="
    ) else (
        set "ICON_RES=bin\app_icon.res"
        echo [Aeros CPU] Icon resource compiled: !ICON_RES!
    )
) else (
    set "ICON_RES="
    echo [WARN] app_icon.rc not found, skipping icon
)

echo [Aeros CPU] Linking (LTCG optimized)...
link /LTCG /OUT:bin\main-!OUT_ARCH!.exe ^
  bin\main.obj bin\globals.obj bin\input.obj bin\gl_utils.obj bin\stl_loader.obj ^
  bin\voxel_grid.obj bin\flow_field.obj bin\particles.obj bin\streamlines.obj ^
  bin\forces.obj bin\model.obj bin\ui.obj bin\atmosphere.obj bin\test_mode.obj bin\lbm.obj bin\lang.obj bin\fsr.obj bin\framegen.obj bin\interesting.obj bin\vulkan_renderer.obj bin\lite_config.obj bin\cuda_stub.obj bin\benchmark.obj bin\debug_log.obj bin\glad.obj ^
  bin\imgui.obj bin\imgui_draw.obj bin\imgui_tables.obj bin\imgui_widgets.obj ^
  bin\imgui_impl_glfw.obj bin\imgui_impl_opengl3.obj !ICON_RES! ^
  /LIBPATH:"!LIBDIR!\glfw\!GLFW_LIBDIR!" glfw3.lib opengl32.lib user32.lib gdi32.lib shell32.lib comdlg32.lib ^
  /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup

if !errorlevel! neq 0 (
    echo [ERROR] Link failed
    exit /b 1
)

rem v1.25.0 — маркер бэкенда: эта сборка без CUDA (CPU_ONLY)
>bin\BUILD-BACKEND-!OUT_ARCH!.txt echo CPU_ONLY
echo [Aeros CPU] Backend marker: bin\BUILD-BACKEND-!OUT_ARCH!.txt = CPU_ONLY

rem bin\main.exe — ТОЛЬКО для локального run.bat; упаковщики берут main-<arch>.exe
copy /Y bin\main-!OUT_ARCH!.exe bin\main.exe >nul

echo [Aeros CPU] Build OK: bin\main-!OUT_ARCH!.exe
