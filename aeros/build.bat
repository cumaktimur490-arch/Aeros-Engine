@echo off
taskkill /IM main.exe /F 2>nul
call "D:\c++\VC\Auxiliary\Build\vcvars64.bat"
if not exist bin mkdir bin

rem Библиотеки берём из репозитория (папка libs рядом с aeros)
set "LIBDIR=%~dp0..\libs"

nvcc -arch=sm_75 -std=c++17 -Xcompiler /MD ^
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
  -L "%LIBDIR%\glfw\lib-vc2022" ^
  -lglfw3 -lopengl32 -luser32 -lgdi32 -lshell32 -lcomdlg32 ^
  -Xlinker /SUBSYSTEM:WINDOWS -Xlinker /ENTRY:mainCRTStartup ^
  -o bin\main.exe

pause
