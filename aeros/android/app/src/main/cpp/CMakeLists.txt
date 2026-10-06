cmake_minimum_required(VERSION 3.22.1)
project("aeros-engine")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -fPIC -DANDROID")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -fPIC -DANDROID")

if(DEFINED ANDROID_NDK)
    set(NATIVE_APP_GLUE_DIR "${ANDROID_NDK}/sources/android/native_app_glue")
    if(EXISTS "${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c")
        set(NATIVE_APP_GLUE_SRC "${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c")
        include_directories(${NATIVE_APP_GLUE_DIR})
    endif()
endif()

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/android_main.cpp
)

if(NATIVE_APP_GLUE_SRC)
    list(APPEND SOURCES ${NATIVE_APP_GLUE_SRC})
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_library(aeros-engine SHARED ${SOURCES})
target_link_libraries(aeros-engine android log EGL GLESv3 m z)
message(STATUS "Minimal installable APK build")
