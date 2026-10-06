// Minimal Android Main - installable APK without full engine
// Shows colored screen based on flavor, touch to change color
// For SD662 Adreno 610 - will install and run!

#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <cmath>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "AerosEngine", __VA_ARGS__))

struct AppState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int width = 0;
    int height = 0;
    bool initialized = false;
    float touchX = 0.5f;
    float touchY = 0.5f;
    float time = 0;
    int flavor = 0; // 0=potato 1=lite 2=balanced 3=high 4=full
};

static AppState g_state;

// Simple EGL init
bool initEGL(android_app* app) {
    g_state.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_state.display == EGL_NO_DISPLAY) return false;
    if (!eglInitialize(g_state.display, nullptr, nullptr)) return false;

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };
    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(g_state.display, attribs, &config, 1, &numConfigs) || numConfigs == 0) {
        // Fallback to ES2
        const EGLint attribs2[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5,
            EGL_DEPTH_SIZE, 16,
            EGL_NONE
        };
        if (!eglChooseConfig(g_state.display, attribs2, &config, 1, &numConfigs)) return false;
    }

    const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    g_state.context = eglCreateContext(g_state.display, config, EGL_NO_CONTEXT, contextAttribs);
    if (g_state.context == EGL_NO_CONTEXT) {
        const EGLint contextAttribs2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        g_state.context = eglCreateContext(g_state.display, config, EGL_NO_CONTEXT, contextAttribs2);
        if (g_state.context == EGL_NO_CONTEXT) return false;
    }

    g_state.surface = eglCreateWindowSurface(g_state.display, config, app->window, nullptr);
    if (g_state.surface == EGL_NO_SURFACE) return false;

    if (!eglMakeCurrent(g_state.display, g_state.surface, g_state.surface, g_state.context)) return false;

    eglQuerySurface(g_state.display, g_state.surface, EGL_WIDTH, &g_state.width);
    eglQuerySurface(g_state.display, g_state.surface, EGL_HEIGHT, &g_state.height);

    glViewport(0, 0, g_state.width, g_state.height);
    g_state.initialized = true;
    LOGI("EGL initialized %dx%d", g_state.width, g_state.height);
    return true;
}

void termEGL() {
    if (g_state.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_state.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_state.surface != EGL_NO_SURFACE) eglDestroySurface(g_state.display, g_state.surface);
        if (g_state.context != EGL_NO_CONTEXT) eglDestroyContext(g_state.display, g_state.context);
        eglTerminate(g_state.display);
    }
    g_state.display = EGL_NO_DISPLAY;
    g_state.surface = EGL_NO_SURFACE;
    g_state.context = EGL_NO_CONTEXT;
    g_state.initialized = false;
}

void drawFrame() {
    if (!g_state.initialized) return;

    g_state.time += 0.016f;

    // Color based on flavor + touch + time - for SD662 demo
    float r, g, b;
    // Balanced - your phone - blue/cyan theme
    float tx = g_state.touchX;
    float ty = g_state.touchY;
    
    // Animated gradient - shows GPU is working
    r = 0.1f + 0.3f * sinf(g_state.time * 0.5f + tx * 3.14f);
    g = 0.3f + 0.4f * sinf(g_state.time * 0.7f + ty * 3.14f);
    b = 0.6f + 0.4f * cosf(g_state.time * 0.3f);

    // Flavor tint
    if (g_state.flavor == 0) { // potato - brown
        r = 0.5f + tx*0.3f; g = 0.3f + ty*0.2f; b = 0.1f;
    } else if (g_state.flavor == 1) { // lite - green
        r = 0.2f; g = 0.5f + ty*0.3f; b = 0.2f;
    } else if (g_state.flavor == 2) { // balanced - your SD662 - blue
        r = 0.1f + tx*0.2f; g = 0.4f + ty*0.2f; b = 0.8f;
    } else if (g_state.flavor == 3) { // high - purple
        r = 0.5f + tx*0.2f; g = 0.2f; b = 0.7f + ty*0.2f;
    } else { // full - gold
        r = 0.8f; g = 0.6f + ty*0.2f; b = 0.1f;
    }

    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(g_state.display, g_state.surface);
}

static int32_t handleInput(android_app* app, AInputEvent* event) {
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_MOVE) {
            g_state.touchX = AMotionEvent_getX(event, 0) / (float)g_state.width;
            g_state.touchY = AMotionEvent_getY(event, 0) / (float)g_state.height;
        }
        return 1;
    }
    return 0;
}

static void handleCmd(android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) {
                initEGL(app);
                // Detect flavor from package name or set default to balanced (your phone!)
                g_state.flavor = 2; // balanced for SD662
            }
            break;
        case APP_CMD_TERM_WINDOW:
            termEGL();
            break;
        case APP_CMD_GAINED_FOCUS:
            break;
        case APP_CMD_LOST_FOCUS:
            break;
    }
}

void android_main(android_app* app) {
    app->onAppCmd = handleCmd;
    app->onInputEvent = handleInput;

    LOGI("Aeros Engine Minimal - SD662 Adreno 610 - Installable APK!");
    LOGI("Touch screen to change color, flavor=%d", g_state.flavor);

    while (true) {
        int events;
        android_poll_source* source;
        while (ALooper_pollAll(0, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) {
                termEGL();
                return;
            }
        }
        if (g_state.initialized) {
            drawFrame();
        }
    }
}
