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

// Глобальные для динамического качества
bool g_autoQualityScaling = true;
bool g_batterySaver = true;
float g_currentFPSAverage = 30.0f;
LiteQualityPreset g_currentPreset = LiteQualityPreset::Low;
static bool g_isUltraLite = false;

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
        // 0=on battery, 1=AC, 255=unknown
        if (sps.ACLineStatus == 0) return true;
        // Если батарея <50% и на батарее — тоже считаем battery saver
        if (sps.ACLineStatus == 0 && sps.BatteryLifePercent != 255 && sps.BatteryLifePercent < 50) return true;
    }
    return false;
#else
    // Linux: проверяем /sys/class/power_supply
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
    // To lower
    std::transform(vendor.begin(), vendor.end(), vendor.begin(), ::tolower);
    std::transform(renderer.begin(), renderer.end(), renderer.begin(), ::tolower);

    // Intel HD 2000/3000/4000/4400/4600 — слабые
    if (renderer.find("hd 2000") != std::string::npos) return true;
    if (renderer.find("hd 3000") != std::string::npos) return true;
    if (renderer.find("hd 4000") != std::string::npos) return true;
    if (renderer.find("hd 4400") != std::string::npos) return true;
    if (renderer.find("hd 4600") != std::string::npos) return true;
    if (renderer.find("hd graphics") != std::string::npos) {
        // HD Graphics без числа — часто слабая
        if (renderer.find("620") == std::string::npos && renderer.find("630") == std::string::npos && renderer.find("520") == std::string::npos) {
            return true;
        }
    }
    if (renderer.find("intel") != std::string::npos && renderer.find("hd") != std::string::npos) {
        // Intel HD — проверяем если не Iris
        if (renderer.find("iris") == std::string::npos) return true;
    }
    // Intel UHD 600/605 — слабые (Celeron/Pentium)
    if (renderer.find("uhd 600") != std::string::npos) return true;
    if (renderer.find("uhd 605") != std::string::npos) return true;
    // NVIDIA GT 6xx/7xx/8xx — слабые ноутбучные
    if (renderer.find("gt 610") != std::string::npos) return true;
    if (renderer.find("gt 620") != std::string::npos) return true;
    if (renderer.find("gt 630") != std::string::npos) return true;
    if (renderer.find("gt 640") != std::string::npos) return true;
    if (renderer.find("gt 720") != std::string::npos) return true;
    if (renderer.find("gt 730") != std::string::npos) return true;
    if (renderer.find("gt 740") != std::string::npos) return true;
    if (renderer.find("820m") != std::string::npos) return true;
    if (renderer.find("920m") != std::string::npos) return true;
    // AMD APU, Radeon HD 7000, R2/R3/R5
    if (renderer.find("radeon hd 7") != std::string::npos) return true;
    if (renderer.find("radeon r2") != std::string::npos) return true;
    if (renderer.find("radeon r3") != std::string::npos) return true;
    if (renderer.find("radeon r5") != std::string::npos) return true;
    if (renderer.find("amd radeon") != std::string::npos && renderer.find("r5") != std::string::npos) return true;
    // Mesa software renderer
    if (renderer.find("llvmpipe") != std::string::npos) return true;
    if (renderer.find("softpipe") != std::string::npos) return true;
    if (renderer.find("swrast") != std::string::npos) return true;

    return false;
}

LiteQualityPreset detectHardwarePreset() {
    unsigned int hwThreads = std::thread::hardware_concurrency();
    bool lowRAM = isLowMemorySystem();
    bool onBattery = isBatteryPower();

    // Ultra-Lite: 1-2 ядра, <3GB RAM, или батарея + слабое железо
    if (hwThreads <= 2 || lowRAM) {
        // Проверяем RAM точнее
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

const char* getPresetName(LiteQualityPreset p) {
    switch (p) {
        case LiteQualityPreset::Potato: return "Potato (Ultra-Lite)";
        case LiteQualityPreset::Low: return "Low (Lite)";
        case LiteQualityPreset::Medium: return "Medium";
        case LiteQualityPreset::Full: return "Full";
        default: return "Unknown";
    }
}

const char* getPresetDescription(LiteQualityPreset p) {
    switch (p) {
        case LiteQualityPreset::Potato: return "Atom/Celeron 1-2C, 2GB RAM, HD 3000 — 500 particles, 4x40 streamlines, voxel 16, 20 FPS, 800x450";
        case LiteQualityPreset::Low: return "i3-3xxx 2C/4T, HD 4000/GT 620M, 4GB RAM — 1500 particles, 8x80 streamlines, voxel 24, 30 FPS, 1024x600, No CUDA";
        case LiteQualityPreset::Medium: return "i5-4xxx 4C, HD 4600/GT 740M, 8GB — 5000 particles, 16x150 streamlines, voxel 32, 45 FPS, 1280x720";
        case LiteQualityPreset::Full: return "i5+ 4C+, GTX 1060+, 8GB+ — 15000 particles, 24x300 streamlines, voxel 48, 60 FPS, Vulkan+CUDA";
        default: return "Unknown preset";
    }
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
            // Средний — между Lite и Full
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
            break;
        case LiteQualityPreset::Full:
            // Full — стандартные настройки
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
            break;
    }
}

void applyUltraLiteDefaults() {
    std::cout << "[Ultra-Lite] Applying Ultra-Lite defaults for Atom/Celeron/2GB RAM..." << std::endl;
    g_isUltraLite = true;
    g_isLiteMode = true;

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
    optParticleLOD = 2; // ultra low
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
    // Авто-детект слабого железа и применение Lite
    LiteQualityPreset preset = detectHardwarePreset();
    bool shouldBeLite = (preset == LiteQualityPreset::Potato || preset == LiteQualityPreset::Low);

    if (shouldBeLite && !g_isLiteMode) {
        std::cout << "[Lite Auto] Weak hardware detected (" << getPresetName(preset) << ") — auto-switching to Lite mode!" << std::endl;
        g_isLiteMode = true;
        applyPreset(preset);
        return true;
    }

    // Также проверяем GPU если уже есть контекст
    // Это вызывается после создания GL контекста — проверим отдельно через isWeakGPU

    // Проверяем батарею
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

    // Скользящее среднее FPS
    g_currentFPSAverage = g_currentFPSAverage * 0.9f + currentFPS * 0.1f;

    float target = g_isUltraLite ? ULTRA_LITE_TARGET_FPS : (g_isLiteMode ? LITE_TARGET_FPS : 60.0f);
    if (g_currentFPSAverage < target * 0.6f) {
        // FPS сильно просел — снижаем качество
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
    } else if (g_currentFPSAverage > target * 1.2f && g_currentFPSAverage > target + 10) {
        // FPS выше цели — можно немного повысить качество (только если не Ultra-Lite)
        if (!g_isUltraLite && numParticles < LITE_MAX_PARTICLES && g_currentPreset != LiteQualityPreset::Potato) {
            if (numParticles < 3000) {
                int newParticles = (int)(numParticles * 1.1f);
                if (newParticles > LITE_MAX_PARTICLES) newParticles = LITE_MAX_PARTICLES;
                // Не спамим
            }
        }
    }
}

float getEstimatedVRAMUsageMB() {
    // Грубая оценка VRAM
    float particlesMB = numParticles * (sizeof(float)*3 + sizeof(float)*3 + sizeof(float)) / (1024*1024.0f); // pos+vel+size
    float streamlinesMB = numStreamlines * streamlineSteps * sizeof(float)*3 / (1024*1024.0f);
    float voxelMB = voxelResolution * voxelResolution * voxelResolution * sizeof(float) / (1024*1024.0f);
    float lbmMB = 0;
    if (lbmParams.enabled) {
        lbmMB = lbmNx * lbmNy * lbmNz * sizeof(float) * 10 / (1024*1024.0f); // rho, ux, uy, uz etc
    }
    float texturesMB = 2.0f; // базовые текстуры
    if (aeroVolumetricEnabled) texturesMB += 16.0f;
    return particlesMB + streamlinesMB + voxelMB + lbmMB + texturesMB;
}

float getEstimatedRAMUsageMB() {
    float vram = getEstimatedVRAMUsageMB();
    float overhead = 50.0f; // код, ImGui, etc
    return vram * 1.5f + overhead; // RAM обычно больше VRAM из-за дублирования
}

void saveLiteConfig(const char* path) {
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cout << "[Lite Config] Failed to save " << path << std::endl;
        return;
    }
    f << "# Aeros Engine Lite Config v1.20.1\n";
    f << "# Auto-generated — edit manually if needed\n";
    f << "preset=" << (int)g_currentPreset << " # 0=Potato 1=Low 2=Medium 3=Full\n";
    f << "isLite=" << (g_isLiteMode?1:0) << "\n";
    f << "isUltraLite=" << (g_isUltraLite?1:0) << "\n";
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
    f << "# Hardware detected\n";
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
        // Trim comment
        size_t comment = val.find('#');
        if (comment != std::string::npos) val = val.substr(0, comment);
        // Trim spaces
        val.erase(std::remove_if(val.begin(), val.end(), ::isspace), val.end());
        key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());

        try {
            if (key == "preset") presetInt = std::stoi(val);
            else if (key == "isLite") g_isLiteMode = (val=="1"||val=="true");
            else if (key == "isUltraLite") g_isUltraLite = (val=="1"||val=="true");
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

    if (presetInt >= 0 && presetInt <= 3) {
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
    if (LITE_ENABLE_OPENMP_LIMIT || g_isLiteMode) {
        int maxThreads = g_isUltraLite ? ULTRA_LITE_MAX_THREADS : (g_liteMaxThreads >0 ? g_liteMaxThreads : LITE_MAX_THREADS);
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
        // Еще ниже приоритет на батарее
        SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
        std::cout << "[Lite] Battery mode — IDLE priority for max battery saving" << std::endl;
    }
#endif

    std::cout << "[Lite] Optimizations applied for Intel HD 4000 / GT 620M / i3-3xxx, preset " << getPresetName(g_currentPreset) << std::endl;
}

const char* getLiteInfoString() {
    if (g_isUltraLite) {
        return "Ultra-Lite v1.20.1 — Atom/Celeron/2GB RAM/HD 3000 — 500 particles, 4x40 streamlines, voxel 16, 20 FPS, 800x450, 1 thread";
    }
#ifdef AEROS_LITE
    return "Lite v1.20.1 — i3-3xxx / HD 4000 / GT 620M / No CUDA — Optimized for weak devices, 30 FPS, low RAM, SSE2, auto-detect, battery saver";
#else
    if (g_isLiteMode) {
        return "Lite mode ON — auto-detected weak hardware, 30 FPS, low RAM";
    }
    return "Full v1.20.1 — Vulkan+OpenGL, CUDA, FSR, FG, all features, auto Lite detection";
#endif
}

void printLiteSystemInfo() {
    std::cout << "=== Aeros Engine System Info v1.20.1 ===" << std::endl;
#ifdef VERSION
    std::cout << "Version: " << VERSION << " " << getLiteInfoString() << std::endl;
#else
    std::cout << "Version: Lite " << getLiteInfoString() << std::endl;
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
    if (totalPhys < 4000) std::cout << "[Lite] Low RAM detected (<4GB) — using low memory mode" << std::endl;
    if (totalPhys < 3000) std::cout << "[Ultra-Lite] Very low RAM (<3GB) — using Ultra-Lite potato mode" << std::endl;
#else
    long pages = sysconf(_SC_PHYS_PAGES);
    long pageSize = sysconf(_SC_PAGE_SIZE);
    long totalMem = pages * pageSize / (1024*1024);
    std::cout << "RAM: " << totalMem << " MB total" << std::endl;
    if (totalMem < 4000) std::cout << "[Lite] Low RAM detected (<4GB) — using low memory mode" << std::endl;
    if (totalMem < 3000) std::cout << "[Ultra-Lite] Very low RAM (<3GB) — using Ultra-Lite potato mode" << std::endl;
#endif

    std::cout << "Battery: " << (isBatteryPower() ? "On battery — battery saver ON" : "On AC power") << std::endl;
    std::cout << "Detected preset: " << getPresetName(detectHardwarePreset()) << " — " << getPresetDescription(detectHardwarePreset()) << std::endl;
    std::cout << "Current preset: " << getPresetName(g_currentPreset) << std::endl;
    std::cout << "Auto quality scaling: " << (g_autoQualityScaling ? "ON" : "OFF") << " — FPS avg " << g_currentFPSAverage << std::endl;
    std::cout << "Estimated VRAM: " << getEstimatedVRAMUsageMB() << " MB, RAM: " << getEstimatedRAMUsageMB() << " MB" << std::endl;

#ifdef AEROS_LITE
    std::cout << "[Lite] Mode: ON" << (g_isUltraLite ? " (Ultra-Lite)" : "") << std::endl;
    std::cout << "[Lite] Particles: " << LITE_DEFAULT_PARTICLES << " (max " << LITE_MAX_PARTICLES << ")" << std::endl;
    std::cout << "[Lite] Streamlines: " << LITE_DEFAULT_STREAMLINES << "x" << LITE_DEFAULT_STREAMLINE_STEPS << std::endl;
    std::cout << "[Lite] Voxel: " << LITE_VOXEL_RESOLUTION << "^3" << std::endl;
    std::cout << "[Lite] LBM: " << (LITE_LBM_ENABLED_DEFAULT?"ON":"OFF") << " res " << LITE_LBM_RESOLUTION << std::endl;
    std::cout << "[Lite] OpenGL: " << LITE_OPENGL_VERSION_MAJOR << "." << LITE_OPENGL_VERSION_MINOR << " MSAA: " << LITE_MSAA_SAMPLES << std::endl;
    std::cout << "[Lite] Vulkan: " << (LITE_ENABLE_VULKAN?"ON":"OFF (HD 4000 fallback)") << std::endl;
    std::cout << "[Lite] FSR: " << (LITE_ENABLE_FSR?"ON":"OFF") << " FG: " << (LITE_ENABLE_FG?"ON":"OFF") << std::endl;
    std::cout << "[Lite] Target FPS: " << LITE_TARGET_FPS << " Max Threads: " << LITE_MAX_THREADS << std::endl;
    std::cout << "[Lite] Optimized for: i3-3xxx (Ivy Bridge 2C/4T SSE4.2), Intel HD 4000 (16 EUs), GT 620M, AMD APU, 4GB RAM laptops" << std::endl;
    std::cout << "[Ultra-Lite] Potato: " << ULTRA_LITE_PARTICLES << " particles, " << ULTRA_LITE_STREAMLINES << "x" << ULTRA_LITE_STREAMLINE_STEPS << " streamlines, voxel " << ULTRA_LITE_VOXEL_RES << ", " << ULTRA_LITE_TARGET_FPS << " FPS, " << ULTRA_LITE_WINDOW_W << "x" << ULTRA_LITE_WINDOW_H << std::endl;
#else
    std::cout << "[Lite] Mode: " << (g_isLiteMode ? "ON (auto-detected)" : "OFF (Full)") << (g_isUltraLite ? " Ultra-Lite" : "") << std::endl;
#endif

    std::cout << "========================================" << std::endl;
}
