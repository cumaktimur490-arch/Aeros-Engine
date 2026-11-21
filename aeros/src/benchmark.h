#pragma once
// v1.20.1 Benchmark — тест производительности для Lite/Full

#include <string>

struct BenchmarkResult {
    std::string presetName;
    float avgFPS;
    float minFPS;
    float maxFPS;
    float frameTimeMs;
    float cpuTimeMs;
    float gpuTimeMs;
    int particles;
    int streamlines;
    int voxelRes;
    float vramMB;
    float ramMB;
    bool passed; // FPS >= target*0.8
};

BenchmarkResult runBenchmark(int seconds = 10);
void printBenchmarkResult(const BenchmarkResult& result);
BenchmarkResult benchmarkPreset(int preset); // 0=potato 1=low 2=medium 3=full
void runAllBenchmarks();
const char* getBenchmarkRecommendation();
