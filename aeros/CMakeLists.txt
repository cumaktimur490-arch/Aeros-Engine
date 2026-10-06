# Aeros Engine — CMake сборка v1.19.0 Vulkan + OpenGL + Linux/Windows
# Требуется: CMake >= 3.18, Visual Studio 2022 или GCC/Clang, Vulkan SDK опционально
#   Windows: cmake -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Release
#   Linux:   cmake -B build && cmake --build build
#   CPU-only: cmake -B build -DAEROS_CPU_ONLY=ON
#   Vulkan:   cmake -B build -DAEROS_ENABLE_VULKAN=ON
#   OpenGL:   cmake -B build -DAEROS_ENABLE_VULKAN=OFF

cmake_minimum_required(VERSION 3.18)

# Читаем версию из ../VERSION если есть
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" APP_VERSION_RAW)
    string(STRIP "${APP_VERSION_RAW}" APP_VERSION)
else()
    set(APP_VERSION "1.19.0")
endif()

option(AEROS_CPU_ONLY "Build without CUDA (CPU fallback only)" OFF)
option(AEROS_ENABLE_VULKAN "Enable Vulkan renderer" ON)
option(AEROS_ENABLE_OPENGL "Enable OpenGL renderer" ON)
option(AEROS_DOWNLOAD_DEPS "Auto-download missing dependencies (GLM, GLAD, ImGui)" ON)
option(AEROS_LITE "Build Lite version for weak devices i3-3xxx / HD 4000 / No CUDA" OFF)

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
    set(CMAKE_CUDA_ARCHITECTURES 75)
endif()

# OpenMP
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found: ${OpenMP_CXX_VERSION}")
else()
    message(STATUS "OpenMP not found — building without")
endif()

# Compiler flags
# Lite vs Full flags
if(AEROS_LITE)
    message(STATUS "LITE MODE — for weak devices i3-3xxx / HD 4000 / 4GB RAM / No CUDA")
    if(MSVC)
        # Lite: O1, SSE2 only (no AVX2), precise math, no LTCG for small binary
        add_compile_options(/O1 /arch:SSE2 /fp:precise /openmp)
        # No LTCG
    else()
        # Lite: O1, SSE2, no AVX2, no fast-math
        add_compile_options(-O1 -msse2 -mfpmath=sse -ffast-math -fopenmp -fPIC)
        add_link_options(-fopenmp)
    endif()
    add_compile_definitions(AEROS_LITE LITE_MODE CPU_ONLY)
else()
    if(MSVC)
        if(MSVC_VERSION GREATER_EQUAL 1929)
            add_compile_options(/O2 /Ot /arch:AVX2 /fp:fast /openmp:llvm)
        else()
            add_compile_options(/O2 /Ot /arch:AVX2 /fp:fast /openmp)
        endif()
        add_link_options(/LTCG)
    else()
        add_compile_options(-O3 -march=native -ffast-math -fopenmp -fPIC)
        add_link_options(-fopenmp)
    endif()
endif()

# Vulkan
set(VULKAN_FOUND FALSE)
if(AEROS_ENABLE_VULKAN)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        message(STATUS "Vulkan found: ${Vulkan_VERSION} — enabling Vulkan renderer")
        set(VULKAN_FOUND TRUE)
        add_compile_definitions(VULKAN_SUPPORTED HAS_VULKAN_H)
    else()
        message(STATUS "Vulkan not found — Vulkan renderer will be stub (runtime check will fallback to OpenGL)")
        message(STATUS "  Install Vulkan SDK: https://vulkan.lunarg.com/ or sudo apt install libvulkan-dev")
    endif()
endif()

# GLFW
set(LIBDIR ${CMAKE_CURRENT_SOURCE_DIR}/../libs)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GLFW3 QUIET glfw3)
    if(GLFW3_FOUND)
        message(STATUS "GLFW3 found via pkg-config: ${GLFW3_VERSION}")
    endif()
endif()

# Определяем архитектуру
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(AEROS_ARCH "x64")
    set(GLFW_LIB_DIR "lib-vc2022")
else()
    set(AEROS_ARCH "x86")
    set(GLFW_LIB_DIR "lib-vc2022-x86")
    if(NOT EXISTS "${LIBDIR}/glfw/${GLFW_LIB_DIR}")
        set(GLFW_LIB_DIR "lib-vc2022")
        message(WARNING "GLFW x86 lib not found, using x64 lib as fallback — run tools/get-glfw-x86.ps1")
    endif()
endif()

message(STATUS "Aeros Engine v${APP_VERSION} Arch: ${AEROS_ARCH} GLFW: ${GLFW_LIB_DIR} Vulkan: ${VULKAN_FOUND}")

# Auto-download deps if enabled
if(AEROS_DOWNLOAD_DEPS)
    if(NOT EXISTS "${LIBDIR}/glm/glm/glm.hpp")
        message(STATUS "GLM not found, attempting to download...")
        # Try to find git
        find_package(Git QUIET)
        if(Git_FOUND)
            execute_process(COMMAND ${GIT_EXECUTABLE} clone --depth 1 https://github.com/g-truc/glm.git ${CMAKE_BINARY_DIR}/glm_tmp
                RESULT_VARIABLE GIT_RESULT
                OUTPUT_QUIET ERROR_QUIET)
            if(GIT_RESULT EQUAL 0 AND EXISTS "${CMAKE_BINARY_DIR}/glm_tmp/glm")
                file(COPY ${CMAKE_BINARY_DIR}/glm_tmp/glm DESTINATION ${LIBDIR}/glm)
                message(STATUS "GLM downloaded via git")
            endif()
        endif()
    endif()
    if(NOT EXISTS "${LIBDIR}/glad/include/glad/glad.h")
        message(STATUS "GLAD not found — will try system OpenGL or download at build time")
    endif()
endif()

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
    src/vulkan_renderer.cpp
    src/lite_config.cpp
    src/benchmark.cpp
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

# Executable name per platform
if(WIN32)
    add_executable(aeros-engine ${SOURCES})
    set_target_properties(aeros-engine PROPERTIES OUTPUT_NAME "main-${AEROS_ARCH}")
else()
    add_executable(aeros-engine ${SOURCES})
    set_target_properties(aeros-engine PROPERTIES OUTPUT_NAME "aeros-engine-linux-${CMAKE_SYSTEM_PROCESSOR}")
endif()

target_include_directories(aeros-engine PRIVATE
    ${LIBDIR}/glfw/include
    ${LIBDIR}/glad/include
    ${LIBDIR}/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui
)

if(GLFW3_FOUND)
    target_include_directories(aeros-engine PRIVATE ${GLFW3_INCLUDE_DIRS})
    target_link_directories(aeros-engine PRIVATE ${GLFW3_LIBRARY_DIRS})
endif()

if(VULKAN_FOUND)
    target_include_directories(aeros-engine PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(aeros-engine PRIVATE ${Vulkan_LIBRARIES})
endif()

target_compile_definitions(aeros-engine PRIVATE VERSION="${APP_VERSION}")
if(AEROS_CPU_ONLY)
    target_compile_definitions(aeros-engine PRIVATE CPU_ONLY)
endif()
if(VULKAN_FOUND)
    target_compile_definitions(aeros-engine PRIVATE HAS_VULKAN_H VULKAN_SUPPORTED)
endif()
if(UNIX AND NOT APPLE)
    target_compile_definitions(aeros-engine PRIVATE LINUX GLFW_INCLUDE_NONE)
endif()

if(OpenMP_CXX_FOUND)
    target_link_libraries(aeros-engine PRIVATE OpenMP::OpenMP_CXX)
endif()

if(WIN32)
    target_link_directories(aeros-engine PRIVATE ${LIBDIR}/glfw/${GLFW_LIB_DIR})
    target_link_libraries(aeros-engine PRIVATE glfw3 opengl32 user32 gdi32 shell32 comdlg32)
    if(MSVC)
        target_link_options(aeros-engine PRIVATE "/SUBSYSTEM:WINDOWS" "/ENTRY:mainCRTStartup")
    endif()
    add_custom_command(TARGET aeros-engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LIBDIR}/glfw/${GLFW_LIB_DIR}/glfw3.dll"
            $<TARGET_FILE_DIR:aeros-engine>
        COMMENT "Copying glfw3.dll..."
    )
else()
    # Linux
    if(GLFW3_FOUND)
        target_link_libraries(aeros-engine PRIVATE ${GLFW3_LIBRARIES})
    else()
        target_link_libraries(aeros-engine PRIVATE glfw)
    endif()
    target_link_libraries(aeros-engine PRIVATE GL X11 Xrandr Xinerama Xcursor Xi dl pthread)
    if(VULKAN_FOUND)
        target_link_libraries(aeros-engine PRIVATE vulkan)
    endif()
    # For file dialogs on Linux, we use zenity/kdialog via popen — no extra lib needed
    message(STATUS "Linux build — using system GLFW, OpenGL, X11, Vulkan (if found)")
endif()

# Install
install(TARGETS aeros-engine DESTINATION .)
if(WIN32)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/bin/glfw3.dll DESTINATION . OPTIONAL)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/bin/sl.common.dll DESTINATION . OPTIONAL)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/bin/sl.dlss.dll DESTINATION . OPTIONAL)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/bin/sl.interposer.dll DESTINATION . OPTIONAL)
endif()
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin/ DESTINATION . FILES_MATCHING PATTERN "*.stl" OPTIONAL)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE DESTINATION . RENAME LICENSE.txt OPTIONAL)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../README.md DESTINATION . RENAME README.txt OPTIONAL)

# For Linux, also install desktop file
if(UNIX AND NOT APPLE)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../packaging/aeros-engine.desktop.in ${CMAKE_BINARY_DIR}/aeros-engine.desktop @ONLY OPTIONAL)
    install(FILES ${CMAKE_BINARY_DIR}/aeros-engine.desktop DESTINATION share/applications OPTIONAL)
endif()

set(CPACK_PACKAGE_NAME "Aeros-Engine")
set(CPACK_PACKAGE_VERSION ${APP_VERSION})
set(CPACK_PACKAGE_VENDOR "GoGonam AoS.")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Aeros Engine - Airflow visualization with Vulkan and OpenGL")
set(CPACK_GENERATOR "ZIP;TGZ")
if(WIN32)
    if(AEROS_CPU_ONLY)
        set(CPACK_PACKAGE_FILE_NAME "Aeros-Engine-Portable-${AEROS_ARCH}-v${APP_VERSION}-CPU")
    else()
        set(CPACK_PACKAGE_FILE_NAME "Aeros-Engine-Portable-${AEROS_ARCH}-v${APP_VERSION}-Vulkan-OpenGL")
    endif()
else()
    set(CPACK_PACKAGE_FILE_NAME "Aeros-Engine-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-v${APP_VERSION}-Vulkan-OpenGL")
endif()
include(CPack)

# Summary
message(STATUS "=== Aeros Engine v${APP_VERSION} Build Summary ===")
message(STATUS "  Arch: ${AEROS_ARCH}")
message(STATUS "  CPU_ONLY: ${AEROS_CPU_ONLY}")
message(STATUS "  Vulkan: ${VULKAN_FOUND} (enable: ${AEROS_ENABLE_VULKAN})")
message(STATUS "  OpenGL: ${AEROS_ENABLE_OPENGL}")
message(STATUS "  GLFW: ${GLFW3_FOUND}")
message(STATUS "  OpenMP: ${OpenMP_CXX_FOUND}")
message(STATUS "  System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Download deps: ${AEROS_DOWNLOAD_DEPS}")
message(STATUS "==========================================")
