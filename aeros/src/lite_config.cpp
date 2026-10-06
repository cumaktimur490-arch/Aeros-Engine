#include "lite_config.h"
#include "globals.h"
#include "lbm.h"

#include <iostream>
#include <thread>
#include <cmath>

#ifdef _OPENMP
#include <omp.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

bool isLiteMode() {
#ifdef AEROS_LITE
    return true;
#else
    return false;
#endif
}

int getLiteMaxThreads() {
#ifdef AEROS_LITE
    // Для i3-3xxx — 2 потока оптимально, чтобы не греть и оставить для системы
    return LITE_MAX_THREADS;
#else
    return 0; // auto
#endif
}

bool isLiteGPU() {
#ifdef AEROS_LITE
    return true;
#else
    // Авто-определение слабого GPU — можно проверить по GL_VENDOR/RENDERER
    // Для простоты — возвращаем false в обычной версии
    return false;
#endif
}

void applyLiteDefaults() {
#ifdef AEROS_LITE
    std::cout << "[Lite] Applying Lite defaults for i3-3xxx / HD 4000 / GT 620M..." << std::endl;

    // Частицы
    numParticles = LITE_DEFAULT_PARTICLES;
    if (numParticles > LITE_MAX_PARTICLES) numParticles = LITE_MAX_PARTICLES;
    particleSize = 2.5f; // чуть больше — лучше видно при малом количестве
    maxSpeedForColor = 5.0f;

    // Линии тока
    numStreamlines = LITE_DEFAULT_STREAMLINES;
    streamlineSteps = LITE_DEFAULT_STREAMLINE_STEPS;
    streamlineStepSize = 0.1f; // чуть больше шаг — быстрее
    streamlineAlpha = 0.9f;
    streamlineWidth = 2.0f; // толще — лучше видно

    // Воксели
    voxelResolution = LITE_VOXEL_RESOLUTION;
    useVoxelCollision = true;

    // LBM — выкл по умолчанию, 32 рес для Lite (глобальные lbmNx/Ny/Nz)
    lbmParams.enabled = LITE_LBM_ENABLED_DEFAULT;
    lbmParams.stepsPerFrame = LITE_LBM_STEPS_PER_FRAME;
    lbmNx = LITE_LBM_RESOLUTION;
    lbmNy = LITE_LBM_RESOLUTION;
    lbmNz = LITE_LBM_RESOLUTION;
    lbmParams.useTurbulence = false; // турбулентность — тяжело
    lbmParams.useGround = false;

    // Визуализация
    showModel = true;
    showParticles = true;
    showStreamlines = true;
    showPressure = false;
    showLiftDrag = true;
    showBoundingBox = false;
    showAxes = true;
    showGroundPlane = false;

    // Производительность
    vsyncEnabled = LITE_VSYNC_DEFAULT;
    limitFPS = LITE_LIMIT_FPS_DEFAULT;
    maxFPS = LITE_MAX_FPS;
    optFramePacing = true;
    optTargetFPS = LITE_TARGET_FPS;

    // Оптимизации — включаем все для слабых
    optFrustumCulling = true;
    optOcclusionCulling = false; // occlusion — тяжело для слабых
    optLOD = true;
    optEarlyZ = true;
    optDynamicParticles = true;
    optVRS = true; // VRS экономит fillrate
    optAsyncCompute = false; // async — тяжело
    optParticleLOD = 1; // half LOD
    optLODDistance = 3.0f;
    optMeshletCulling = true;

    // FSR/FG — выкл для встроек
    fsrEnabled = LITE_ENABLE_FSR;
    fgEnabled = LITE_ENABLE_FG;

    // Интересные фичи — выкл по умолчанию, кроме легких
    aeroShowVortexTubes = LITE_ENABLE_VORTEX_TUBES;
    aeroShowShockWaves = LITE_ENABLE_SHOCK;
    aeroShowLIC = LITE_ENABLE_LIC;
    aeroVolumetricEnabled = LITE_ENABLE_VOLUMETRIC;
    aeroSchlierenEnabled = LITE_ENABLE_SCHLIEREN;
    aeroFlightMode = LITE_ENABLE_FLIGHT;
    aeroShowAeroAcoustic = LITE_ENABLE_AEROACOUSTIC;
    aeroShowTemperature = false;

    aeroSmokeDensity = 0.5f;
    aeroSmokeOpacity = 0.5f;
    aeroSmokeInjectors = 2; // меньше
    aeroVortexTubeCount = 8; // меньше

    // Поток — проще
    flowSpeed = 2.0f;
    wakeStrength = 0.3f;
    wakeLength = 5.0f;
    strouhal = 0.2f;

    // Окно — меньше для 1366x768
    // display_w/h уже установлены, но для Lite — 1024x600
    // Это будет применено в main.cpp

    std::cout << "[Lite] Defaults applied: particles=" << numParticles << " streamlines=" << numStreamlines
              << "x" << streamlineSteps << " voxel=" << voxelResolution << " LBM=" << (lbmParams.enabled?"ON":"OFF") << std::endl;
#else
    // Обычная версия — ничего не делаем
#endif
}

void applyLiteOptimizations() {
#ifdef AEROS_LITE
    std::cout << "[Lite] Applying optimizations for weak GPUs..." << std::endl;

    // Ограничиваем OpenMP
#ifdef _OPENMP
    if (LITE_ENABLE_OPENMP_LIMIT) {
        int maxThreads = LITE_MAX_THREADS;
        if (maxThreads > 0) {
            omp_set_num_threads(maxThreads);
            std::cout << "[Lite] OpenMP limited to " << maxThreads << " threads (was " << omp_get_max_threads() << ")" << std::endl;
        } else {
            // Авто — 2 потока для i3
            int hwThreads = std::thread::hardware_concurrency();
            if (hwThreads > 4) hwThreads = 2; // для i3-3xxx и ноутов — 2
            else if (hwThreads > 2) hwThreads = 2;
            omp_set_num_threads(hwThreads);
            std::cout << "[Lite] OpenMP auto-limited to " << hwThreads << " threads (HW: " << std::thread::hardware_concurrency() << ")" << std::endl;
        }
    }
#endif

    // Устанавливаем низкий приоритет процесса на Windows — чтобы не грузить систему
#ifdef _WIN32
    if (LITE_POWER_SAVING) {
        SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
        std::cout << "[Lite] Process priority set to BELOW_NORMAL for power saving" << std::endl;
    }
#endif

    // Ограничиваем память — для ноутбуков с 4GB RAM
    // Просто информируем, реальный лимит — через настройки

    std::cout << "[Lite] Optimizations applied for Intel HD 4000 / GT 620M / i3-3xxx" << std::endl;
#endif
}

const char* getLiteInfoString() {
#ifdef AEROS_LITE
    return "Lite v1.20.0 — i3-3xxx / HD 4000 / GT 620M / No CUDA — Optimized for weak devices, 30 FPS target, low RAM, SSE2";
#else
    return "Full v1.19.0 — Vulkan+OpenGL, CUDA, FSR, FG, all features";
#endif
}

void printLiteSystemInfo() {
    std::cout << "=== Aeros Engine System Info ===" << std::endl;
#ifdef VERSION
    std::cout << "Version: " << VERSION << " " << getLiteInfoString() << std::endl;
#else
    std::cout << "Version: Lite " << getLiteInfoString() << std::endl;
#endif

    // CPU
    unsigned int hwThreads = std::thread::hardware_concurrency();
    std::cout << "CPU threads (HW): " << hwThreads << std::endl;
#ifdef _OPENMP
    std::cout << "OpenMP threads: " << omp_get_max_threads() << std::endl;
#endif

    // RAM
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    DWORDLONG totalPhys = memInfo.ullTotalPhys / (1024*1024);
    DWORDLONG availPhys = memInfo.ullAvailPhys / (1024*1024);
    std::cout << "RAM: " << totalPhys << " MB total, " << availPhys << " MB available" << std::endl;
    if (totalPhys < 4000) std::cout << "[Lite] Low RAM detected (<4GB) — using low memory mode" << std::endl;
#else
    long pages = sysconf(_SC_PHYS_PAGES);
    long pageSize = sysconf(_SC_PAGE_SIZE);
    long totalMem = pages * pageSize / (1024*1024);
    std::cout << "RAM: " << totalMem << " MB total" << std::endl;
    if (totalMem < 4000) std::cout << "[Lite] Low RAM detected (<4GB) — using low memory mode" << std::endl;
#endif

    // Lite specifics
#ifdef AEROS_LITE
    std::cout << "[Lite] Mode: ON" << std::endl;
    std::cout << "[Lite] Particles: " << LITE_DEFAULT_PARTICLES << " (max " << LITE_MAX_PARTICLES << ")" << std::endl;
    std::cout << "[Lite] Streamlines: " << LITE_DEFAULT_STREAMLINES << "x" << LITE_DEFAULT_STREAMLINE_STEPS << std::endl;
    std::cout << "[Lite] Voxel: " << LITE_VOXEL_RESOLUTION << "^3" << std::endl;
    std::cout << "[Lite] LBM: " << (LITE_LBM_ENABLED_DEFAULT?"ON":"OFF") << " res " << LITE_LBM_RESOLUTION << std::endl;
    std::cout << "[Lite] OpenGL: " << LITE_OPENGL_VERSION_MAJOR << "." << LITE_OPENGL_VERSION_MINOR << " MSAA: " << LITE_MSAA_SAMPLES << std::endl;
    std::cout << "[Lite] Vulkan: " << (LITE_ENABLE_VULKAN?"ON":"OFF (HD 4000 fallback)") << std::endl;
    std::cout << "[Lite] FSR: " << (LITE_ENABLE_FSR?"ON":"OFF") << " FG: " << (LITE_ENABLE_FG?"ON":"OFF") << std::endl;
    std::cout << "[Lite] Target FPS: " << LITE_TARGET_FPS << " Max Threads: " << LITE_MAX_THREADS << std::endl;
    std::cout << "[Lite] Optimized for: i3-3xxx (Ivy Bridge 2C/4T SSE4.2), Intel HD 4000 (16 EUs), GT 620M, AMD APU, 4GB RAM laptops" << std::endl;
#else
    std::cout << "[Lite] Mode: OFF (Full version)" << std::endl;
#endif

    std::cout << "================================" << std::endl;
}
