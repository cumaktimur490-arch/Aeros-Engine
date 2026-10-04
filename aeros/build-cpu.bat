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

where cl.exe >nul 2>&1
if !errorlevel! neq 0 (
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

set "VERSION_FILE=%~dp0..\VERSION"
set "APP_VERSION=1.0.0"
if exist "!VERSION_FILE!" set /p APP_VERSION=<"!VERSION_FILE!"

echo [Aeros CPU] Version: !APP_VERSION! Arch: !OUT_ARCH!
echo [Aeros CPU] LibDir: !LIBDIR! GLFW: !GLFW_LIBDIR!

del /Q bin\*.obj 2>nul

echo [Aeros CPU] Compiling glad.c...
cl /c /MD /TC /I "!LIBDIR!\glad\include" src\glad.c /Fo:bin\glad.obj
if !errorlevel! neq 0 (
    echo [ERROR] glad.c compile failed
    exit /b 1
)

echo [Aeros CPU] Compiling C++ files...

set "INCLUDES=/I "!LIBDIR!\glfw\include" /I "!LIBDIR!\glad\include" /I "!LIBDIR!\glm" /I src /I src\imgui"
set "DEFINES=/DCPU_ONLY /DVERSION_STRING=\"!APP_VERSION!\""
set "CXXFLAGS=/std:c++17 /EHsc /MD /W1 !DEFINES! !INCLUDES!"

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

echo [Aeros CPU] Linking...
link /OUT:bin\main-!OUT_ARCH!.exe ^
  bin\main.obj bin\globals.obj bin\input.obj bin\gl_utils.obj bin\stl_loader.obj ^
  bin\voxel_grid.obj bin\flow_field.obj bin\particles.obj bin\streamlines.obj ^
  bin\forces.obj bin\model.obj bin\ui.obj bin\cuda_stub.obj bin\glad.obj ^
  bin\imgui.obj bin\imgui_draw.obj bin\imgui_tables.obj bin\imgui_widgets.obj ^
  bin\imgui_impl_glfw.obj bin\imgui_impl_opengl3.obj ^
  /LIBPATH:"!LIBDIR!\glfw\!GLFW_LIBDIR!" glfw3.lib opengl32.lib user32.lib gdi32.lib shell32.lib comdlg32.lib ^
  /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup

if !errorlevel! neq 0 (
    echo [ERROR] Link failed
    exit /b 1
)

copy /Y bin\main-!OUT_ARCH!.exe bin\main.exe >nul
if exist "!LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.dll" copy /Y "!LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.dll" bin\ >nul

echo [Aeros CPU] Build OK: bin\main-!OUT_ARCH!.exe
