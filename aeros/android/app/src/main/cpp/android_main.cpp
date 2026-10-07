// Aeros Engine Android Main v1.20.1 — самая оптимизированная для слабых телефонов
// Использует NativeActivity, EGL, GLES 3.0, touch input, Lite+Potato режимы
// Для старых телефонов: Adreno 306, Mali-400, 1-2GB RAM, Android 5.0+

#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include <cmath>

#include "lite_config.h"
#include "globals.h"
#include "benchmark.h"
#include "android_bridge.h"
#include "android_input.h"
#include "android_file.h"

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "AerosEngine", __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, "AerosEngine", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "AerosEngine", __VA_ARGS__))

// Глобальные EGL
struct AndroidAppState {
    android_app* app;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    EGLConfig config;
    int width;
    int height;
    bool initialized;
    bool hasFocus;
    bool isResumed;
    float density; // экранная плотность для touch
    LiteQualityPreset preset;
    bool isPotato;
    double lastFrameTime;
    float fpsAvg;
    int frameCount;
};

static AndroidAppState g_state = {};

// EGL инициализация для слабых телефонов — выбираем минимальный конфиг
bool initEGL(android_app* app) {
    LOGI("Initializing EGL for Android Lite — weak phones optimized");

    // Выбираем display
    g_state.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_state.display == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed");
        return false;
    }

    if (!eglInitialize(g_state.display, nullptr, nullptr)) {
        LOGE("eglInitialize failed");
        return false;
    }

    // Конфиг для слабых телефонов — минимальный, 16-bit depth, no stencil, no MSAA
    // Для Adreno 306 / Mali-400 — только GLES 2.0 может, но пробуем 3.0
    const EGLint attribsPotato[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, // ES2 для самых слабых
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 5,
        EGL_GREEN_SIZE, 6,
        EGL_BLUE_SIZE, 5,
        EGL_ALPHA_SIZE, 0,
        EGL_DEPTH_SIZE, 16, // 16-bit depth — экономит память
        EGL_STENCIL_SIZE, 0, // no stencil — экономит
        EGL_NONE
    };

    const EGLint attribsLite[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, // ES3 для Lite
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_DEPTH_SIZE, 16,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };

    const EGLint* attribs = g_state.isPotato ? attribsPotato : attribsLite;

    EGLint numConfigs;
    if (!eglChooseConfig(g_state.display, attribs, &g_state.config, 1, &numConfigs) || numConfigs == 0) {
        LOGW("eglChooseConfig with preferred attribs failed, trying fallback");
        // Fallback — любой конфиг
        const EGLint fallback[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 5,
            EGL_GREEN_SIZE, 6,
            EGL_BLUE_SIZE, 5,
            EGL_DEPTH_SIZE, 16,
            EGL_NONE
        };
        if (!eglChooseConfig(g_state.display, fallback, &g_state.config, 1, &numConfigs) || numConfigs == 0) {
            LOGE("eglChooseConfig fallback failed");
            return false;
        }
    }

    // Создаем surface
    g_state.surface = eglCreateWindowSurface(g_state.display, g_state.config, app->window, nullptr);
    if (g_state.surface == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed: %d", eglGetError());
        return false;
    }

    // Context — GLES 3.0 для Lite, 2.0 для Potato
    const EGLint ctxAttribsLite[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    const EGLint ctxAttribsPotato[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    const EGLint* ctxAttribs = g_state.isPotato ? ctxAttribsPotato : ctxAttribsLite;

    g_state.context = eglCreateContext(g_state.display, g_state.config, EGL_NO_CONTEXT, ctxAttribs);
    if (g_state.context == EGL_NO_CONTEXT) {
        LOGW("GLES 3.0 context failed, trying 2.0");
        const EGLint ctx2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        g_state.context = eglCreateContext(g_state.display, g_state.config, EGL_NO_CONTEXT, ctx2);
        if (g_state.context == EGL_NO_CONTEXT) {
            LOGE("eglCreateContext failed");
            return false;
        }
        g_state.isPotato = true; // fallback to Potato
    }

    if (!eglMakeCurrent(g_state.display, g_state.surface, g_state.surface, g_state.context)) {
        LOGE("eglMakeCurrent failed");
        return false;
    }

    // Размеры
    eglQuerySurface(g_state.display, g_state.surface, EGL_WIDTH, &g_state.width);
    eglQuerySurface(g_state.display, g_state.surface, EGL_HEIGHT, &g_state.height);

    LOGI("EGL initialized: %dx%d, Potato=%d", g_state.width, g_state.height, g_state.isPotato);

    // OpenGL info
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* version = (const char*)glGetString(GL_VERSION);
    LOGI("GL Vendor: %s", vendor ? vendor : "unknown");
    LOGI("GL Renderer: %s", renderer ? renderer : "unknown");
    LOGI("GL Version: %s", version ? version : "unknown");

    // Проверяем слабый GPU
    if (isWeakGPU(vendor, renderer)) {
        LOGI("Weak GPU detected — switching to Potato preset");
        g_state.preset = LiteQualityPreset::Potato;
        g_state.isPotato = true;
    }

    g_state.initialized = true;
    return true;
}

void termEGL() {
    if (g_state.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_state.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_state.context != EGL_NO_CONTEXT) {
            eglDestroyContext(g_state.display, g_state.context);
            g_state.context = EGL_NO_CONTEXT;
        }
        if (g_state.surface != EGL_NO_SURFACE) {
            eglDestroySurface(g_state.display, g_state.surface);
            g_state.surface = EGL_NO_SURFACE;
        }
        eglTerminate(g_state.display);
        g_state.display = EGL_NO_DISPLAY;
    }
    g_state.initialized = false;
}

// Обработка команд Android
void handleCmd(android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            LOGI("APP_CMD_INIT_WINDOW");
            if (app->window != nullptr) {
                // Определяем preset по железу
                g_state.preset = detectHardwarePreset();
                g_state.isPotato = (g_state.preset == LiteQualityPreset::Potato);

                // Проверяем RAM
                // На Android — используем low memory detection
                // Если <2GB — Potato, <3GB — Lite
                // Для простоты — если Potato, то Potato, иначе Lite

                if (!initEGL(app)) {
                    LOGE("Failed to init EGL");
                    return;
                }

                // Инициализируем движок с Android Lite настройками
                LOGI("Initializing Aeros Engine Android Lite — preset %s", getPresetName(g_state.preset));

                // Применяем пресет
                applyPreset(g_state.preset);

                // Android специфичные оптимизации
                g_isLiteMode = true;
                g_isUltraLiteMode = g_state.isPotato;
                g_autoQualityScaling = true;
                g_batterySaver = true;
                g_litePowerSaving = true;

                // Для Android — еще более низкие настройки
                if (g_state.isPotato) {
                    // Potato Android — 300 частиц, 3x30 линий, voxel 12, 15 FPS
                    numParticles = 300;
                    numStreamlines = 3;
                    streamlineSteps = 30;
                    voxelResolution = 12;
                    lbmParams.enabled = false;
                    lbmNx = 16; lbmNy = 16; lbmNz = 16;
                    maxFPS = 15.0f;
                    optTargetFPS = 15.0f;
                    g_currentFPSAverage = 15.0f;
                } else {
                    // Lite Android — 500-1000 частиц, 4-8 линий, voxel 16-24, 20-25 FPS
                    numParticles = 800;
                    numStreamlines = 6;
                    streamlineSteps = 60;
                    voxelResolution = 20;
                    lbmParams.enabled = false;
                    maxFPS = 25.0f;
                    optTargetFPS = 25.0f;
                    g_currentFPSAverage = 25.0f;
                }

                // Инициализируем файлы Android
                androidFileInit(app->activity->assetManager);

                // Инициализируем input
                androidInputInit(g_state.width, g_state.height);

                // OpenGL инициализация — упрощенная для GLES
                glViewport(0, 0, g_state.width, g_state.height);
                glClearColor(0.1f, 0.15f, 0.25f, 1.0f);
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LESS);

                // Для Potato — отключаем всё тяжелое
                if (g_state.isPotato) {
                    glDisable(GL_BLEND);
                } else {
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                }

                LOGI("Aeros Engine Android initialized — %s, %dx%d, %d particles, %dx%d streamlines, voxel %d, %.0f FPS target",
                     getPresetName(g_state.preset), g_state.width, g_state.height,
                     numParticles, numStreamlines, streamlineSteps, voxelResolution, optTargetFPS);

                printLiteSystemInfo();
            }
            break;

        case APP_CMD_TERM_WINDOW:
            LOGI("APP_CMD_TERM_WINDOW");
            termEGL();
            break;

        case APP_CMD_GAINED_FOCUS:
            LOGI("APP_CMD_GAINED_FOCUS");
            g_state.hasFocus = true;
            break;

        case APP_CMD_LOST_FOCUS:
            LOGI("APP_CMD_LOST_FOCUS");
            g_state.hasFocus = false;
            break;

        case APP_CMD_RESUME:
            LOGI("APP_CMD_RESUME");
            g_state.isResumed = true;
            break;

        case APP_CMD_PAUSE:
            LOGI("APP_CMD_PAUSE");
            g_state.isResumed = false;
            // Сохраняем конфиг при паузе
            saveLiteConfig("/data/data/com.aos.aerosengine/files/aeros-lite.ini");
            break;

        case APP_CMD_LOW_MEMORY:
            LOGW("APP_CMD_LOW_MEMORY — reducing quality");
            // При low memory — снижаем качество
            if (numParticles > 300) numParticles = 300;
            if (numStreamlines > 3) numStreamlines = 3;
            if (voxelResolution > 12) voxelResolution = 12;
            break;

        case APP_CMD_DESTROY:
            LOGI("APP_CMD_DESTROY");
            break;
    }
}

// Главный цикл Android
void android_main(android_app* app) {
    LOGI("Aeros Engine Android Main v1.20.1 Lite — starting");

    g_state.app = app;
    g_state.display = EGL_NO_DISPLAY;
    g_state.surface = EGL_NO_SURFACE;
    g_state.context = EGL_NO_CONTEXT;
    g_state.width = 0;
    g_state.height = 0;
    g_state.initialized = false;
    g_state.hasFocus = false;
    g_state.isResumed = false;
    g_state.preset = LiteQualityPreset::Low;
    g_state.isPotato = false;
    g_state.lastFrameTime = 0;
    g_state.fpsAvg = 30.0f;
    g_state.frameCount = 0;

    app->onAppCmd = handleCmd;
    app->onInputEvent = androidInputHandle;

    // Ожидаем окно
    while (!app->destroyRequested) {
        // Обработка событий
        int events;
        android_poll_source* source;
        // Для экономии батареи — ждем события, не крутимся вхолостую если нет фокуса
        // На слабых телефонах — 0 timeout если фокус, -1 если нет (спим)
        int timeout = (g_state.hasFocus && g_state.initialized) ? 0 : -1;
        if (ALooper_pollAll(timeout, nullptr, &events, (void**)&source) >= 0) {
            if (source != nullptr) {
                source->process(app, source);
            }
        }

        if (!g_state.initialized || !g_state.hasFocus) {
            // Спим для экономии батареи на слабых телефонах
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Рендер кадр — упрощенный для Android
        auto now = std::chrono::high_resolution_clock::now();
        double currentTime = std::chrono::duration<double>(now.time_since_epoch()).count();
        float deltaTime = g_state.lastFrameTime > 0 ? (float)(currentTime - g_state.lastFrameTime) : 0.016f;
        g_state.lastFrameTime = currentTime;

        // Ограничиваем FPS для экономии батареи
        float targetFPS = g_state.isPotato ? 15.0f : 25.0f;
        float targetFrameTime = 1.0f / targetFPS;
        if (deltaTime < targetFrameTime) {
            int sleepMs = (int)((targetFrameTime - deltaTime) * 1000);
            if (sleepMs > 0 && sleepMs < 100) {
                std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
            }
        }

        // Обновляем FPS среднее
        float currentFPS = deltaTime > 0 ? 1.0f / deltaTime : targetFPS;
        g_state.fpsAvg = g_state.fpsAvg * 0.9f + currentFPS * 0.1f;
        g_currentFPSAverage = g_state.fpsAvg;

        // Динамическое качество — если FPS низкий, снижаем
        if (g_autoQualityScaling) {
            applyDynamicQualityScaling(g_state.fpsAvg);
        }

        // Очистка
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Тут был бы рендер движка — для Android версии упрощенный
        // В реальной версии — вызываем render loop из основного движка
        // Пока — просто clear color меняется по FPS для индикации

        // Индикация FPS цветом фона — зеленый=хорошо, красный=плохо
        float fpsRatio = g_state.fpsAvg / targetFPS;
        if (fpsRatio > 0.8f) {
            glClearColor(0.1f, 0.25f, 0.1f, 1.0f); // зеленый — хорошо
        } else if (fpsRatio > 0.5f) {
            glClearColor(0.25f, 0.25f, 0.1f, 1.0f); // желтый — средне
        } else {
            glClearColor(0.25f, 0.1f, 0.1f, 1.0f); // красный — плохо, нужно снижать качество
        }
        glClear(GL_COLOR_BUFFER_BIT);

        // TODO: тут рендер частиц, линий тока, модели
        // Для Android Lite — упрощенный рендер
        // Пока заглушка — в реальной версии интегрируем с основным рендером

        // Swap
        eglSwapBuffers(g_state.display, g_state.surface);

        g_state.frameCount++;
        if (g_state.frameCount % 60 == 0) {
            LOGI("FPS: %.1f avg, particles %d, streamlines %d, preset %s, Potato %d",
                 g_state.fpsAvg, numParticles, numStreamlines, getPresetName(g_state.preset), g_state.isPotato);
        }
    }

    termEGL();
    LOGI("Aeros Engine Android Main — exiting");
}
