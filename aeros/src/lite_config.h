#pragma once
// v1.21.0 Android+Lite — для слабых устройств i3-3xxx, Atom, Celeron, встроенная графика, ноутбучные GPU + Android SD662/Adreno 610
// Без CUDA, оптимизировано для Intel HD 4000 / GT 620M / AMD APU + Atom/Celeron 2GB RAM + SD662/Adreno 610

// Определяем Lite режим компиляции
#ifdef AEROS_LITE

// --- CPU оптимизации для i3-3xxx (Ivy Bridge, 2012) ---
#define LITE_MAX_THREADS 2
#define LITE_USE_SSE2 1
#define LITE_USE_AVX2 0
#define LITE_USE_AVX 0
#define LITE_OPT_LEVEL 1

// --- GPU оптимизации для Intel HD 4000 / GT 620M ---
#define LITE_OPENGL_VERSION_MAJOR 3
#define LITE_OPENGL_VERSION_MINOR 3
#define LITE_MSAA_SAMPLES 0
#define LITE_ENABLE_VULKAN 0
#define LITE_ENABLE_FSR 0
#define LITE_ENABLE_FG 0
#define LITE_ENABLE_PBR 0
#define LITE_SHADOWS 0

// --- Визуализация — сниженные настройки ---
#define LITE_DEFAULT_PARTICLES 1500
#define LITE_MAX_PARTICLES 5000
#define LITE_DEFAULT_STREAMLINES 8
#define LITE_MAX_STREAMLINES 16
#define LITE_DEFAULT_STREAMLINE_STEPS 80
#define LITE_MAX_STREAMLINE_STEPS 150
#define LITE_VOXEL_RESOLUTION 24
#define LITE_LBM_RESOLUTION 32
#define LITE_LBM_ENABLED_DEFAULT 0
#define LITE_LBM_STEPS_PER_FRAME 1

// --- Память ---
#define LITE_MAX_MEMORY_MB 512
#define LITE_TEXTURE_QUALITY 0
#define LITE_SMOKE_RESOLUTION 16
#define LITE_VORTEX_RESOLUTION 12

// --- CPU/GPU баланс ---
#define LITE_TARGET_FPS 30
#define LITE_VSYNC_DEFAULT 1
#define LITE_LIMIT_FPS_DEFAULT 1
#define LITE_MAX_FPS 30.0f

// --- Фичи ---
#define LITE_ENABLE_VOLUMETRIC 0
#define LITE_ENABLE_VORTEX_TUBES 0
#define LITE_ENABLE_FLIGHT 0
#define LITE_ENABLE_LIC 0
#define LITE_ENABLE_AEROACOUSTIC 0
#define LITE_ENABLE_SCHLIEREN 1
#define LITE_ENABLE_SHOCK 1
#define LITE_ENABLE_INTERESTING_DEFAULT 0

// --- Шейдеры ---
#define LITE_SIMPLE_SHADERS 1
#define LITE_MAX_LIGHTS 1

// --- Другое ---
#define LITE_ENABLE_CUDA 0
#define LITE_ENABLE_OPENMP_LIMIT 1
#define LITE_POWER_SAVING 1
#define LITE_BINARY_NAME "aeros-engine-lite"
#define LITE_WINDOW_WIDTH 1024
#define LITE_WINDOW_HEIGHT 600

#else

// Обычная версия
#define LITE_MAX_THREADS 0
#define LITE_DEFAULT_PARTICLES 15000
#define LITE_DEFAULT_STREAMLINES 24
#define LITE_DEFAULT_STREAMLINE_STEPS 300
#define LITE_VOXEL_RESOLUTION 48
#define LITE_BINARY_NAME "aeros-engine"
#define LITE_WINDOW_WIDTH 1280
#define LITE_WINDOW_HEIGHT 720


// --- Макросы Lite-пресета, доступные и в обычной сборке (кнопка "Apply Lite Defaults") ---
#define LITE_USE_SSE2 1
#define LITE_USE_AVX2 0
#define LITE_USE_AVX 0
#define LITE_OPT_LEVEL 1
#define LITE_OPENGL_VERSION_MAJOR 3
#define LITE_OPENGL_VERSION_MINOR 3
#define LITE_MSAA_SAMPLES 0
#define LITE_ENABLE_VULKAN 0
#define LITE_ENABLE_FSR 0
#define LITE_ENABLE_FG 0
#define LITE_ENABLE_PBR 0
#define LITE_SHADOWS 0
#define LITE_MAX_PARTICLES 5000
#define LITE_MAX_STREAMLINES 16
#define LITE_MAX_STREAMLINE_STEPS 150
#define LITE_LBM_RESOLUTION 32
#define LITE_LBM_ENABLED_DEFAULT 0
#define LITE_LBM_STEPS_PER_FRAME 1
#define LITE_MAX_MEMORY_MB 512
#define LITE_TEXTURE_QUALITY 0
#define LITE_SMOKE_RESOLUTION 16
#define LITE_VORTEX_RESOLUTION 12
#define LITE_TARGET_FPS 30
#define LITE_VSYNC_DEFAULT 1
#define LITE_LIMIT_FPS_DEFAULT 1
#define LITE_MAX_FPS 30.0f
#define LITE_ENABLE_VOLUMETRIC 0
#define LITE_ENABLE_VORTEX_TUBES 0
#define LITE_ENABLE_FLIGHT 0
#define LITE_ENABLE_LIC 0
#define LITE_ENABLE_AEROACOUSTIC 0
#define LITE_ENABLE_SCHLIEREN 1
#define LITE_ENABLE_SHOCK 1
#define LITE_ENABLE_INTERESTING_DEFAULT 0
#define LITE_SIMPLE_SHADERS 1
#define LITE_MAX_LIGHTS 1
#define LITE_ENABLE_CUDA 0
#define LITE_ENABLE_OPENMP_LIMIT 1
#define LITE_POWER_SAVING 1
#endif

// --- ULTRA-LITE для Atom/Celeron/2GB RAM (v1.20.1) ---
#define ULTRA_LITE_PARTICLES 500
#define ULTRA_LITE_MAX_PARTICLES 1000
#define ULTRA_LITE_STREAMLINES 4
#define ULTRA_LITE_STREAMLINE_STEPS 40
#define ULTRA_LITE_VOXEL_RES 16
#define ULTRA_LITE_LBM_RES 16
#define ULTRA_LITE_TARGET_FPS 20
#define ULTRA_LITE_WINDOW_W 800
#define ULTRA_LITE_WINDOW_H 450
#define ULTRA_LITE_MAX_THREADS 1
#define ULTRA_LITE_MAX_MEMORY_MB 256

// --- ANDROID BALANCED для SD662/Adreno 610 (NEW v1.21.0 для конкретного телефона юзера) ---
// Snapdragon 662: 4x Kryo 260 Gold (A73) @ 2.11GHz + 4x Kryo 260 Silver (A53) @ 1.8GHz, 11nm, 8 cores
// Adreno 610: ES 3.2, Vulkan 1.1, ~150 GFLOPS, ASTC, 720x1604 90Hz
// Оптимизировано: больше чем Lite, меньше чем Full, 60 FPS target с динамикой, 4 потока
#define ANDROID_SD662_PARTICLES 2500
#define ANDROID_SD662_MAX_PARTICLES 5000
#define ANDROID_SD662_STREAMLINES 12
#define ANDROID_SD662_MAX_STREAMLINES 20
#define ANDROID_SD662_STREAMLINE_STEPS 120
#define ANDROID_SD662_MAX_STEPS 200
#define ANDROID_SD662_VOXEL_RES 32
#define ANDROID_SD662_LBM_RES 24
#define ANDROID_SD662_TARGET_FPS 60
#define ANDROID_SD662_MAX_FPS 60.0f
#define ANDROID_SD662_BATTERY_FPS 45
#define ANDROID_SD662_WINDOW_W 720
#define ANDROID_SD662_WINDOW_H 1604
#define ANDROID_SD662_MAX_THREADS 4
#define ANDROID_SD662_MAX_MEMORY_MB 800
#define ANDROID_SD662_VRAM_MB 400

// --- Пресеты качества (v1.21.0 с Balanced для SD662) ---
enum class LiteQualityPreset {
    Potato = 0,   // Atom/Celeron 2GB RAM, HD 3000, Adreno 306 — минимум 300 particles 15 FPS
    Low = 1,      // i3-3xxx, HD 4000, GT 620M, 4GB, Adreno 405 — Lite 1500 particles 30 FPS
    Medium = 2,   // i5-4xxx, HD 4600, GT 740M, 8GB, Adreno 506 — 5000 particles 45 FPS
    Balanced = 3, // SD662/Adreno 610, 720x1604 90Hz, 4-6GB — 2500 particles 60 FPS (NEW для юзера)
    High = 4,     // i5-8xxx, GTX 1050, Adreno 640 — 8000 particles 60 FPS
    Full = 5      // Современный ПК GTX 1060+ / SD 8 Gen 2 — 15000 particles 60 FPS
};

// Функции
bool isLiteMode();
bool isUltraLiteMode();
void applyLiteDefaults();
void applyUltraLiteDefaults();
void applyLiteOptimizations();
const char* getLiteInfoString();
int getLiteMaxThreads();
bool isLiteGPU();
void printLiteSystemInfo();

// v1.20.1
LiteQualityPreset detectHardwarePreset();
void applyPreset(LiteQualityPreset preset);
bool detectAndApplyLiteIfNeeded();
bool isBatteryPower();
bool isLowMemorySystem();
bool isWeakGPU(const char* glVendor, const char* glRenderer);
const char* getPresetName(LiteQualityPreset p);
const char* getPresetDescription(LiteQualityPreset p);
void saveLiteConfig(const char* path = "aeros-lite.ini");
bool loadLiteConfig(const char* path = "aeros-lite.ini");
void applyDynamicQualityScaling(float currentFPS);
float getEstimatedVRAMUsageMB();
float getEstimatedRAMUsageMB();

// v1.21.0 для SD662/Adreno 610
bool isAdreno610GPU(const char* glVendor, const char* glRenderer);
bool isSnapdragon662();
bool is90HzDisplay();
void applySD662Defaults(); // Специально для твоего телефона
void applyBalancedDefaults();
LiteQualityPreset detectAndroidPreset(); // Детект Android с учетом Adreno 610
const char* getGPUInfoString();
const char* getSoCInfoString();

// Глобальные
extern bool g_autoQualityScaling;
extern bool g_batterySaver;
extern float g_currentFPSAverage;
extern LiteQualityPreset g_currentPreset;
extern bool g_isSD662Device;
extern bool g_isAdreno610;
extern bool g_is90Hz;
