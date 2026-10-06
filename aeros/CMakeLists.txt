# Aeros Engine — CMake сборка (экспериментальная, основной способ — build.bat)
# Требуется: CMake >= 3.18, Visual Studio 2022, CUDA Toolkit.
#   cmake -B build -G "Visual Studio 17 2022" -A x64
#   cmake --build build --config Release
#   cmake -B build32 -G "Visual Studio 17 2022" -A Win32
#   cmake --build build32 --config Release
# Для CPU-only (без CUDA):
#   cmake -B build -DAEROS_CPU_ONLY=ON

cmake_minimum_required(VERSION 3.18)

# Читаем версию из ../VERSION если есть
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" APP_VERSION_RAW)
    string(STRIP "${APP_VERSION_RAW}" APP_VERSION)
else()
    set(APP_VERSION "1.0.0")
endif()

option(AEROS_CPU_ONLY "Build without CUDA (CPU fallback only)" OFF)

if(AEROS_CPU_ONLY)
    project(AerosEngine VERSION ${APP_VERSION} LANGUAGES C CXX)
    message(STATUS "Aeros Engine v${APP_VERSION} CPU-ONLY mode")
else()
    project(AerosEngine VERSION ${APP_VERSION} LANGUAGES C CXX CUDA)
    message(STATUS "Aeros Engine v${APP_VERSION} CUDA mode")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT AEROS_CPU_ONLY)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_ARCHITECTURES 75)  # Для совместимости, как build.bat
endif()

# v1.6.0 Optimized — OpenMP, O2, AVX2, fast math
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found: ${OpenMP_CXX_VERSION}")
else()
    message(STATUS "OpenMP not found — building without")
endif()

# v1.9.0 fix: OpenMP flag handling for MSVC compatibility
if(MSVC)
    # Check MSVC version for openmp:llvm support (VS 2019 16.10+)
    if(MSVC_VERSION GREATER_EQUAL 1929)
        add_compile_options(/O2 /Ot /arch:AVX2 /fp:fast /openmp:llvm)
    else()
        add_compile_options(/O2 /Ot /arch:AVX2 /fp:fast /openmp)
    endif()
    add_link_options(/LTCG)
else()
    add_compile_options(-O3 -march=native -ffast-math -fopenmp)
    add_link_options(-fopenmp)
endif()

set(LIBDIR ${CMAKE_CURRENT_SOURCE_DIR}/../libs)

# Определяем архитектуру
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(AEROS_ARCH "x64")
    set(GLFW_LIB_DIR "lib-vc2022")
else()
    set(AEROS_ARCH "x86")
    set(GLFW_LIB_DIR "lib-vc2022-x86")
    if(NOT EXISTS "${LIBDIR}/glfw/${GLFW_LIB_DIR}")
        set(GLFW_LIB_DIR "lib-vc2022")
        message(WARNING "GLFW x86 lib not found, using x64 lib as fallback — build may fail. Run tools/get-glfw-x86.ps1")
    endif()
endif()

message(STATUS "Aeros Engine v${APP_VERSION} Arch: ${AEROS_ARCH} GLFW: ${GLFW_LIB_DIR}")

set(SOURCES
    src/main.cpp
    src/globals.cpp
    src/input.cpp
    src/gl_utils.cpp
    src/stl_loader.cpp
    src/voxel_grid.cpp
    src/flow_field.cpp
    src/particles.cpp
    src/streamlines.cpp
    src/forces.cpp
    src/model.cpp
    src/ui.cpp
    src/atmosphere.cpp
    src/test_mode.cpp
    src/lbm.cpp
    src/lang.cpp
    src/fsr.cpp
    src/framegen.cpp
    src/interesting.cpp
    src/glad.c
    src/imgui/imgui.cpp
    src/imgui/imgui_draw.cpp
    src/imgui/imgui_tables.cpp
    src/imgui/imgui_widgets.cpp
    src/imgui/imgui_impl_glfw.cpp
    src/imgui/imgui_impl_opengl3.cpp
)

if(AEROS_CPU_ONLY)
    list(APPEND SOURCES src/cuda_stub.cpp)
else()
    list(APPEND SOURCES src/kernel.cu)
endif()

if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/app_icon.rc")
    list(APPEND SOURCES src/app_icon.rc)
    message(STATUS "Including app icon resource: src/app_icon.rc")
endif()

add_executable(main ${SOURCES})

target_include_directories(main PRIVATE
    ${LIBDIR}/glfw/include
    ${LIBDIR}/glad/include
    ${LIBDIR}/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui
)

target_compile_definitions(main PRIVATE VERSION="${APP_VERSION}")
if(AEROS_CPU_ONLY)
    target_compile_definitions(main PRIVATE CPU_ONLY)
endif()
if(OpenMP_CXX_FOUND)
    target_link_libraries(main PRIVATE OpenMP::OpenMP_CXX)
endif()

if(WIN32)
    target_link_directories(main PRIVATE ${LIBDIR}/glfw/${GLFW_LIB_DIR})
    target_link_libraries(main PRIVATE glfw3 opengl32 user32 gdi32 shell32 comdlg32)
    if(MSVC)
        target_link_options(main PRIVATE "/SUBSYSTEM:WINDOWS" "/ENTRY:mainCRTStartup")
    endif()
    add_custom_command(TARGET main POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LIBDIR}/glfw/${GLFW_LIB_DIR}/glfw3.dll"
            $<TARGET_FILE_DIR:main>
        COMMENT "Copying glfw3.dll..."
    )
else()
    message(WARNING "Проект поддерживает только Windows (WinAPI-диалог, пребилд GLFW).")
endif()

install(TARGETS main DESTINATION .)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/bin/glfw3.dll DESTINATION . OPTIONAL)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/bin/sl.common.dll DESTINATION . OPTIONAL)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/bin/sl.dlss.dll DESTINATION . OPTIONAL)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/bin/sl.interposer.dll DESTINATION . OPTIONAL)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin/ DESTINATION . FILES_MATCHING PATTERN "*.stl")
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE DESTINATION . RENAME LICENSE.txt OPTIONAL)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../README.md DESTINATION . RENAME README.txt OPTIONAL)

set(CPACK_PACKAGE_NAME "Aeros-Engine")
set(CPACK_PACKAGE_VERSION ${APP_VERSION})
set(CPACK_PACKAGE_VENDOR "GoGonam AoS.")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Aeros Engine - Airflow visualization")
set(CPACK_GENERATOR "ZIP")
if(AEROS_CPU_ONLY)
    set(CPACK_PACKAGE_FILE_NAME "Aeros-Engine-Portable-${AEROS_ARCH}-v${APP_VERSION}-CPU")
else()
    set(CPACK_PACKAGE_FILE_NAME "Aeros-Engine-Portable-${AEROS_ARCH}-v${APP_VERSION}-CMake")
endif()
include(CPack)
