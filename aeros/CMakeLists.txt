# Экспериментальная CMake-сборка (основной способ — build.bat).
# Требуется: CMake >= 3.18, Visual Studio 2022, CUDA Toolkit.
#   cmake -B build -G "Visual Studio 17 2022" -A x64
#   cmake --build build --config Release
cmake_minimum_required(VERSION 3.18)
project(AerosEngine LANGUAGES C CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_ARCHITECTURES 75)   # как и build.bat: целевая карта — Turing

set(LIBDIR ${CMAKE_CURRENT_SOURCE_DIR}/../libs)

add_executable(main
    src/main.cpp
    src/globals.cpp
    src/gl_utils.cpp
    src/stl_loader.cpp
    src/voxel_grid.cpp
    src/flow_field.cpp
    src/particles.cpp
    src/streamlines.cpp
    src/forces.cpp
    src/model.cpp
    src/ui.cpp
    src/glad.c
    src/kernel.cu
    src/imgui/imgui.cpp
    src/imgui/imgui_draw.cpp
    src/imgui/imgui_tables.cpp
    src/imgui/imgui_widgets.cpp
    src/imgui/imgui_impl_glfw.cpp
    src/imgui/imgui_impl_opengl3.cpp
)

target_include_directories(main PRIVATE
    ${LIBDIR}/glfw/include
    ${LIBDIR}/glad/include
    ${LIBDIR}/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui
)

if(WIN32)
    # Пребилд GLFW из репозитория (lib-vc2022)
    target_link_directories(main PRIVATE ${LIBDIR}/glfw/lib-vc2022)
    target_link_libraries(main PRIVATE glfw3 opengl32 user32 gdi32 shell32 comdlg32)
    if(MSVC)
        # GUI-подсистема с обычной main() — как /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup в build.bat
        target_link_options(main PRIVATE "/SUBSYSTEM:WINDOWS" "/ENTRY:mainCRTStartup")
    endif()
else()
    message(WARNING "Проект поддерживает только Windows (WinAPI-диалог, пребилд GLFW).")
endif()
