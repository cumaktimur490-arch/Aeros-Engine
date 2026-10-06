#pragma once
// Android touch input — обработка касаний для слабых телефонов
// Оптимизировано: минимум аллокаций, без лишних вычислений

#include <android_native_app_glue.h>
#include <android/input.h>

void androidInputInit(int width, int height);
int32_t androidInputHandle(android_app* app, AInputEvent* event);

// Глобальные для движка — touch state
extern float g_touchX, g_touchY;
extern bool g_touchDown;
extern float g_touchDeltaX, g_touchDeltaY;
extern float g_pinchScale;
extern bool g_isPinching;
