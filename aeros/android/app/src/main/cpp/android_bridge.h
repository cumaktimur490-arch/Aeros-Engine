#pragma once
// Android bridge — функции для связи Java/C++ и Android специфичные

#include <android/asset_manager.h>
#include <android_native_app_glue.h>

// Инициализация
void androidBridgeInit(android_app* app);
const char* androidGetDeviceInfo();
bool androidIsLowRamDevice();
int androidGetApiLevel();
