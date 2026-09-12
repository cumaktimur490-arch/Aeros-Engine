@echo off
taskkill /IM main.exe /F 2>nul
call "D:\c++\VC\Auxiliary\Build\vcvars64.bat"
if not exist bin mkdir bin

nvcc -arch=sm_75 -std=c++17 -Xcompiler /MD ^
  -I "C:/Users/USER/Desktop/dev/libs/glfw/include" ^
  -I "C:/Users/USER/Desktop/dev/libs/glad/include" ^
  -I "C:/Users/USER/Desktop/dev/libs/glm" ^
  -I src ^
  -I src/imgui ^
  src/main.cpp src/glad.c src/kernel.cu ^
  src/imgui/imgui.cpp src/imgui/imgui_draw.cpp src/imgui/imgui_tables.cpp src/imgui/imgui_widgets.cpp ^
  src/imgui/imgui_impl_glfw.cpp src/imgui/imgui_impl_opengl3.cpp ^
  -L "C:/Users/USER/Desktop/dev/libs/glfw/lib-vc2022" ^
  -lglfw3 -lopengl32 -luser32 -lgdi32 -lshell32 -lcomdlg32 ^
  -Xlinker /SUBSYSTEM:WINDOWS -Xlinker /ENTRY:mainCRTStartup ^
  -o bin/main.exe

pause