#include "lite_config.h"
#include "globals.h"
#include "lbm.h"

#include <iostream>
#include <thread>
#include <cmath>
#include <fstream>
#include <string>
#include <algorithm>
#include <cctype>

#ifdef _OPENMP
#include <omp.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#include <cstring>
#endif

#ifdef ANDROID
#include <sys/system_properties.h>
#endif

// Глобальные
bool g_autoQualityScaling = true;
bool g_batterySaver = true;
float g_currentFPSAverage = 30.0f;
LiteQualityPreset g_currentPreset = LiteQualityPreset::Low;
static bool g_isUltraLite = false;
bool g_isSD662Device = false;
bool g_isAdreno610 = false;
bool g_is90Hz = false;

bool isLiteMode() {
#ifdef AEROS_LITE
    return true;
#else
    return g_isLiteMode;
#endif
}

bool isUltraLiteMode() {
    return g_isUltraLite;
}

int getLiteMaxThreads() {
    if (g_isSD662Device) return ANDROID_SD662_MAX_THREADS;
#ifdef AEROS_LITE
    return g_isUltraLite ? ULTRA_LITE_MAX_THREADS : LITE_MAX_THREADS;
#else
    return g_liteMaxThreads;
#endif
}

bool isLiteGPU() {
#ifdef AEROS_LITE
    return true;
#else
    return g_isLiteMode;
#endif
}

bool isLowMemorySystem() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        DWORDLONG totalPhys = memInfo.ullTotalPhys / (1024*1024);
        return totalPhys < 4000;
    }
    return false;
#else
    long pages = sysconf(_SC_PHYS_PAGES);
    long pageSize = sysconf(_SC_PAGE_SIZE);
    long totalMem = pages * pageSize / (1024*1024);
    return totalMem < 4000;
#endif
}

bool isBatteryPower() {
#ifdef _WIN32
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
        if (sps.ACLineStatus == 0) return true;
        if (sps.ACLineStatus == 0 && sps.BatteryLifePercent != 255 && sps.BatteryLifePercent < 50) return true;
    }
    return false;
#else
    std::ifstream f("/sys/class/power_supply/BAT0/status");
    if (f.is_open()) {
        std::string status;
        f >> status;
        if (status == "Discharging") return true;
    }
    std::ifstream f2("/sys/class/power_supply/BAT1/status");
    if (f2.is_open()) {
        std::string status;
        f2 >> status;
        if (status == "Discharging") return true;
    }
    return false;
#endif
}

bool isWeakGPU(const char* glVendor, const char* glRenderer) {
    if (!glVendor || !glRenderer) return false;
    std::string vendor(glVendor);
    std::string renderer(glRenderer);
    std::transform(vendor.begin(), vendor.end(), vendor.begin(), ::tolower);
    std::transform(renderer.begin(), renderer.end(), renderer.begin(), ::tolower);

    if (renderer.find("hd 2000") != std::string::npos) return true;
    if (renderer.find("hd 3000") != std::string::npos) return true;
    if (renderer.find("hd 4000") != std::string::npos) return true;
    if (renderer.find("hd 4400") != std::string::npos) return true;
    if (renderer.find("hd 4600") != std::string::npos) return true;
    if (renderer.find("hd graphics") != std::string::npos) {
        if (renderer.find("620") == std::string::npos && renderer.find("630") == std::string::npos && renderer.find("520") == std::string::npos) {
            return true;
        }
    }
    if (renderer.find("intel") != std::string::npos && renderer.find("hd") != std::string::npos) {
        if (renderer.find("iris") == std::string::npos) return true;
    }
    if (renderer.find("uhd 600") != std::string::npos) return true;
    if (renderer.find("uhd 605") != std::string::npos) return true;
    if (renderer.find("gt 610") != std::string::npos) return true;
    if (renderer.find("gt 620") != std::string::npos) return true;
    if (renderer.find("gt 630") != std::string::npos) return true;
    if (renderer.find("gt 640") != std::string::npos) return true;
    if (renderer.find("gt 720") != std::string::npos) return true;
    if (renderer.find("gt 730") != std::string::npos) return true;
    if (renderer.find("gt 740") != std::string::npos) return true;
    if (renderer.find("820m") != std::string::npos) return true;
    if (renderer.find("920m") != std::string::npos) return true;
    if (renderer.find("radeon hd 7") != std::string::npos) return true;
    if (renderer.find("radeon r2") != std::string::npos) return true;
    if (renderer.find("radeon r3") != std::string::npos) return true;
    if (renderer.find("radeon r5") != std::string::npos) return true;
    if (renderer.find("amd radeon") != std::string::npos && renderer.find("r5") != std::string::npos) return true;
    if (renderer.find("llvmpipe") != std::string::npos) return true;
    if (renderer.find("softpipe") != std::string::npos) return true;
    if (renderer.find("swrast") != std::string::npos) return true;
    // Android weak GPUs
    if (renderer.find("adreno 306") != std::string::npos) return true;
    if (renderer.find("adreno 304") != std::string::npos) return true;
    if (renderer.find("mali-400") != std::string::npos) return true;
    if (renderer.find("mali-450") != std::string::npos) return true;
    if (renderer.find("sgx 544") != std::string::npos) return true;

    return false;
}

bool isAdreno610GPU(const char* glVendor, const char* glRenderer) {
    if (!glVendor || !glRenderer) return false;
    std::string vendor(glVendor);
    std::string renderer(glRenderer);
    std::transform(vendor.begin(), vendor.end(), vendor.begin(), ::tolower);
    std::transform(renderer.begin(), renderer.end(), renderer.begin(), ::tolower);

    // Adreno 610 — точно для SD662
    if (renderer.find("adreno (tm) 610") != std::string::npos) return true;
    if (renderer.find("adreno 610") != std::string::npos) return true;
    // Также 612, 616 — похожие
    if (renderer.find("adreno 612") != std::string::npos) return true;
    if (renderer.find("adreno 616") != std::string::npos) return true;

    return false;
}

bool isSnapdragon662() {
#ifdef ANDROID
    char soc[PROP_VALUE_MAX], hardware[PROP_VALUE_MAX], model[PROP_VALUE_MAX];
    __system_property_get("ro.soc.model", soc);
    __system_property_get("ro.hardware", hardware);
    __system_property_get("ro.product.board", model);

    std::string socStr(soc), hwStr(hardware), modelStr(model);
    std::transform(socStr.begin(), socStr.end(), socStr.begin(), ::tolower);
    std::transform(hwStr.begin(), hwStr.end(), hwStr.begin(), ::tolower);
    std::transform(modelStr.begin(), modelStr.end(), modelStr.begin(), ::tolower);

    if (socStr.find("sm6115") != std::string::npos) return true;
    if (socStr.find("sdm662") != std::string::npos) return true;
    if (socStr.find("662") != std::string::npos) return true;
    if (modelStr.find("sm6115") != std::string::npos) return true;
    if (modelStr.find("bengal") != std::string::npos) return true; // Bengal — кодовое имя SD662

    // Проверяем через cpuinfo — Kryo 260
    // Для простоты — если Adreno 610, то скорее всего SD662
    if (g_isAdreno610) return true;

    return false;
#else
    return false;
#endif
}

bool is90HzDisplay() {
#ifdef ANDROID
    // В Android — можно проверить через Java Display.getRefreshRate()
    // Тут заглушка — для SD662 часто 90Hz
    // Реальная проверка в Java
    return g_is90Hz;
#else
    return false;
#endif
}

const char* getGPUInfoString() {
    static std::string info;
    // В реальном рендере — glGetString
    if (g_isAdreno610) {
        info = "Adreno 610 (SD662) — ES 3.2, ~150 GFLOPS, 720x1604 90Hz, ASTC, optimized";
    } else if (g_isSD662Device) {
        info = "Snapdragon 662 — Kryo 260 8 cores 11nm, Adreno 610 class";
    } else {
        info = "Unknown GPU";
    }
    return info.c_str();
}

const char* getSoCInfoString() {
    static std::string info;
    if (g_isSD662Device) {
        info = "Snapdragon 662 (SM6115) — 4x Kryo 260 Gold @ 2.11GHz + 4x Silver @ 1.8GHz, 11nm, 8 cores, Adreno 610, 720x1604 90Hz";
    } else {
        info = "Unknown SoC";
    }
    return info.c_str();
}

LiteQualityPreset detectHardwarePreset() {
    // Сначала проверяем Android SD662
#ifdef ANDROID
    if (isSnapdragon662() || g_isAdreno610 || g_isSD662Device) {
        std::cout << "[Lite Detect] SD662/Adreno 610 detected — Balanced preset 60 FPS" << std::endl;
        return LiteQualityPreset::Balanced;
    }
#endif

    unsigned int hwThreads = std::thread::hardware_concurrency();
    bool lowRAM = isLowMemorySystem();
    bool onBattery = isBatteryPower();

    if (hwThreads <= 2 || lowRAM) {
#ifdef _WIN32
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);
        DWORDLONG totalPhys = memInfo.ullTotalPhys / (1024*1024);
        if (totalPhys < 3000 || hwThreads <= 2) {
            std::cout << "[Lite Detect] Ultra-Lite preset — weak CPU " << hwThreads << " threads, RAM " << totalPhys << " MB" << std::endl;
            return LiteQualityPreset::Potato;
        }
#else
        long pages = sysconf(_SC_PHYS_PAGES);
        long pageSize = sysconf(_SC_PAGE_SIZE);
        long totalMem = pages * pageSize / (1024*1024);
        if (totalMem < 3000 || hwThreads <= 2) {
            std::cout << "[Lite Detect] Ultra-Lite preset — weak CPU " << hwThreads << " threads, RAM " << totalMem << " MB" << std::endl;
            return LiteQualityPreset::Potato;
        }
#endif
    }

    if (hwThreads <= 4 && lowRAM) {
        std::cout << "[Lite Detect] Low preset — i3-like " << hwThreads << " threads, low RAM" << std::endl;
        return LiteQualityPreset::Low;
    }

    if (hwThreads <= 4) {
        std::cout << "[Lite Detect] Low preset — " << hwThreads << " threads (i3-3xxx class)" << std::endl;
        return LiteQualityPreset::Low;
    }

    if (hwThreads == 8) {
        // 8 cores — может быть SD662 или i7
        // Если Android — то Balanced для SD662
#ifdef ANDROID
        std::cout << "[Lite Detect] 8 cores Android — Balanced preset for SD662 class" << std::endl;
        return LiteQualityPreset::Balanced;
#else
        if (onBattery) {
            std::cout << "[Lite Detect] Medium preset — battery saver, " << hwThreads << " threads" << std::endl;
            return LiteQualityPreset::Medium;
        }
        std::cout << "[Lite Detect] Balanced preset — 8 threads modern but power saving" << std::endl;
        return LiteQualityPreset::Balanced;
#endif
    }

    if (hwThreads <= 8 && onBattery) {
        std::cout << "[Lite Detect] Medium preset — battery saver, " << hwThreads << " threads" << std::endl;
        return LiteQualityPreset::Medium;
    }

    if (lowRAM) {
        std::cout << "[Lite Detect] Medium preset — low RAM but decent CPU" << std::endl;
        return LiteQualityPreset::Medium;
    }

    std::cout << "[Lite Detect] Full preset — " << hwThreads << " threads, enough RAM" << std::endl;
    return LiteQualityPreset::Full;
}

LiteQualityPreset detectAndroidPreset() {
    // Для Android с учетом Adreno 610
    if (g_isAdreno610 || g_isSD662Device || isSnapdragon662()) {
        return LiteQualityPreset::Balanced;
    }

    // Проверяем RAM через Java — тут упрощенно
    unsigned int hwThreads = std::thread::hardware_concurrency();
    if (hwThreads <= 2) return LiteQualityPreset::Potato;
    if (hwThreads <= 4) return LiteQualityPreset::Low;
    if (hwThreads == 8) return LiteQualityPreset::Balanced; // SD662 8 cores
    return LiteQualityPreset::Medium;
}

const char* getPresetName(LiteQualityPreset p) {
    switch (p) {
        case LiteQualityPreset::Potato: return "Potato (Ultra-Lite)";
        case LiteQualityPreset::Low: return "Low (Lite)";
        case LiteQualityPreset::Medium: return "Medium";
        case LiteQualityPreset::Balanced: return "Balanced (SD662/Adreno 610)";
        case LiteQualityPreset::High: return "High";
        case LiteQualityPreset::Full: return "Full";
        default: return "Unknown";
    }
}

const char* getPresetDescription(LiteQualityPreset p) {
    switch (p) {
        case LiteQualityPreset::Potato: return "Atom/Celeron 1-2C, 2GB RAM, HD 3000, Adreno 306 — 500 particles, 4x40 streamlines, voxel 16, 20 FPS, 800x450";
        case LiteQualityPreset::Low: return "i3-3xxx 2C/4T, HD 4000/GT 620M, 4GB RAM, Adreno 405 — 1500 particles, 8x80 streamlines, voxel 24, 30 FPS, 1024x600, No CUDA";
        case LiteQualityPreset::Medium: return "i5-4xxx 4C, HD 4600/GT 740M, 8GB, Adreno 506 — 5000 particles, 16x150 streamlines, voxel 32, 45 FPS, 1280x720";
        case LiteQualityPreset::Balanced: return "SD662 8x Kryo 260 2.1GHz, Adreno 610 ES 3.2, 720x1604 90Hz, 4-6GB — 2500 particles, 12x120 streamlines, voxel 32, 60 FPS, 4 threads, ASTC";
        case LiteQualityPreset::High: return "i5-8xxx 4C+, GTX 1050, Adreno 640, 6GB+ — 8000 particles, 16x200 streamlines, voxel 40, 60 FPS";
        case LiteQualityPreset::Full: return "i5+ 4C+, GTX 1060+, 8GB+, SD 8 Gen 2 — 15000 particles, 24x300 streamlines, voxel 48, 60 FPS, Vulkan+CUDA";
        default: return "Unknown preset";
    }
}

void applyBalancedDefaults() {
    applySD662Defaults();
}

void applySD662Defaults() {
    std::cout << "[SD662 Balanced] Applying SD662/Adreno 610 optimized defaults for your phone..." << std::endl;
    g_isSD662Device = true;
    g_isAdreno610 = true;
    g_isLiteMode = false; // не Lite, а Balanced — больше чем Lite
    g_isUltraLite = false;
    g_currentPreset = LiteQualityPreset::Balanced;

    // Для SD662 — больше чем Lite, но оптимизировано для 720p 90Hz
    numParticles = ANDROID_SD662_PARTICLES; // 2500
    particleSize = 2.8f;
    maxSpeedForColor = 6.0f;

    numStreamlines = ANDROID_SD662_STREAMLINES; // 12
    streamlineSteps = ANDROID_SD662_STREAMLINE_STEPS; // 120
    streamlineStepSize = 0.08f; // точнее для лучшего качества
    streamlineAlpha = 0.85f;
    streamlineWidth = 2.5f;

    voxelResolution = ANDROID_SD662_VOXEL_RES; // 32
    useVoxelCollision = true;

    lbmParams.enabled = false; // LBM тяжело даже для SD662, но можно вкл low res
    lbmParams.stepsPerFrame = 1;
    lbmNx = ANDROID_SD662_LBM_RES; // 24
    lbmNy = ANDROID_SD662_LBM_RES;
    lbmNz = ANDROID_SD662_LBM_RES;
    lbmParams.useTurbulence = false;
    lbmParams.useGround = false;

    showModel = true;
    showParticles = true;
    showStreamlines = true;
    showPressure = true; // для SD662 можно давление
    showLiftDrag = true;
    showBoundingBox = false;
    showAxes = true;
    showGroundPlane = false;

    // Для 90Hz экрана — 60 FPS target, на батарее 45 FPS
    vsyncEnabled = true;
    limitFPS = true;
    maxFPS = ANDROID_SD662_MAX_FPS; // 60
    optFramePacing = true;
    optTargetFPS = ANDROID_SD662_TARGET_FPS; // 60

    // Оптимизации — включаем почти все
    optFrustumCulling = true;
    optOcclusionCulling = true; // для Adreno 610 можно occlusion
    optLOD = true;
    optEarlyZ = true;
    optDynamicParticles = true;
    optVRS = true;
    optAsyncCompute = false;
    optParticleLOD = 0; // full LOD для SD662
    optLODDistance = 5.0f;
    optMeshletCulling = true;

    // FSR/FG — для Adreno 610 можно FSR, но не FG
    fsrEnabled = true; // FSR помогает для 720p -> 1080p
    fgEnabled = false;

    // Интересные фичи — для SD662 можно некоторые
    aeroShowVortexTubes = true; // можно, но low count
    aeroShowShockWaves = true;
    aeroShowLIC = false; // LIC тяжело
    aeroVolumetricEnabled = false; // volumetric тяжело
    aeroSchlierenEnabled = true;
    aeroFlightMode = false;
    aeroShowAeroAcoustic = false;
    aeroShowTemperature = true; // можно температуру

    aeroSmokeDensity = 0.6f;
    aeroSmokeOpacity = 0.6f;
    aeroSmokeInjectors = 2;
    aeroVortexTubeCount = 12; // больше чем Lite

    flowSpeed = 3.5f;
    wakeStrength = 0.4f;
    wakeLength = 6.0f;
    strouhal = 0.2f;

    g_litePowerSaving = false; // для SD662 не нужен power saving на AC
    g_batterySaver = true; // но battery saver вкл для батареи
    g_autoQualityScaling = true;
    g_liteMaxThreads = ANDROID_SD662_MAX_THREADS; // 4 потока для 8 ядер SD662
    g_currentFPSAverage = ANDROID_SD662_TARGET_FPS;

    std::cout << "[SD662 Balanced] Applied for SD662/Adreno 610 720x1604 90Hz: " << numParticles << " particles, " << numStreamlines << "x" << streamlineSteps << " streamlines, voxel " << voxelResolution << ", " << optTargetFPS << " FPS, 4 threads, FSR ON, <800 MB" << std::endl;
}

void applyPreset(LiteQualityPreset preset) {
    g_currentPreset = preset;
    std::cout << "[Lite] Applying preset: " << getPresetName(preset) << " — " << getPresetDescription(preset) << std::endl;

    switch (preset) {
        case LiteQualityPreset::Potato:
            applyUltraLiteDefaults();
            break;
        case LiteQualityPreset::Low:
            applyLiteDefaults();
            break;
        case LiteQualityPreset::Medium:
            numParticles = 5000;
            numStreamlines = 16;
            streamlineSteps = 150;
            voxelResolution = 32;
            lbmParams.enabled = false;
            lbmParams.stepsPerFrame = 1;
            lbmNx = 48; lbmNy = 48; lbmNz = 48;
            lbmParams.useTurbulence = false;
            vsyncEnabled = true;
            limitFPS = true;
            maxFPS = 45.0f;
            optTargetFPS = 45.0f;
            fsrEnabled = false;
            fgEnabled = false;
            aeroShowVortexTubes = false;
            aeroVolumetricEnabled = false;
            aeroShowLIC = false;
            aeroShowAeroAcoustic = false;
            aeroSchlierenEnabled = true;
            aeroShowShockWaves = true;
            flowSpeed = 3.0f;
            g_liteMaxThreads = 4;
            break;
        case LiteQualityPreset::Balanced:
            applySD662Defaults();
            break;
        case LiteQualityPreset::High:
            numParticles = 8000;
            numStreamlines = 16;
            streamlineSteps = 200;
            voxelResolution = 40;
            lbmParams.enabled = false;
            lbmParams.stepsPerFrame = 2;
            lbmNx = 64; lbmNy = 64; lbmNz = 64;
            lbmParams.useTurbulence = false;
            vsyncEnabled = true;
            limitFPS = true;
            maxFPS = 60.0f;
            optTargetFPS = 60.0f;
            fsrEnabled = true;
            fgEnabled = false;
            aeroShowVortexTubes = true;
            aeroVolumetricEnabled = false;
            aeroShowLIC = false;
            aeroShowAeroAcoustic = false;
            aeroSchlierenEnabled = true;
            aeroShowShockWaves = true;
            flowSpeed = 4.0f;
            g_liteMaxThreads = 6;
            break;
        case LiteQualityPreset::Full:
            numParticles = 15000;
            numStreamlines = 24;
            streamlineSteps = 300;
            voxelResolution = 48;
            lbmParams.enabled = true;
            lbmParams.stepsPerFrame = 3;
            lbmNx = 128; lbmNy = 64; lbmNz = 64;
            lbmParams.useTurbulence = true;
            vsyncEnabled = false;
            limitFPS = false;
            maxFPS = 60.0f;
            optTargetFPS = 60.0f;
            fsrEnabled = true;
            fgEnabled = true;
            aeroShowVortexTubes = true;
            aeroVolumetricEnabled = true;
            aeroShowLIC = false;
            aeroShowAeroAcoustic = false;
            flowSpeed = 5.0f;
            g_liteMaxThreads = 0;
            break;
    }
}

void applyUltraLiteDefaults() {
    std::cout << "[Ultra-Lite] Applying Ultra-Lite defaults for Atom/Celeron/2GB RAM..." << std::endl;
    g_isUltraLite = true;
    g_isLiteMode = true;
    g_isSD662Device = false;

    numParticles = ULTRA_LITE_PARTICLES;
    particleSize = 3.0f;
    maxSpeedForColor = 5.0f;

    numStreamlines = ULTRA_LITE_STREAMLINES;
    streamlineSteps = ULTRA_LITE_STREAMLINE_STEPS;
    streamlineStepSize = 0.15f;
    streamlineAlpha = 1.0f;
    streamlineWidth = 3.0f;

    voxelResolution = ULTRA_LITE_VOXEL_RES;
    useVoxelCollision = true;

    lbmParams.enabled = false;
    lbmParams.stepsPerFrame = 1;
    lbmNx = ULTRA_LITE_LBM_RES;
    lbmNy = ULTRA_LITE_LBM_RES;
    lbmNz = ULTRA_LITE_LBM_RES;
    lbmParams.useTurbulence = false;
    lbmParams.useGround = false;

    showModel = true;
    showParticles = true;
    showStreamlines = true;
    showPressure = false;
    showLiftDrag = true;
    showBoundingBox = false;
    showAxes = false;
    showGroundPlane = false;

    vsyncEnabled = true;
    limitFPS = true;
    maxFPS = (float)ULTRA_LITE_TARGET_FPS;
    optFramePacing = true;
    optTargetFPS = (float)ULTRA_LITE_TARGET_FPS;

    optFrustumCulling = true;
    optOcclusionCulling = false;
    optLOD = true;
    optEarlyZ = true;
    optDynamicParticles = true;
    optVRS = true;
    optAsyncCompute = false;
    optParticleLOD = 2;
    optLODDistance = 2.0f;
    optMeshletCulling = true;

    fsrEnabled = false;
    fgEnabled = false;

    aeroShowVortexTubes = false;
    aeroShowShockWaves = true;
    aeroShowLIC = false;
    aeroVolumetricEnabled = false;
    aeroSchlierenEnabled = false;
    aeroFlightMode = false;
    aeroShowAeroAcoustic = false;
    aeroShowTemperature = false;

    aeroSmokeDensity = 0.3f;
    aeroSmokeOpacity = 0.3f;
    aeroSmokeInjectors = 1;
    aeroVortexTubeCount = 4;

    flowSpeed = 1.5f;
    wakeStrength = 0.2f;
    wakeLength = 3.0f;
    strouhal = 0.2f;

    std::cout << "[Ultra-Lite] Applied: particles=" << numParticles << " streamlines=" << numStreamlines
              << "x" << streamlineSteps << " voxel=" << voxelResolution << " LBM OFF" << std::endl;
}

void applyLiteDefaults() {
    std::cout << "[Lite] Applying Lite defaults for i3-3xxx / HD 4000 / GT 620M..." << std::endl;
    g_isUltraLite = false;
    g_isLiteMode = true;
    g_isSD662Device = false;

    numParticles = LITE_DEFAULT_PARTICLES;
    if (numParticles > LITE_MAX_PARTICLES) numParticles = LITE_MAX_PARTICLES;
    particleSize = 2.5f;
    maxSpeedForColor = 5.0f;

    numStreamlines = LITE_DEFAULT_STREAMLINES;
    streamlineSteps = LITE_DEFAULT_STREAMLINE_STEPS;
    streamlineStepSize = 0.1f;
    streamlineAlpha = 0.9f;
    streamlineWidth = 2.0f;

    voxelResolution = LITE_VOXEL_RESOLUTION;
    useVoxelCollision = true;

    lbmParams.enabled = LITE_LBM_ENABLED_DEFAULT;
    lbmParams.stepsPerFrame = LITE_LBM_STEPS_PER_FRAME;
    lbmNx = LITE_LBM_RESOLUTION;
    lbmNy = LITE_LBM_RESOLUTION;
    lbmNz = LITE_LBM_RESOLUTION;
    lbmParams.useTurbulence = false;
    lbmParams.useGround = false;

    showModel = true;
    showParticles = true;
    showStreamlines = true;
    showPressure = false;
    showLiftDrag = true;
    showBoundingBox = false;
    showAxes = true;
    showGroundPlane = false;

    vsyncEnabled = LITE_VSYNC_DEFAULT;
    limitFPS = LITE_LIMIT_FPS_DEFAULT;
    maxFPS = LITE_MAX_FPS;
    optFramePacing = true;
    optTargetFPS = LITE_TARGET_FPS;

    optFrustumCulling = true;
    optOcclusionCulling = false;
    optLOD = true;
    optEarlyZ = true;
    optDynamicParticles = true;
    optVRS = true;
    optAsyncCompute = false;
    optParticleLOD = 1;
    optLODDistance = 3.0f;
    optMeshletCulling = true;

    fsrEnabled = LITE_ENABLE_FSR;
    fgEnabled = LITE_ENABLE_FG;

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
    aeroSmokeInjectors = 2;
    aeroVortexTubeCount = 8;

    flowSpeed = 2.0f;
    wakeStrength = 0.3f;
    wakeLength = 5.0f;
    strouhal = 0.2f;

    std::cout << "[Lite] Defaults applied: particles=" << numParticles << " streamlines=" << numStreamlines
              << "x" << streamlineSteps << " voxel=" << voxelResolution << " LBM=" << (lbmParams.enabled?"ON":"OFF") << std::endl;
}

bool detectAndApplyLiteIfNeeded() {
    LiteQualityPreset preset = detectHardwarePreset();
    bool shouldBeLite = (preset == LiteQualityPreset::Potato || preset == LiteQualityPreset::Low);

    if (shouldBeLite && !g_isLiteMode) {
        std::cout << "[Lite Auto] Weak hardware detected (" << getPresetName(preset) << ") — auto-switching to Lite mode!" << std::endl;
        g_isLiteMode = true;
        applyPreset(preset);
        return true;
    }

    if (isBatteryPower() && g_batterySaver) {
        std::cout << "[Lite Auto] Battery power detected — enabling battery saver (30 FPS limit)" << std::endl;
        limitFPS = true;
        maxFPS = 30.0f;
        optTargetFPS = 30.0f;
        g_litePowerSaving = true;
    }

    return false;
}

void applyDynamicQualityScaling(float currentFPS) {
    if (!g_autoQualityScaling) return;

    g_currentFPSAverage = g_currentFPSAverage * 0.9f + currentFPS * 0.1f;

    float target = 60.0f;
    switch (g_currentPreset) {
        case LiteQualityPreset::Potato: target = ULTRA_LITE_TARGET_FPS; break;
        case LiteQualityPreset::Low: target = LITE_TARGET_FPS; break;
        case LiteQualityPreset::Medium: target = 45.0f; break;
        case LiteQualityPreset::Balanced: target = g_is90Hz ? 60.0f : 60.0f; if (isBatteryPower()) target = ANDROID_SD662_BATTERY_FPS; break;
        case LiteQualityPreset::High: target = 60.0f; break;
        case LiteQualityPreset::Full: target = 60.0f; break;
    }

    if (g_currentFPSAverage < target * 0.6f) {
        if (numParticles > 500) {
            int newParticles = (int)(numParticles * 0.8f);
            if (newParticles < 500) newParticles = 500;
            if (newParticles != numParticles) {
                std::cout << "[Lite AutoQuality] FPS " << g_currentFPSAverage << " < " << target*0.6f << " — reducing particles " << numParticles << " -> " << newParticles << std::endl;
                numParticles = newParticles;
            }
        } else if (numStreamlines > 4) {
            int newSL = numStreamlines - 1;
            std::cout << "[Lite AutoQuality] FPS " << g_currentFPSAverage << " low — reducing streamlines " << numStreamlines << " -> " << newSL << std::endl;
            numStreamlines = newSL;
        } else if (streamlineSteps > 40) {
            int newSteps = (int)(streamlineSteps * 0.8f);
            if (newSteps < 40) newSteps = 40;
            std::cout << "[Lite AutoQuality] FPS " << g_currentFPSAverage << " low — reducing steps " << streamlineSteps << " -> " << newSteps << std::endl;
            streamlineSteps = newSteps;
        }
    }
}

float getEstimatedVRAMUsageMB() {
    float particlesMB = numParticles * (sizeof(float)*3 + sizeof(float)*3 + sizeof(float)) / (1024*1024.0f);
    float streamlinesMB = numStreamlines * streamlineSteps * sizeof(float)*3 / (1024*1024.0f);
    float voxelMB = voxelResolution * voxelResolution * voxelResolution * sizeof(float) / (1024*1024.0f);
    float lbmMB = 0;
    if (lbmParams.enabled) {
        lbmMB = lbmNx * lbmNy * lbmNz * sizeof(float) * 10 / (1024*1024.0f);
    }
    float texturesMB = 2.0f;
    if (aeroVolumetricEnabled) texturesMB += 16.0f;
    if (fsrEnabled) texturesMB += 2.0f;
    return particlesMB + streamlinesMB + voxelMB + lbmMB + texturesMB;
}

float getEstimatedRAMUsageMB() {
    float vram = getEstimatedVRAMUsageMB();
    float overhead = 50.0f;
    return vram * 1.5f + overhead;
}

void saveLiteConfig(const char* path) {
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cout << "[Lite Config] Failed to save " << path << std::endl;
        return;
    }
    f << "# Aeros Engine Lite Config v1.21.0 SD662\n";
    f << "preset=" << (int)g_currentPreset << " # 0=Potato 1=Low 2=Medium 3=Balanced 4=High 5=Full\n";
    f << "isLite=" << (g_isLiteMode?1:0) << "\n";
    f << "isUltraLite=" << (g_isUltraLite?1:0) << "\n";
    f << "isSD662=" << (g_isSD662Device?1:0) << "\n";
    f << "isAdreno610=" << (g_isAdreno610?1:0) << "\n";
    f << "particles=" << numParticles << "\n";
    f << "streamlines=" << numStreamlines << "\n";
    f << "streamlineSteps=" << streamlineSteps << "\n";
    f << "voxelResolution=" << voxelResolution << "\n";
    f << "lbmEnabled=" << (lbmParams.enabled?1:0) << "\n";
    f << "lbmResolution=" << lbmNx << "\n";
    f << "targetFPS=" << optTargetFPS << "\n";
    f << "maxFPS=" << maxFPS << "\n";
    f << "vsync=" << (vsyncEnabled?1:0) << "\n";
    f << "autoQualityScaling=" << (g_autoQualityScaling?1:0) << "\n";
    f << "batterySaver=" << (g_batterySaver?1:0) << "\n";
    f << "powerSaving=" << (g_litePowerSaving?1:0) << "\n";
    f << "maxThreads=" << g_liteMaxThreads << "\n";
    f << "hwThreads=" << std::thread::hardware_concurrency() << "\n";
    f << "lowRAM=" << (isLowMemorySystem()?1:0) << "\n";
    f << "onBattery=" << (isBatteryPower()?1:0) << "\n";
    f.close();
    std::cout << "[Lite Config] Saved to " << path << std::endl;
}

bool loadLiteConfig(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line;
    int presetInt = -1;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq+1);
        size_t comment = val.find('#');
        if (comment != std::string::npos) val = val.substr(0, comment);
        val.erase(std::remove_if(val.begin(), val.end(), ::isspace), val.end());
        key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());

        try {
            if (key == "preset") presetInt = std::stoi(val);
            else if (key == "isLite") g_isLiteMode = (val=="1"||val=="true");
            else if (key == "isUltraLite") g_isUltraLite = (val=="1"||val=="true");
            else if (key == "isSD662") g_isSD662Device = (val=="1"||val=="true");
            else if (key == "isAdreno610") g_isAdreno610 = (val=="1"||val=="true");
            else if (key == "particles") numParticles = std::stoi(val);
            else if (key == "streamlines") numStreamlines = std::stoi(val);
            else if (key == "streamlineSteps") streamlineSteps = std::stoi(val);
            else if (key == "voxelResolution") voxelResolution = std::stoi(val);
            else if (key == "lbmEnabled") lbmParams.enabled = (val=="1");
            else if (key == "lbmResolution") { lbmNx = std::stoi(val); lbmNy = lbmNx; lbmNz = lbmNx; }
            else if (key == "targetFPS") optTargetFPS = std::stof(val);
            else if (key == "maxFPS") maxFPS = std::stof(val);
            else if (key == "vsync") vsyncEnabled = (val=="1");
            else if (key == "autoQualityScaling") g_autoQualityScaling = (val=="1");
            else if (key == "batterySaver") g_batterySaver = (val=="1");
            else if (key == "powerSaving") g_litePowerSaving = (val=="1");
            else if (key == "maxThreads") g_liteMaxThreads = std::stoi(val);
        } catch (...) {}
    }
    f.close();

    if (presetInt >= 0 && presetInt <= 5) {
        g_currentPreset = (LiteQualityPreset)presetInt;
        std::cout << "[Lite Config] Loaded preset " << getPresetName(g_currentPreset) << " from " << path << std::endl;
        return true;
    }
    std::cout << "[Lite Config] Loaded custom config from " << path << std::endl;
    return true;
}

void applyLiteOptimizations() {
    std::cout << "[Lite] Applying optimizations for weak GPUs..." << std::endl;

#ifdef _OPENMP
    if (LITE_ENABLE_OPENMP_LIMIT || g_isLiteMode || g_isSD662Device) {
        int maxThreads = g_liteMaxThreads >0 ? g_liteMaxThreads : (g_isSD662Device ? ANDROID_SD662_MAX_THREADS : (g_isUltraLite ? ULTRA_LITE_MAX_THREADS : LITE_MAX_THREADS));
        if (maxThreads > 0) {
            omp_set_num_threads(maxThreads);
            std::cout << "[Lite] OpenMP limited to " << maxThreads << " threads" << std::endl;
        } else {
            int hwThreads = std::thread::hardware_concurrency();
            if (hwThreads > 4) hwThreads = 2;
            else if (hwThreads > 2) hwThreads = 2;
            omp_set_num_threads(hwThreads);
            std::cout << "[Lite] OpenMP auto-limited to " << hwThreads << " threads (HW: " << std::thread::hardware_concurrency() << ")" << std::endl;
        }
    }
#endif

#ifdef _WIN32
    if (LITE_POWER_SAVING || g_litePowerSaving) {
        SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
        std::cout << "[Lite] Process priority BELOW_NORMAL for power saving" << std::endl;
    }
    if (g_batterySaver && isBatteryPower()) {
        SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
        std::cout << "[Lite] Battery mode — IDLE priority for max battery saving" << std::endl;
    }
#endif

    std::cout << "[Lite] Optimizations applied, preset " << getPresetName(g_currentPreset) << ", SD662=" << g_isSD662Device << " Adreno610=" << g_isAdreno610 << std::endl;
}

const char* getLiteInfoString() {
    if (g_isSD662Device) {
        return "Balanced SD662 v1.21.0 — Snapdragon 662 8x Kryo 260 2.1GHz Adreno 610 ES 3.2 720x1604 90Hz — 2500 particles 12x120 streamlines voxel 32 60 FPS 4 threads FSR ON <800 MB — Optimized for your phone!";
    }
    if (g_isUltraLite) {
        return "Ultra-Lite v1.20.1 — Atom/Celeron/2GB RAM/HD 3000 — 500 particles, 4x40 streamlines, voxel 16, 20 FPS, 800x450, 1 thread";
    }
#ifdef AEROS_LITE
    return "Lite v1.20.1 — i3-3xxx / HD 4000 / GT 620M / No CUDA — Optimized for weak devices, 30 FPS, low RAM, SSE2, auto-detect, battery saver";
#else
    if (g_isLiteMode) {
        return "Lite mode ON — auto-detected weak hardware, 30 FPS, low RAM";
    }
    return "Full v1.21.0 — Vulkan+OpenGL, CUDA, FSR, FG, all features, auto Lite detection, SD662 Balanced";
#endif
}

void printLiteSystemInfo() {
    std::cout << "=== Aeros Engine System Info v1.21.0 SD662 ===" << std::endl;
#ifdef VERSION
    std::cout << "Version: " << VERSION << " " << getLiteInfoString() << std::endl;
#else
    std::cout << "Version: " << getLiteInfoString() << std::endl;
#endif

    unsigned int hwThreads = std::thread::hardware_concurrency();
    std::cout << "CPU threads (HW): " << hwThreads << std::endl;
#ifdef _OPENMP
    std::cout << "OpenMP threads: " << omp_get_max_threads() << std::endl;
#endif

#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    DWORDLONG totalPhys = memInfo.ullTotalPhys / (1024*1024);
    DWORDLONG availPhys = memInfo.ullAvailPhys / (1024*1024);
    std::cout << "RAM: " << totalPhys << " MB total, " << availPhys << " MB available" << std::endl;
#else
    long pages = sysconf(_SC_PHYS_PAGES);
    long pageSize = sysconf(_SC_PAGE_SIZE);
    long totalMem = pages * pageSize / (1024*1024);
    std::cout << "RAM: " << totalMem << " MB total" << std::endl;
#endif

    std::cout << "Battery: " << (isBatteryPower() ? "On battery — battery saver ON" : "On AC power") << std::endl;
    std::cout << "SD662: " << (g_isSD662Device ? "YES — Snapdragon 662 detected" : "NO") << std::endl;
    std::cout << "Adreno 610: " << (g_isAdreno610 ? "YES — Adreno 610 detected" : "NO") << std::endl;
    std::cout << "90Hz: " << (g_is90Hz ? "YES — 90Hz display" : "NO") << std::endl;
    if (g_isSD662Device) {
        std::cout << "SoC: " << getSoCInfoString() << std::endl;
        std::cout << "GPU: " << getGPUInfoString() << std::endl;
    }
    std::cout << "Detected preset: " << getPresetName(detectHardwarePreset()) << " — " << getPresetDescription(detectHardwarePreset()) << std::endl;
    std::cout << "Current preset: " << getPresetName(g_currentPreset) << std::endl;
    std::cout << "Auto quality scaling: " << (g_autoQualityScaling ? "ON" : "OFF") << " — FPS avg " << g_currentFPSAverage << std::endl;
    std::cout << "Estimated VRAM: " << getEstimatedVRAMUsageMB() << " MB, RAM: " << getEstimatedRAMUsageMB() << " MB" << std::endl;

    std::cout << "========================================" << std::endl;
}
