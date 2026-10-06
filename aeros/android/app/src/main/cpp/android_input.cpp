#include "android_input.h"
#include <android/log.h>
#include <cmath>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "AerosEngine", __VA_ARGS__))

float g_touchX = 0, g_touchY = 0;
bool g_touchDown = false;
float g_touchDeltaX = 0, g_touchDeltaY = 0;
float g_pinchScale = 1.0f;
bool g_isPinching = false;

static int g_screenWidth = 0, g_screenHeight = 0;
static float g_lastTouchX = 0, g_lastTouchY = 0;
static float g_lastPinchDist = 0;

void androidInputInit(int width, int height) {
    g_screenWidth = width;
    g_screenHeight = height;
    g_touchX = width * 0.5f;
    g_touchY = height * 0.5f;
    LOGI("Android Input Init: %dx%d", width, height);
}

int32_t androidInputHandle(android_app* app, AInputEvent* event) {
    int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event);
        int32_t actionMasked = action & AMOTION_EVENT_ACTION_MASK;
        size_t pointerCount = AMotionEvent_getPointerCount(event);

        if (pointerCount == 1) {
            // Один палец — вращение модели
            float x = AMotionEvent_getX(event, 0);
            float y = AMotionEvent_getY(event, 0);

            if (actionMasked == AMOTION_EVENT_ACTION_DOWN) {
                g_touchDown = true;
                g_touchX = x;
                g_touchY = y;
                g_lastTouchX = x;
                g_lastTouchY = y;
                g_touchDeltaX = 0;
                g_touchDeltaY = 0;
                g_isPinching = false;
                return 1;
            } else if (actionMasked == AMOTION_EVENT_ACTION_MOVE) {
                if (g_touchDown) {
                    g_touchDeltaX = x - g_lastTouchX;
                    g_touchDeltaY = y - g_lastTouchY;
                    g_touchX = x;
                    g_touchY = y;
                    g_lastTouchX = x;
                    g_lastTouchY = y;
                }
                return 1;
            } else if (actionMasked == AMOTION_EVENT_ACTION_UP || actionMasked == AMOTION_EVENT_ACTION_CANCEL) {
                g_touchDown = false;
                g_touchDeltaX = 0;
                g_touchDeltaY = 0;
                return 1;
            }
        } else if (pointerCount == 2) {
            // Два пальца — pinch zoom
            float x0 = AMotionEvent_getX(event, 0);
            float y0 = AMotionEvent_getY(event, 0);
            float x1 = AMotionEvent_getX(event, 1);
            float y1 = AMotionEvent_getY(event, 1);

            float dx = x1 - x0;
            float dy = y1 - y0;
            float dist = sqrtf(dx*dx + dy*dy);

            if (actionMasked == AMOTION_EVENT_ACTION_POINTER_DOWN) {
                g_isPinching = true;
                g_lastPinchDist = dist;
                g_pinchScale = 1.0f;
                return 1;
            } else if (actionMasked == AMOTION_EVENT_ACTION_MOVE && g_isPinching) {
                if (g_lastPinchDist > 0) {
                    g_pinchScale = dist / g_lastPinchDist;
                    // Ограничиваем
                    if (g_pinchScale < 0.5f) g_pinchScale = 0.5f;
                    if (g_pinchScale > 2.0f) g_pinchScale = 2.0f;
                }
                return 1;
            } else if (actionMasked == AMOTION_EVENT_ACTION_POINTER_UP || actionMasked == AMOTION_EVENT_ACTION_UP) {
                g_isPinching = false;
                g_pinchScale = 1.0f;
                g_lastPinchDist = 0;
                return 1;
            }
        }

        return 1;
    } else if (type == AINPUT_EVENT_TYPE_KEY) {
        // Клавиши — для отладки с клавиатурой
        int32_t keyCode = AKeyEvent_getKeyCode(event);
        int32_t action = AKeyEvent_getAction(event);
        if (action == AKEY_EVENT_ACTION_DOWN) {
            if (keyCode == AKEYCODE_BACK) {
                // Back — выход
                // Не обрабатываем, пусть система закроет
                return 0;
            }
        }
        return 1;
    }

    return 0;
}
