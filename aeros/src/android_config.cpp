#include "android_config.h"
#include "globals.h"
#include "lite_config.h"
#include "lbm.h"

#include <iostream>
#include <algorithm>
#include <cctype>

#ifdef ANDROID
#include <sys/system_properties.h>
#include <android/log.h>
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "AerosEngine", __VA_ARGS__))
#else
#define LOGI(...) std::cout << "[Android] " << __VA_ARGS__ << std::endl
#endif

bool isAndroidLowRamDevice() {
#ifdef ANDROID
    char lowRam[PROP_VALUE_MAX];
    __system_property_get("ro.config.low_ram", lowRam);
    if (lowRam[0] == 't' || lowRam[0] == '1') return true;
    // Также проверяем по total mem — если <2GB, считаем low ram
    // В Android NDK нет прямого доступа, но можно через Java
    // Для простоты — возвращаем false, реальная проверка в Java
    return false;
#else
    return isLowMemorySystem();
#endif
}

bool isAndroidPotatoDevice() {
#ifdef ANDROID
    // Проверяем по свойствам — старые чипы
    char hardware[PROP_VALUE_MAX], model[PROP_VALUE_MAX];
    __system_property_get("ro.hardware", hardware);
    __system_property_get("ro.product.model", model);

    std::string hw(hardware);
    std::string mdl(model);
    // To lower
    std::transform(hw.begin(), hw.end(), hw.begin(), ::tolower);
    std::transform(mdl.begin(), mdl.end(), mdl.begin(), ::tolower);

    // Старые чипы — Snapdragon 410, 400, 200, MT6582, etc
    if (hw.find("qcom") != std::string::npos) {
        // Проверяем модель — если содержит старые названия
        if (mdl.find("sm-g313") != std::string::npos) return true; // Galaxy Ace 4 — 1GB RAM
        if (mdl.find("gt-s") != std::string::npos) return true; // старые Galaxy
    }
    // Если low ram device — точно Potato
    if (isAndroidLowRamDevice()) return true;
    return false;
#else
    return false;
#endif
}

void applyAndroidPotatoDefaults() {
    std::cout << "[Android Potato] Applying Potato defaults for weakest phones 1GB RAM Adreno 306..." << std::endl;

    numParticles = ANDROID_POTATO_PARTICLES;
    particleSize = 4.0f; // больше для видимости на маленьком экране
    maxSpeedForColor = 5.0f;

    numStreamlines = ANDROID_POTATO_STREAMLINES;
    streamlineSteps = ANDROID_POTATO_STREAMLINE_STEPS;
    streamlineStepSize = 0.2f;
    streamlineAlpha = 1.0f;
    streamlineWidth = 4.0f; // толще для touch экрана

    voxelResolution = ANDROID_POTATO_VOXEL_RES;
    useVoxelCollision = true;

    lbmParams.enabled = false;
    lbmParams.stepsPerFrame = 1;
    lbmNx = ANDROID_POTATO_LBM_RES;
    lbmNy = ANDROID_POTATO_LBM_RES;
    lbmNz = ANDROID_POTATO_LBM_RES;
    lbmParams.useTurbulence = false;

    showModel = true;
    showParticles = true;
    showStreamlines = true;
    showPressure = false;
    showLiftDrag = false; // выкл для экономии места на маленьком экране
    showBoundingBox = false;
    showAxes = false;
    showGroundPlane = false;

    vsyncEnabled = true;
    limitFPS = true;
    maxFPS = ANDROID_POTATO_MAX_FPS;
    optFramePacing = true;
    optTargetFPS = ANDROID_POTATO_TARGET_FPS;

    optFrustumCulling = true;
    optOcclusionCulling = false;
    optLOD = true;
    optEarlyZ = true;
    optDynamicParticles = true;
    optVRS = true;
    optAsyncCompute = false;
    optParticleLOD = 2;
    optLODDistance = 2.0f;

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

    aeroSmokeDensity = 0.2f;
    aeroSmokeOpacity = 0.2f;
    aeroSmokeInjectors = 1;
    aeroVortexTubeCount = 2;

    flowSpeed = 1.0f;
    wakeStrength = 0.1f;
    wakeLength = 2.0f;

    g_isLiteMode = true;
    g_isUltraLiteMode = true;
    g_litePowerSaving = true;
    g_batterySaver = true;
    g_autoQualityScaling = true;
    g_liteMaxThreads = ANDROID_POTATO_MAX_THREADS;
    g_currentPreset = LiteQualityPreset::Potato;
    g_currentFPSAverage = ANDROID_POTATO_TARGET_FPS;

    std::cout << "[Android Potato] Applied: " << numParticles << " particles, " << numStreamlines << "x" << streamlineSteps << " streamlines, voxel " << voxelResolution << ", " << optTargetFPS << " FPS, 1 thread, <150 MB" << std::endl;
}

void applyAndroidLiteDefaults() {
    std::cout << "[Android Lite] Applying Lite defaults for weak phones 2-4GB RAM Adreno 405+..." << std::endl;

    numParticles = ANDROID_LITE_PARTICLES;
    particleSize = 3.0f;
    maxSpeedForColor = 5.0f;

    numStreamlines = ANDROID_LITE_STREAMLINES;
    streamlineSteps = ANDROID_LITE_STREAMLINE_STEPS;
    streamlineStepSize = 0.15f;
    streamlineAlpha = 0.9f;
    streamlineWidth = 3.0f;

    voxelResolution = ANDROID_LITE_VOXEL_RES;
    useVoxelCollision = true;

    lbmParams.enabled = false;
    lbmParams.stepsPerFrame = 1;
    lbmNx = ANDROID_LITE_LBM_RES;
    lbmNy = ANDROID_LITE_LBM_RES;
    lbmNz = ANDROID_LITE_LBM_RES;
    lbmParams.useTurbulence = false;

    showModel = true;
    showParticles = true;
    showStreamlines = true;
    showPressure = false;
    showLiftDrag = true;
    showBoundingBox = false;
    showAxes = true;
    showGroundPlane = false;

    vsyncEnabled = true;
    limitFPS = true;
    maxFPS = ANDROID_LITE_MAX_FPS;
    optFramePacing = true;
    optTargetFPS = ANDROID_LITE_TARGET_FPS;

    optFrustumCulling = true;
    optOcclusionCulling = false;
    optLOD = true;
    optEarlyZ = true;
    optDynamicParticles = true;
    optVRS = true;
    optAsyncCompute = false;
    optParticleLOD = 1;
    optLODDistance = 3.0f;

    fsrEnabled = false;
    fgEnabled = false;

    aeroShowVortexTubes = false;
    aeroShowShockWaves = true;
    aeroShowLIC = false;
    aeroVolumetricEnabled = false;
    aeroSchlierenEnabled = true;
    aeroFlightMode = false;
    aeroShowAeroAcoustic = false;
    aeroShowTemperature = false;

    aeroSmokeDensity = 0.4f;
    aeroSmokeOpacity = 0.4f;
    aeroSmokeInjectors = 1;
    aeroVortexTubeCount = 4;

    flowSpeed = 1.5f;
    wakeStrength = 0.2f;
    wakeLength = 3.0f;

    g_isLiteMode = true;
    g_isUltraLiteMode = false;
    g_litePowerSaving = true;
    g_batterySaver = true;
    g_autoQualityScaling = true;
    g_liteMaxThreads = ANDROID_LITE_MAX_THREADS;
    g_currentPreset = LiteQualityPreset::Low;
    g_currentFPSAverage = ANDROID_LITE_TARGET_FPS;

    std::cout << "[Android Lite] Applied: " << numParticles << " particles, " << numStreamlines << "x" << streamlineSteps << " streamlines, voxel " << voxelResolution << ", " << optTargetFPS << " FPS, 2 threads, <300 MB" << std::endl;
}

void applyAndroidDefaults(bool isPotato) {
    if (isPotato) {
        applyAndroidPotatoDefaults();
    } else {
        applyAndroidLiteDefaults();
    }
}

const char* getAndroidDeviceInfo() {
#ifdef ANDROID
    static char info[512];
    char brand[PROP_VALUE_MAX], model[PROP_VALUE_MAX], version[PROP_VALUE_MAX], api[PROP_VALUE_MAX];
    __system_property_get("ro.product.brand", brand);
    __system_property_get("ro.product.model", model);
    __system_property_get("ro.build.version.release", version);
    __system_property_get("ro.build.version.sdk", api);
    snprintf(info, sizeof(info), "%s %s Android %s API %s", brand, model, version, api);
    return info;
#else
    return "Not Android";
#endif
}

int getAndroidApiLevel() {
#ifdef ANDROID
    char api[PROP_VALUE_MAX];
    __system_property_get("ro.build.version.sdk", api);
    return atoi(api);
#else
    return 0;
#endif
}

float getAndroidBatteryLevel() {
    // В реальном Android — через Java BatteryManager
    // Тут заглушка
    return 100.0f;
}

bool getAndroidIsCharging() {
    return false;
}
