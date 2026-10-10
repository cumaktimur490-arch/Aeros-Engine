// Placeholder — real file is in NDK sources
// This file is provided by NDK — we don't need to include it, but for CMake to find
// In real build, NDK provides android_native_app_glue.c via ${ANDROID_NDK}/sources/android/native_app_glue/
// We will add it via CMake include

// For standalone build without NDK, we provide minimal stub
// Actual implementation is in NDK

#ifdef ANDROID_STUB
#include <android_native_app_glue.h>
#endif
