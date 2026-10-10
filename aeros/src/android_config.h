#pragma once
// v1.20.1 Android — конфигурация для самых слабых телефонов
// Adreno 306, Mali-400, PowerVR SGX, 1-2GB RAM, Android 5.0+

#ifdef ANDROID

// --- Android Potato — для самых слабых телефонов 1GB RAM Adreno 306 ---
// Adreno 306: OpenGL ES 3.0 (но часто только 2.0), 24 ALUs, 400MHz, 2014
// Mali-400: ES 2.0 only, 1-4 cores, 2010
// PowerVR SGX 544: ES 2.0, 2012
#define ANDROID_POTATO_PARTICLES 300
#define ANDROID_POTATO_MAX_PARTICLES 500
#define ANDROID_POTATO_STREAMLINES 3
#define ANDROID_POTATO_MAX_STREAMLINES 6
#define ANDROID_POTATO_STREAMLINE_STEPS 30
#define ANDROID_POTATO_MAX_STEPS 60
#define ANDROID_POTATO_VOXEL_RES 12
#define ANDROID_POTATO_LBM_RES 12
#define ANDROID_POTATO_TARGET_FPS 15
#define ANDROID_POTATO_MAX_FPS 15.0f
#define ANDROID_POTATO_WINDOW_W 800
#define ANDROID_POTATO_WINDOW_H 480
#define ANDROID_POTATO_MAX_THREADS 1
#define ANDROID_POTATO_MAX_MEMORY_MB 150

// --- Android Lite — для слабых телефонов 2-4GB RAM Adreno 405+ ---
// Adreno 405: ES 3.1, 48 ALUs, 2015, 2GB RAM телефоны
// Mali-T720: ES 3.1, 2014
#define ANDROID_LITE_PARTICLES 800
#define ANDROID_LITE_MAX_PARTICLES 1500
#define ANDROID_LITE_STREAMLINES 6
#define ANDROID_LITE_MAX_STREAMLINES 12
#define ANDROID_LITE_STREAMLINE_STEPS 60
#define ANDROID_LITE_MAX_STEPS 120
#define ANDROID_LITE_VOXEL_RES 20
#define ANDROID_LITE_LBM_RES 20
#define ANDROID_LITE_TARGET_FPS 25
#define ANDROID_LITE_MAX_FPS 25.0f
#define ANDROID_LITE_WINDOW_W 1280
#define ANDROID_LITE_WINDOW_H 720
#define ANDROID_LITE_MAX_THREADS 2
#define ANDROID_LITE_MAX_MEMORY_MB 300

// --- Android Medium — для средних телефонов 4GB+ ---
#define ANDROID_MEDIUM_PARTICLES 2000
#define ANDROID_MEDIUM_STREAMLINES 10
#define ANDROID_MEDIUM_STEPS 100
#define ANDROID_MEDIUM_VOXEL 28
#define ANDROID_MEDIUM_FPS 30

// --- Общие Android оптимизации ---
#define ANDROID_MIN_SDK 21 // Android 5.0 Lollipop — для старых телефонов 2014
#define ANDROID_TARGET_SDK 34
#define ANDROID_GL_ES_VERSION 3 // 3.0 для Lite, 2.0 для Potato
#define ANDROID_TOUCH_SLOP 8 // минимум движения для touch
#define ANDROID_MAX_TOUCH_POINTS 2 // только 2 пальца для экономии
#define ANDROID_BATTERY_SAVER_ALWAYS 1 // всегда экономия батареи на Android
#define ANDROID_AUTO_QUALITY_ALWAYS 1 // всегда авто качество
#define ANDROID_VSYNC_ON 1 // VSync вкл для экономии
#define ANDROID_MSAA 0 // без MSAA
#define ANDROID_SHADOWS 0
#define ANDROID_PBR 0
#define ANDROID_FSR 0
#define ANDROID_FG 0
#define ANDROID_VOLUMETRIC 0
#define ANDROID_VORTEX 0
#define ANDROID_LIC 0
#define ANDROID_ACOUSTIC 0
#define ANDROID_FLIGHT 0
#define ANDROID_SCHLIEREN 1 // легкая
#define ANDROID_SHOCK 1

#else

// Не Android — дефолты
#define ANDROID_POTATO_PARTICLES 500
#define ANDROID_LITE_PARTICLES 1500

#endif

// Функции
bool isAndroidLowRamDevice();
bool isAndroidPotatoDevice();
void applyAndroidDefaults(bool isPotato);
void applyAndroidPotatoDefaults();
void applyAndroidLiteDefaults();
const char* getAndroidDeviceInfo();
int getAndroidApiLevel();
float getAndroidBatteryLevel();
bool getAndroidIsCharging();
