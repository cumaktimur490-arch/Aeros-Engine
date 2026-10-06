@echo off
REM Aeros Engine — Ultra-Lite сборка для самых слабых устройств v1.20.1
REM Atom/Celeron, 1-2 ядра, 2GB RAM, HD 3000, 800x450, 20 FPS, 1 поток
REM Использование: build-ultra-lite.bat [x64|x86|clean|deps]

setlocal EnableDelayedExpansion
chdir /d "%~dp0"
set ROOT=%CD%\..
set ARCH=x64
set MODE=all
if not "%~1"=="" set MODE=%~1
if /I "%MODE%"=="x64" set ARCH=x64
if /I "%MODE%"=="x86" set ARCH=x86
if /I "%MODE%"=="ultra" set ARCH=x64

echo [Aeros Ultra-Lite] Building v1.20.1 Ultra-Lite for %ARCH% — Potato mode for Atom/Celeron 2GB RAM
echo [Aeros Ultra-Lite] Optimized: SSE2, no AVX2, 1 thread, 500 particles, 4x40 streamlines, voxel 16, 20 FPS, 800x450

if /I "%MODE%"=="clean" goto :clean
if /I "%MODE%"=="deps" goto :download_deps
if /I "%MODE%"=="check-deps" goto :check_deps

REM Авто-загрузка зависимостей
call :download_deps

REM Проверка MSVC
set MSVC_FOUND=0
where cl >nul 2>&1
if %ERRORLEVEL%==0 set MSVC_FOUND=1
if %MSVC_FOUND%==0 (
  echo [ERROR] MSVC not found — run from Developer Command Prompt or install VS 2022
  exit /b 1
)

set LIBDIR=%ROOT%\libs
set SRCDIR=%CD%\src
set BINDIR=%CD%\bin
set OBJDIR=%CD%\obj-ultra-lite

if not exist "%BINDIR%" mkdir "%BINDIR%"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"

REM Ultra-Lite флаги — максимально совместимые
REM O1, SSE2 only, no AVX, no AVX2, 1 thread, small binary, low RAM
set INCLUDES=/I"%LIBDIR%\glad\include" /I"%LIBDIR%\glfw\include" /I"%LIBDIR%\glm" /I"%SRCDIR%" /I"%SRCDIR%\imgui"
set DEFINES=/DAEROS_LITE /DULTRA_LITE /DCPU_ONLY /DLITE_MODE /DPOTATO_MODE /DVERSION_STRING="\"1.20.1 Ultra-Lite\"" /DGLFW_INCLUDE_NONE /DNOMINMAX /DWIN32_LEAN_AND_MEAN
set CXXFLAGS=/std:c++17 /EHsc /MD /W1 /O1 /arch:SSE2 /openmp /fp:precise /DNDEBUG /Gy /Gw

set SOURCES=%SRCDIR%\main.cpp %SRCDIR%\globals.cpp %SRCDIR%\input.cpp %SRCDIR%\gl_utils.cpp %SRCDIR%\stl_loader.cpp %SRCDIR%\voxel_grid.cpp %SRCDIR%\flow_field.cpp %SRCDIR%\particles.cpp %SRCDIR%\streamlines.cpp %SRCDIR%\forces.cpp %SRCDIR%\model.cpp %SRCDIR%\ui.cpp %SRCDIR%\atmosphere.cpp %SRCDIR%\test_mode.cpp %SRCDIR%\lbm.cpp %SRCDIR%\lang.cpp %SRCDIR%\fsr.cpp %SRCDIR%\framegen.cpp %SRCDIR%\interesting.cpp %SRCDIR%\vulkan_renderer.cpp %SRCDIR%\lite_config.cpp %SRCDIR%\cuda_stub.cpp %SRCDIR%\glad.c %SRCDIR%\imgui\imgui.cpp %SRCDIR%\imgui\imgui_draw.cpp %SRCDIR%\imgui\imgui_tables.cpp %SRCDIR%\imgui\imgui_widgets.cpp %SRCDIR%\imgui\imgui_impl_glfw.cpp %SRCDIR%\imgui\imgui_impl_opengl3.cpp

echo [CXX Ultra-Lite] Compiling for Atom/Celeron...
set OBJ_FILES=
for %%f in (%SOURCES%) do (
  set fname=%%~nf
  cl %CXXFLAGS% %DEFINES% %INCLUDES% /c "%%f" /Fo"%OBJDIR%\!fname!.obj" /nologo
  if !ERRORLEVEL! neq 0 (
    echo [ERROR] Failed to compile %%f
    exit /b 1
  )
  set OBJ_FILES=!OBJ_FILES! "%OBJDIR%\!fname!.obj"
)

echo [LD Ultra-Lite] Linking aeros-engine-ultra-lite-%ARCH%.exe
set LIBS=opengl32.lib glfw3.lib user32.lib gdi32.lib shell32.lib ole32.lib
set LIBPATHS=/LIBPATH:"%LIBDIR%\glfw\lib\%ARCH%" /LIBPATH:"%LIBDIR%\glfw\lib-vc2022\%ARCH%"

link %OBJ_FILES% /OUT:"%BINDIR%\main-ultra-lite-%ARCH%.exe" %LIBS% %LIBPATHS% /SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF /nologo
if %ERRORLEVEL% neq 0 (
  echo [ERROR] Link failed
  exit /b 1
)

copy /Y "%BINDIR%\main-ultra-lite-%ARCH%.exe" "%BINDIR%\main-ultra-lite.exe" >nul
copy /Y "%BINDIR%\main-ultra-lite-%ARCH%.exe" "%BINDIR%\aeros-engine-ultra-lite-%ARCH%.exe" >nul
copy /Y "%BINDIR%\main-ultra-lite-%ARCH%.exe" "%BINDIR%\aeros-engine-ultra-lite.exe" >nul
copy /Y "%BINDIR%\main-ultra-lite-%ARCH%.exe" "%BINDIR%\aeros-engine-potato.exe" >nul

echo [OK] Ultra-Lite built: %BINDIR%\main-ultra-lite-%ARCH%.exe
for %%f in ("%BINDIR%\main-ultra-lite-%ARCH%.exe") do echo Size: %%~zf bytes — Potato mode for Atom/Celeron 2GB RAM
goto :eof

:download_deps
echo [Aeros Ultra-Lite Deps] Downloading minimal dependencies...
powershell -ExecutionPolicy Bypass -File "%ROOT%\installer\download_deps.ps1" -Arch %ARCH% -Quiet
goto :eof

:check_deps
echo [Aeros Ultra-Lite Deps] Checking...
if not exist "%LIBDIR%\glm\glm\glm.hpp" echo [WARN] GLM missing — run build-ultra-lite.bat deps
if not exist "%LIBDIR%\glad\include\glad\glad.h" echo [WARN] GLAD missing
echo [OK] Ultra-Lite needs only: MSVC, GLFW, OpenGL — no Vulkan, no CUDA, minimal
goto :eof

:clean
echo [Aeros Ultra-Lite Clean] Removing...
rmdir /s /q "%OBJDIR%" 2>nul
del /q "%BINDIR%\*ultra-lite*.exe" 2>nul
del /q "%BINDIR%\*potato*.exe" 2>nul
echo [OK] Cleaned
goto :eof
