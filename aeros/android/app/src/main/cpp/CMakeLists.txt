# Aeros Engine Android CMakeLists.txt v1.20.1 — самая оптимизированная для слабых телефонов
# Для старых телефонов: Adreno 306, Mali-400, 1-2GB RAM, Android 5.0+
cmake_minimum_required(VERSION 3.22.1)

project("aeros-engine")

# Версия из ../../../VERSION или дефолт
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../VERSION")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../../../VERSION" APP_VERSION_RAW)
    string(STRIP "${APP_VERSION_RAW}" APP_VERSION)
else()
    set(APP_VERSION "1.20.1-android")
endif()

message(STATUS "Aeros Engine Android v${APP_VERSION} Lite — для слабых телефонов")

# Опции
option(AEROS_ANDROID "Android build" ON)
option(AEROS_LITE "Lite mode" ON)
option(ANDROID_LITE "Android Lite" ON)
option(ANDROID_POTATO "Potato mode for weakest phones" OFF)
option(CPU_ONLY "CPU only no CUDA" ON)

# Android специфичные
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Флаги для слабых телефонов — самые оптимизированные
# O1 (не O3) — меньше бинарь, меньше нагрев, быстрее запуск на старых телефонах
# -Os — оптимизация размера для Potato
# -ffast-math — быстрее
# -fopenmp — если есть, но лимит 1-2 потока
# -DANDROID — флаг Android
# -DAEROS_LITE -DULTRA_LITE -DANDROID_LITE — Lite режимы
# -DPOTATO_MODE — самый слабый
# -DCPU_ONLY — без CUDA
# -DGL_ES — OpenGL ES вместо OpenGL
if(ANDROID_POTATO)
    message(STATUS "POTATO MODE — for weakest phones: 1GB RAM, Adreno 306, Android 5.0, 300 particles, 15 FPS")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O1 -Os -ffast-math -fPIC -DANDROID -DAEROS_LITE -DULTRA_LITE -DANDROID_LITE -DANDROID_POTATO -DPOTATO_MODE -DCPU_ONLY -DGL_ES -DGLFW_INCLUDE_NONE -DVERSION=\"\\\"${APP_VERSION} Potato\\\"\"")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O1 -Os -ffast-math -fPIC -DANDROID -DGL_ES")
else()
    message(STATUS "LITE MODE — for weak phones: 2-4GB RAM, Adreno 405+, Android 6.0+, 500-1500 particles, 20-30 FPS")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O1 -ffast-math -fPIC -DANDROID -DAEROS_LITE -DANDROID_LITE -DCPU_ONLY -DGL_ES -DGLFW_INCLUDE_NONE -DVERSION=\"\\\"${APP_VERSION} Lite\\\"\"")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O1 -ffast-math -fPIC -DANDROID -DGL_ES")
endif()

# NEON для ARM — быстрее на старых телефонах
if(ANDROID_ARM_NEON)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfpu=neon")
endif()

# OpenMP для Android (если доступен)
find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fopenmp")
endif()

# Пути к исходникам — относительно aeros/
set(AEROS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")
set(SRC_DIR "${AEROS_ROOT}/src")
set(LIBS_DIR "${AEROS_ROOT}/../libs")

message(STATUS "AEROS_ROOT: ${AEROS_ROOT}")
message(STATUS "SRC_DIR: ${SRC_DIR}")

# Android native app glue — из NDK
if(DEFINED ANDROID_NDK)
    set(NATIVE_APP_GLUE_DIR "${ANDROID_NDK}/sources/android/native_app_glue")
    if(EXISTS "${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c")
        set(NATIVE_APP_GLUE_SRC "${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c")
        include_directories(${NATIVE_APP_GLUE_DIR})
        message(STATUS "Found native_app_glue: ${NATIVE_APP_GLUE_SRC}")
    else()
        message(WARNING "native_app_glue.c not found in NDK")
        set(NATIVE_APP_GLUE_SRC "")
    endif()
else()
    set(NATIVE_APP_GLUE_SRC "")
endif()

# Исходники — все кроме main.cpp (используем android_main.cpp)
set(SOURCES
    ${SRC_DIR}/globals.cpp
    ${SRC_DIR}/gl_utils.cpp
    ${SRC_DIR}/stl_loader.cpp
    ${SRC_DIR}/voxel_grid.cpp
    ${SRC_DIR}/flow_field.cpp
    ${SRC_DIR}/particles.cpp
    ${SRC_DIR}/streamlines.cpp
    ${SRC_DIR}/forces.cpp
    ${SRC_DIR}/model.cpp
    ${SRC_DIR}/ui.cpp
    ${SRC_DIR}/atmosphere.cpp
    ${SRC_DIR}/test_mode.cpp
    ${SRC_DIR}/lbm.cpp
    ${SRC_DIR}/lang.cpp
    ${SRC_DIR}/fsr.cpp
    ${SRC_DIR}/framegen.cpp
    ${SRC_DIR}/interesting.cpp
    ${SRC_DIR}/vulkan_renderer.cpp
    ${SRC_DIR}/lite_config.cpp
    ${SRC_DIR}/benchmark.cpp
    ${SRC_DIR}/android_config.cpp
    ${SRC_DIR}/cuda_stub.cpp
    ${SRC_DIR}/glad.c
    ${SRC_DIR}/imgui/imgui.cpp
    ${SRC_DIR}/imgui/imgui_draw.cpp
    ${SRC_DIR}/imgui/imgui_tables.cpp
    ${SRC_DIR}/imgui/imgui_widgets.cpp
    ${SRC_DIR}/imgui/imgui_impl_opengl3.cpp
    # Android специфичные
    ${CMAKE_CURRENT_SOURCE_DIR}/android_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/android_bridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/android_input.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/android_file.cpp
)

if(NATIVE_APP_GLUE_SRC)
    list(APPEND SOURCES ${NATIVE_APP_GLUE_SRC})
endif()

# Заголовки
include_directories(
    ${SRC_DIR}
    ${SRC_DIR}/imgui
    ${LIBS_DIR}/glm
    ${LIBS_DIR}/glad/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Создаем shared library — libaeros-engine.so
add_library(aeros-engine SHARED ${SOURCES})

# Линкуем Android библиотеки — минимально для маленького APK
target_link_libraries(aeros-engine
    android
    log
    EGL
    GLESv3
    m
    z
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(aeros-engine OpenMP::OpenMP_CXX)
endif()

# Для старых телефонов — 16KB page size совместимость (Android 15+)
# target_link_options(aeros-engine PRIVATE "-Wl,-z,max-page-size=16384")

# Оптимизации линковщика для размера
target_link_options(aeros-engine PRIVATE
    "-Wl,--gc-sections"
    "-Wl,--icf=all"
    "-Wl,-O1"
)

message(STATUS "Android Lite build configured — optimized for weak phones")
message(STATUS "  Version: ${APP_VERSION}")
message(STATUS "  Potato: ${ANDROID_POTATO}")
message(STATUS "  ABI: ${ANDROID_ABI}")
message(STATUS "  Platform: ${ANDROID_PLATFORM}")
message(STATUS "  NDK: ${ANDROID_NDK}")
