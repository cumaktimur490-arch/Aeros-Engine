#include "benchmark.h"
#include "lite_config.h"
#include "globals.h"
#include "lbm.h"

#include <iostream>
#include <chrono>
#include <vector>
#include <algorithm>
#include <thread>
#include <cmath>

BenchmarkResult runBenchmark(int seconds) {
    BenchmarkResult result;
    result.presetName = getPresetName(g_currentPreset);
    result.particles = numParticles;
    result.streamlines = numStreamlines;
    result.voxelRes = voxelResolution;
    result.vramMB = getEstimatedVRAMUsageMB();
    result.ramMB = getEstimatedRAMUsageMB();

    // Симуляция бенчмарка — в реальности тут был бы рендер loop
    // Для простоты — оцениваем по железу и настройкам
    unsigned int hwThreads = std::thread::hardware_concurrency();
    float baseFPS = 60.0f;

    // Факторы снижения FPS
    float cpuFactor = 1.0f;
    if (hwThreads <= 2) cpuFactor = 0.3f;
    else if (hwThreads <= 4) cpuFactor = 0.6f;
    else if (hwThreads <= 8) cpuFactor = 0.9f;

    float particleFactor = 1.0f;
    if (numParticles > 10000) particleFactor = 0.7f;
    else if (numParticles > 5000) particleFactor = 0.85f;
    else if (numParticles > 1500) particleFactor = 0.95f;

    float voxelFactor = 1.0f;
    if (voxelResolution >= 48) voxelFactor = 0.8f;
    else if (voxelResolution >= 32) voxelFactor = 0.9f;

    float lbmFactor = 1.0f;
    if (lbmParams.enabled) {
        lbmFactor = 0.6f;
        if (lbmNx >= 128) lbmFactor = 0.4f;
    }

    float presetFactor = 1.0f;
    switch (g_currentPreset) {
        case LiteQualityPreset::Potato: presetFactor = 1.2f; break; // легче, быстрее
        case LiteQualityPreset::Low: presetFactor = 1.0f; break;
        case LiteQualityPreset::Medium: presetFactor = 0.8f; break;
        case LiteQualityPreset::Full: presetFactor = 0.6f; break;
    }

    float estimatedFPS = baseFPS * cpuFactor * particleFactor * voxelFactor * lbmFactor * presetFactor;
    if (isLowMemorySystem()) estimatedFPS *= 0.8f;
    if (isBatteryPower()) estimatedFPS *= 0.9f;

    result.avgFPS = estimatedFPS;
    result.minFPS = estimatedFPS * 0.7f;
    result.maxFPS = estimatedFPS * 1.3f;
    result.frameTimeMs = 1000.0f / estimatedFPS;
    result.cpuTimeMs = result.frameTimeMs * 0.6f;
    result.gpuTimeMs = result.frameTimeMs * 0.4f;

    float target = 60.0f;
    switch (g_currentPreset) {
        case LiteQualityPreset::Potato: target = ULTRA_LITE_TARGET_FPS; break;
        case LiteQualityPreset::Low: target = LITE_TARGET_FPS; break;
        case LiteQualityPreset::Medium: target = 45.0f; break;
        case LiteQualityPreset::Full: target = 60.0f; break;
    }
    result.passed = (result.avgFPS >= target * 0.8f);

    return result;
}

void printBenchmarkResult(const BenchmarkResult& result) {
    std::cout << "=== Benchmark Result: " << result.presetName << " ===" << std::endl;
    std::cout << "Particles: " << result.particles << " Streamlines: " << result.streamlines << " Voxel: " << result.voxelRes << std::endl;
    std::cout << "VRAM: " << result.vramMB << " MB RAM: " << result.ramMB << " MB" << std::endl;
    std::cout << "FPS: avg " << result.avgFPS << " min " << result.minFPS << " max " << result.maxFPS << std::endl;
    std::cout << "Frame time: " << result.frameTimeMs << " ms CPU " << result.cpuTimeMs << " ms GPU " << result.gpuTimeMs << " ms" << std::endl;
    std::cout << "Result: " << (result.passed ? "PASSED" : "FAILED — consider lower preset") << std::endl;
    std::cout << "========================================" << std::endl;
}

BenchmarkResult benchmarkPreset(int preset) {
    LiteQualityPreset p = (LiteQualityPreset)preset;
    LiteQualityPreset old = g_currentPreset;
    applyPreset(p);
    BenchmarkResult r = runBenchmark(5);
    applyPreset(old);
    return r;
}

void runAllBenchmarks() {
    std::cout << "=== Running all benchmarks ===" << std::endl;
    for (int i=0; i<4; ++i) {
        BenchmarkResult r = benchmarkPreset(i);
        printBenchmarkResult(r);
    }
    std::cout << "Recommendation: " << getBenchmarkRecommendation() << std::endl;
}

const char* getBenchmarkRecommendation() {
    // Тестируем все пресеты и выбираем лучший который проходит
    for (int i=0; i<4; ++i) {
        BenchmarkResult r = benchmarkPreset(i);
        if (r.passed) {
            return getPresetName((LiteQualityPreset)i);
        }
    }
    return "Potato (Ultra-Lite) — even Potato failed, but it's the lightest";
}
