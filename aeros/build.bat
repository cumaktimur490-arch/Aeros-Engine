@echo off
setlocal enabledelayedexpansion
rem =====================================================
rem Aeros Engine — универсальный build.bat v1.19.0 Vulkan + OpenGL + Linux ready
rem Поддерживает: x64 (по умолчанию), x86, arm64, Vulkan, OpenGL
rem Использование:
rem   build.bat         -> x64 auto (Vulkan+OpenGL)
rem   build.bat x64     -> x64
rem   build.bat x86     -> Win32
rem   build.bat vulkan  -> Vulkan only
rem   build.bat opengl  -> OpenGL only
rem   build.bat clean   -> очистка
rem   build.bat deps    -> только скачать зависимости
rem =====================================================

set "ARG1=%~1"
if "%ARG1%"=="" set "ARG1=x64"

if /I "%ARG1%"=="clean" goto :clean
if /I "%ARG1%"=="deps" goto :deps
if /I "%ARG1%"=="vulkan" set "RENDERER=vulkan" & set "ARG1=x64" & goto :start
if /I "%ARG1%"=="opengl" set "RENDERER=opengl" & set "ARG1=x64" & goto :start
if /I "%ARG1%"=="all" set "RENDERER=all" & set "ARG1=x64" & goto :start

:start
set "ARCH=%ARG1%"
if "%ARCH%"=="" set "ARCH=x64"
if /I "%ARCH%"=="x32" set "ARCH=x86"
if /I "%ARCH%"=="Win32" set "ARCH=x86"
if not defined RENDERER set "RENDERER=all"

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
    echo [ERROR] Unknown arch "%ARCH%". Use x64, x86, arm64, vulkan, opengl, deps, clean.
    exit /b 1
)

echo [Aeros] Building v1.19.0 for %OUT_ARCH% renderer=%RENDERER% using %VCVARS% ...

taskkill /IM main.exe /F 2>nul
taskkill /IM main-%OUT_ARCH%.exe /F 2>nul
taskkill /IM aeros-engine.exe /F 2>nul

rem --- Авто-загрузка зависимостей ---
call :download_deps
if !errorlevel! neq 0 (
    echo [WARN] Dependency download had issues, continuing...
)

rem --- Всегда инициализируем vcvars целевой архитектуры: при сборке нескольких
rem архитектур в одном CI-джобе иначе берётся чужой cl.exe из PATH ---

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

echo [Aeros] Cleaning previous build artifacts for full rebuild...
del /Q bin\*.obj 2>nul
del /Q bin\main-%OUT_ARCH%.exe 2>nul
del /Q bin\main.exe 2>nul
del /Q bin\aeros-engine*.exe 2>nul

rem --- Проверка GLFW lib ---
if not exist "!LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.lib" (
    echo [WARN] GLFW lib not found: !LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.lib
    if /I "!OUT_ARCH!"=="x86" (
        echo [INFO] Trying fallback...
        if exist "!LIBDIR!\glfw\lib-vc2022\glfw3.lib" (
            set "GLFW_LIBDIR=lib-vc2022"
        ) else (
            echo [ERROR] No GLFW lib found
            exit /b 1
        )
    ) else if /I "!OUT_ARCH!"=="arm64" (
        powershell -ExecutionPolicy Bypass -File "%~dp0..\tools\build-glfw-arm64.ps1" -Arch arm64
        if not exist "!LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.lib" (
            if exist "!LIBDIR!\glfw\lib-vc2022\glfw3.lib" set "GLFW_LIBDIR=lib-vc2022"
        )
    ) else (
        echo [ERROR] GLFW lib not found for x64: !LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.lib
        echo [INFO] Run build.bat deps to download
        exit /b 1
    )
)

rem --- Проверка Vulkan SDK ---
set "VULKAN_SDK_FOUND=0"
set "VULKAN_LIB="
if defined VULKAN_SDK (
    if exist "%VULKAN_SDK%\Lib\vulkan-1.lib" set "VULKAN_SDK_FOUND=1" & set "VULKAN_LIB=%VULKAN_SDK%\Lib\vulkan-1.lib"
)
if exist "%ProgramFiles%\VulkanSDK" (
    for /D %%D in ("%ProgramFiles%\VulkanSDK\*") do (
        if exist "%%D\Lib\vulkan-1.lib" set "VULKAN_SDK_FOUND=1" & set "VULKAN_LIB=%%D\Lib\vulkan-1.lib"
    )
)
if "%VULKAN_SDK_FOUND%"=="1" (
    echo [Aeros] Vulkan SDK found: !VULKAN_LIB!
) else (
    echo [WARN] Vulkan SDK not found — Vulkan renderer will be stub, OpenGL fallback will be used
    echo [INFO] Install Vulkan SDK from https://vulkan.lunarg.com/
)

rem --- Версия ---
set "VERSION_FILE=%~dp0..\VERSION"
set "APP_VERSION=1.19.0"
if exist "!VERSION_FILE!" (
    set /p APP_VERSION=<"!VERSION_FILE!"
)

echo [Aeros] Version: !APP_VERSION! Libs: !LIBDIR! GLFW: !GLFW_LIBDIR! Vulkan: !VULKAN_SDK_FOUND! Renderer: !RENDERER!

rem --- Иконка ---
if exist src\app_icon.rc (
    echo [Aeros] Compiling icon resource...
    rc /fo bin\app_icon.res src\app_icon.rc
    if !errorlevel! neq 0 (
        set "ICON_RES="
    ) else (
        set "ICON_RES=bin\app_icon.res"
    )
) else (
    set "ICON_RES="
)

rem --- Флаги ---
set "NVCC_ARCH=-arch=sm_75"
set "NVCC_FLAGS=-allow-unsupported-compiler -O3 --use_fast_math -Xcompiler /openmp:llvm -Xcompiler /arch:AVX2 -Xcompiler /O2 -Xcompiler /Ot -Xcompiler /fp:fast"

if "%RENDERER%"=="vulkan" set "NVCC_FLAGS=%NVCC_FLAGS% -Xcompiler /DFORCE_VULKAN -Xcompiler /DVULKAN_SUPPORTED"
if "%RENDERER%"=="opengl" set "NVCC_FLAGS=%NVCC_FLAGS% -Xcompiler /DFORCE_OPENGL"
if "%VULKAN_SDK_FOUND%"=="1" set "NVCC_FLAGS=%NVCC_FLAGS% -Xcompiler /DVULKAN_SUPPORTED -Xcompiler /DHAS_VULKAN_H"

set "VULKAN_LINK="
if "%VULKAN_SDK_FOUND%"=="1" (
    set "VULKAN_LINK=!VULKAN_LIB!"
)

echo [Aeros] Starting nvcc compilation for !OUT_ARCH! renderer=!RENDERER! ...

nvcc !NVCC_ARCH! !NVCC_FLAGS! -std=c++17 -Xcompiler /MD -Xcompiler /EHsc ^
  -I "!LIBDIR!\glfw\include" ^
  -I "!LIBDIR!\glad\include" ^
  -I "!LIBDIR!\glm" ^
  -I src ^
  -I src\imgui ^
  src\main.cpp src\globals.cpp src\input.cpp src\gl_utils.cpp src\stl_loader.cpp ^
  src\voxel_grid.cpp src\flow_field.cpp src\particles.cpp src\streamlines.cpp ^
  src\forces.cpp src\model.cpp src\ui.cpp src\atmosphere.cpp src\test_mode.cpp src\lbm.cpp src\lang.cpp src\fsr.cpp src\framegen.cpp src\interesting.cpp src\vulkan_renderer.cpp src\lite_config.cpp src\benchmark.cpp ^
  src\glad.c src\kernel.cu ^
  src\imgui\imgui.cpp src\imgui\imgui_draw.cpp src\imgui\imgui_tables.cpp src\imgui\imgui_widgets.cpp ^
  src\imgui\imgui_impl_glfw.cpp src\imgui\imgui_impl_opengl3.cpp ^
  -L "!LIBDIR!\glfw\!GLFW_LIBDIR!" ^
  -lglfw3 -lopengl32 -luser32 -lgdi32 -lshell32 -lcomdlg32 !VULKAN_LINK! !ICON_RES! ^
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
copy /Y bin\main-!OUT_ARCH!.exe bin\aeros-engine-%OUT_ARCH%.exe >nul
echo [Aeros] Build OK: bin\main-!OUT_ARCH%.exe and bin\aeros-engine-%OUT_ARCH%.exe

if exist "!LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.dll" (
    copy /Y "!LIBDIR!\glfw\!GLFW_LIBDIR!\glfw3.dll" bin\ >nul
    echo [Aeros] Copied glfw3.dll from !GLFW_LIBDIR!
)

if "%VULKAN_SDK_FOUND%"=="1" (
    if exist "%VULKAN_SDK%\Bin\vulkan-1.dll" copy /Y "%VULKAN_SDK%\Bin\vulkan-1.dll" bin\ >nul
)

:done
echo [Aeros] Done. Version !APP_VERSION! Arch !OUT_ARCH! Renderer !RENDERER! Vulkan !VULKAN_SDK_FOUND!
goto :eof

:download_deps
echo [Aeros] Checking and downloading dependencies...
set "LIBDIR=%~dp0..\libs"
if not exist "%LIBDIR%" mkdir "%LIBDIR%"

rem GLM
if not exist "%LIBDIR%\glm\glm\glm.hpp" (
    echo [Deps] GLM not found, downloading...
    powershell -Command "if (!(Test-Path '%LIBDIR%\glm')) { New-Item -ItemType Directory -Force -Path '%LIBDIR%\glm' | Out-Null }; if (Test-Path env:TEMP) { $tmp = Join-Path $env:TEMP 'glm_aeros'; if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }; git clone --depth 1 https://github.com/g-truc/glm.git $tmp; if (Test-Path $tmp\glm) { Copy-Item -Recurse -Force $tmp\glm '%LIBDIR%\glm\'; Write-Host '[Deps] GLM downloaded via git' } else { Write-Host '[Deps] GLM git clone failed' } } else { Write-Host '[Deps] TEMP not found' }"
    if not exist "%LIBDIR%\glm\glm\glm.hpp" (
        echo [Deps] Trying curl for GLM...
        powershell -Command "try { Invoke-WebRequest -Uri 'https://github.com/g-truc/glm/archive/refs/tags/0.9.9.8.zip' -OutFile \"$env:TEMP\glm.zip\"; Expand-Archive -Path \"$env:TEMP\glm.zip\" -DestinationPath \"$env:TEMP\" -Force; Copy-Item -Recurse -Force \"$env:TEMP\glm-0.9.9.8\glm\" \"%LIBDIR%\glm\"; } catch { Write-Host '[Deps] GLM download failed' }"
    )
) else (
    echo [Deps] GLM OK
)

rem GLAD
if not exist "%LIBDIR%\glad\include\glad\glad.h" (
    echo [Deps] GLAD not found, downloading...
    if not exist "%LIBDIR%\glad\include\glad" mkdir "%LIBDIR%\glad\include\glad"
    if not exist "%LIBDIR%\glad\include\KHR" mkdir "%LIBDIR%\glad\include\KHR"
    if not exist "%LIBDIR%\glad\src" mkdir "%LIBDIR%\glad\src"
    powershell -Command "try { Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/Dav1dde/glad/master/include/glad/glad.h' -OutFile '%LIBDIR%\glad\include\glad\glad.h'; Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/Dav1dde/glad/master/src/glad.c' -OutFile '%LIBDIR%\glad\src\glad.c'; Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/Dav1dde/glad/master/include/KHR/khrplatform.h' -OutFile '%LIBDIR%\glad\include\KHR\khrplatform.h'; Write-Host '[Deps] GLAD downloaded' } catch { Write-Host '[Deps] GLAD download failed' }"
) else (
    echo [Deps] GLAD OK
)

rem GLFW
if not exist "%LIBDIR%\glfw\include\GLFW\glfw3.h" (
    echo [Deps] GLFW header not found, downloading...
    if not exist "%LIBDIR%\glfw\include\GLFW" mkdir "%LIBDIR%\glfw\include\GLFW"
    powershell -Command "try { Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/glfw/glfw/master/include/GLFW/glfw3.h' -OutFile '%LIBDIR%\glfw\include\GLFW\glfw3.h'; Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/glfw/glfw/master/include/GLFW/glfw3native.h' -OutFile '%LIBDIR%\glfw\include\GLFW\glfw3native.h'; Write-Host '[Deps] GLFW headers downloaded' } catch { Write-Host '[Deps] GLFW header download failed' }"
) else (
    echo [Deps] GLFW header OK
)

if not exist "%LIBDIR%\glfw\lib-vc2022\glfw3.lib" (
    echo [Deps] GLFW lib not found for x64, downloading prebuilt...
    powershell -Command "try { $url='https://github.com/glfw/glfw/releases/download/3.3.8/glfw-3.3.8.bin.WIN64.zip'; Invoke-WebRequest -Uri $url -OutFile \"$env:TEMP\glfw.zip\"; Expand-Archive -Path \"$env:TEMP\glfw.zip\" -DestinationPath \"$env:TEMP\" -Force; $src=Get-ChildItem \"$env:TEMP\glfw-3.3.8.bin.WIN64\" -Recurse -Filter glfw3.lib | Select-Object -First 1; if ($src) { New-Item -ItemType Directory -Force -Path '%LIBDIR%\glfw\lib-vc2022' | Out-Null; Copy-Item $src.FullName '%LIBDIR%\glfw\lib-vc2022\glfw3.lib' -Force; $dll=Get-ChildItem \"$env:TEMP\glfw-3.3.8.bin.WIN64\" -Recurse -Filter glfw3.dll | Select-Object -First 1; if ($dll) { Copy-Item $dll.FullName '%LIBDIR%\glfw\lib-vc2022\' -Force }; Write-Host '[Deps] GLFW lib downloaded' } } catch { Write-Host '[Deps] GLFW lib download failed' }"
) else (
    echo [Deps] GLFW lib OK
)

rem ImGui
if not exist "src\imgui\imgui.h" (
    echo [Deps] ImGui not found, downloading...
    powershell -Command "try { $tmp=Join-Path $env:TEMP 'imgui_aeros'; if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }; git clone --depth 1 https://github.com/ocornut/imgui.git $tmp; if (Test-Path $tmp\imgui.h) { New-Item -ItemType Directory -Force -Path 'src\imgui' | Out-Null; Copy-Item $tmp\*.cpp, $tmp\*.h src\imgui\ -Force; Copy-Item $tmp\backends\imgui_impl_glfw.* src\imgui\ -Force; Copy-Item $tmp\backends\imgui_impl_opengl3.* src\imgui\ -Force; Write-Host '[Deps] ImGui downloaded' } } catch { Write-Host '[Deps] ImGui download failed' }"
) else (
    echo [Deps] ImGui OK
)

echo [Deps] All dependencies checked
goto :eof

:deps
call :download_deps
goto :eof

:clean
echo [Aeros] Cleaning...
if exist bin\main.exe del /Q bin\main.exe
if exist bin\main-x64.exe del /Q bin\main-x64.exe
if exist bin\main-x86.exe del /Q bin\main-x86.exe
if exist bin\aeros-engine*.exe del /Q bin\aeros-engine*.exe
if exist bin\*.obj del /Q bin\*.obj
echo [Aeros] Clean done.
goto :eof
