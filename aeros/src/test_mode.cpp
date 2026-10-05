#include "test_mode.h"
#include "globals.h"
#include "flow_field.h"
#include "voxel_grid.h"
#include "atmosphere.h"
#include "forces.h"

#include <glm/glm.hpp>
#include <cmath>
#include <chrono>
#include <sstream>
#include <iostream>
#include <limits>

// Глобальные
bool testModeEnabled = false;
bool testContinuous = false;
std::vector<TestResult> lastTestResults;
int testsPassed = 0;
int testsFailed = 0;
float lastTestTimeMs = 0.0f;
std::string testLog;

static bool isValidFloat(float v) {
    return !std::isnan(v) && !std::isinf(v);
}

static bool isValidVec3(const glm::vec3& v) {
    return isValidFloat(v.x) && isValidFloat(v.y) && isValidFloat(v.z);
}

void logTest(const std::string& msg) {
    testLog += msg + "\n";
    std::cout << "[TEST] " << msg << std::endl;
}

bool testSpeedConversion() {
    logTest("Testing speed conversion...");
    struct Case { float ms; SpeedUnit unit; float expected; };
    Case cases[] = {
        {10.0f, SPEED_MS, 10.0f},
        {10.0f, SPEED_KMH, 36.0f},
        {10.0f, SPEED_MPH, 22.3694f},
        {10.0f, SPEED_KNOTS, 19.4384f},
        {10.0f, SPEED_FTS, 32.8084f},
    };
    bool ok = true;
    for (auto& c : cases) {
        float converted = speedFromMS(c.ms, c.unit);
        float back = speedToMS(converted, c.unit);
        float diff = fabsf(back - c.ms);
        if (diff > 0.01f || !isValidFloat(converted) || !isValidFloat(back)) {
            logTest("  FAIL: " + std::string(speedUnitShort(c.unit)) + " roundtrip diff=" + std::to_string(diff));
            ok = false;
        }
        float expDiff = fabsf(converted - c.expected);
        if (expDiff > 0.05f) {
            logTest("  FAIL: " + std::string(speedUnitShort(c.unit)) + " expected=" + std::to_string(c.expected) + " got=" + std::to_string(converted));
            ok = false;
        }
    }
    // Проверка всех единиц
    for (int u = 0; u < SPEED_UNIT_COUNT; u++) {
        float val = 5.0f;
        float toMS = speedToMS(val, (SpeedUnit)u);
        float fromMS = speedFromMS(toMS, (SpeedUnit)u);
        if (fabsf(fromMS - val) > 0.001f) {
            logTest("  FAIL: unit " + std::to_string(u) + " roundtrip");
            ok = false;
        }
    }
    return ok;
}

bool testAtmosphereModel() {
    logTest("Testing ISA atmosphere model...");
    struct Ref {
        float alt;
        float rho;
        float pressure;
        float temp;
    };
    // Референсные значения ISA (приблизительные, из таблиц)
    Ref refs[] = {
        {0.0f, 1.225f, 101325.0f, 288.15f},
        {1000.0f, 1.1116f, 89874.0f, 281.65f},
        {2000.0f, 1.0065f, 79501.0f, 275.15f},
        {5000.0f, 0.7364f, 54019.0f, 255.65f},
        {10000.0f, 0.4135f, 26436.0f, 223.15f},
        {15000.0f, 0.1948f, 12111.0f, 216.65f},
        {20000.0f, 0.0889f, 5529.0f, 216.65f},
    };
    bool ok = true;
    for (auto& r : refs) {
        AtmosphereParams atm = calculateAtmosphere(r.alt);
        if (!isValidFloat(atm.density) || !isValidFloat(atm.pressure) || !isValidFloat(atm.temperature)) {
            logTest("  FAIL: NaN at alt " + std::to_string(r.alt));
            ok = false;
            continue;
        }
        float rhoDiff = fabsf(atm.density - r.rho) / (r.rho + 1e-6f);
        float pDiff = fabsf(atm.pressure - r.pressure) / (r.pressure + 1e-6f);
        float tDiff = fabsf(atm.temperature - r.temp);
        if (rhoDiff > 0.05f) {
            logTest("  FAIL: rho at " + std::to_string(r.alt) + "m expected " + std::to_string(r.rho) + " got " + std::to_string(atm.density) + " diff " + std::to_string(rhoDiff*100) + "%");
            ok = false;
        }
        if (pDiff > 0.05f) {
            logTest("  FAIL: P at " + std::to_string(r.alt) + "m diff " + std::to_string(pDiff*100) + "%");
            ok = false;
        }
        if (tDiff > 2.0f) {
            logTest("  FAIL: T at " + std::to_string(r.alt) + "m expected " + std::to_string(r.temp) + " got " + std::to_string(atm.temperature));
            ok = false;
        }
        // Плотность должна убывать с высотой
        if (r.alt > 0 && atm.density >= 1.3f) {
            logTest("  FAIL: density not decreasing at " + std::to_string(r.alt));
            ok = false;
        }
        // Давление тоже убывает
        if (atm.pressure > 110000.0f || atm.pressure < 1.0f) {
            logTest("  FAIL: pressure out of range at " + std::to_string(r.alt));
            ok = false;
        }
    }
    // Монотонность
    float prevRho = 10.0f;
    for (float h = 0; h <= 20000; h += 1000) {
        float rho = getAirDensity(h);
        if (rho > prevRho + 0.001f) {
            logTest("  FAIL: density not monotonic at " + std::to_string(h));
            ok = false;
        }
        prevRho = rho;
    }
    return ok;
}

bool testSDFSampling() {
    logTest("Testing SDF sampling...");
    if (g_distanceField.empty()) {
        logTest("  SKIP: no voxel grid loaded");
        return true; // не ошибка, просто нет модели
    }
    bool ok = true;
    // Точка в центре модели должна быть внутри (отрицательный SDF) или близко
    float dCenter = sampleSDFCPU(center);
    if (!isValidFloat(dCenter)) {
        logTest("  FAIL: SDF at center is NaN/Inf");
        ok = false;
    }
    // Далеко от модели — большой положительный SDF
    glm::vec3 farPoint = center + glm::vec3(maxDim * 10.0f, 0, 0);
    float dFar = sampleSDFCPU(farPoint);
    if (!isValidFloat(dFar)) {
        logTest("  FAIL: SDF far point NaN");
        ok = false;
    }
    if (dFar < 0) {
        logTest("  FAIL: SDF far point should be positive, got " + std::to_string(dFar));
        ok = false;
    }
    // Нормаль должна быть валидной и нормализованной
    glm::vec3 n = sdfNormalCPU(center + glm::vec3(maxDim*0.6f, 0, 0));
    if (!isValidVec3(n)) {
        logTest("  FAIL: SDF normal NaN");
        ok = false;
    }
    float nLen = glm::length(n);
    if (fabsf(nLen - 1.0f) > 0.1f && nLen > 0.001f) {
        logTest("  FAIL: SDF normal not normalized, len=" + std::to_string(nLen));
        ok = false;
    }
    // Проверка на разных точках — не должно быть NaN
    for (int i = 0; i < 100; i++) {
        float x = minBB.x + (maxBB.x - minBB.x) * (i / 100.0f);
        glm::vec3 p(x, center.y, center.z);
        float d = sampleSDFCPU(p);
        if (!isValidFloat(d)) {
            logTest("  FAIL: SDF NaN at x=" + std::to_string(x));
            ok = false;
            break;
        }
    }
    return ok;
}

bool testVelocityField() {
    logTest("Testing velocity field...");
    updateFlowParams();
    bool ok = true;
    float vinf = glm::length(glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz));
    if (vinf < 1e-6f) {
        logTest("  SKIP: zero freestream velocity");
        return true;
    }

    // Тест в разных точках домена
    int nanCount = 0;
    int negPenetration = 0;
    for (int i = 0; i < 200; i++) {
        float fx = (i % 10) / 9.0f;
        float fy = ((i / 10) % 10) / 9.0f;
        float fz = (i / 100) / 2.0f;
        glm::vec3 p(
            flowParams.minX + fx * (flowParams.maxX - flowParams.minX),
            flowParams.minY + fy * (flowParams.maxY - flowParams.minY),
            flowParams.minZ + fz * (flowParams.maxZ - flowParams.minZ)
        );
        glm::vec3 v = computeVelocityFieldCPU(p, flowParams);
        if (!isValidVec3(v)) {
            nanCount++;
            continue;
        }
        float speed = glm::length(v);
        if (speed > vinf * 5.0f) {
            logTest("  WARN: velocity magnitude too high at (" + std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z) + ") speed=" + std::to_string(speed) + " vinf=" + std::to_string(vinf));
            // не фейлим, но предупреждаем — ускорение на боках может давать 1.6*vinf, но не 5x
            if (speed > vinf * 10.0f) {
                ok = false;
            }
        }
        // Проверка непротекания: если близко к поверхности и скорость внутрь — плохо
        if (!g_distanceField.empty()) {
            float d = sampleSDFCPU(p);
            if (d > 0 && d < flowParams.cellSizeX * 2.0f) {
                glm::vec3 n = sdfNormalCPU(p);
                float vn = glm::dot(v, n);
                // Если точка снаружи и близко, нормальная компонента внутрь (отрицательная) должна быть убрана
                // Допускаем небольшую погрешность
                if (vn < -0.1f * vinf) {
                    negPenetration++;
                }
            }
        }
    }
    if (nanCount > 0) {
        logTest("  FAIL: velocity field produced " + std::to_string(nanCount) + " NaN/Inf values");
        ok = false;
    }
    if (negPenetration > 10) {
        logTest("  FAIL: " + std::to_string(negPenetration) + " points with negative penetration near surface");
        ok = false;
    }
    // Проверка внутри объекта — скорость должна быть маленькой
    if (!g_distanceField.empty()) {
        glm::vec3 vInside = computeVelocityFieldCPU(center, flowParams);
        if (!isValidVec3(vInside)) {
            logTest("  FAIL: velocity inside object NaN");
            ok = false;
        } else {
            float speedInside = glm::length(vInside);
            if (speedInside > vinf * 0.5f) {
                logTest("  WARN: velocity inside object too high: " + std::to_string(speedInside));
            }
        }
    }
    return ok;
}

bool testPressureCalculation() {
    logTest("Testing pressure calculation...");
    if (g_vertices.empty()) {
        logTest("  SKIP: no model loaded");
        return true;
    }
    updateFlowParams();
    bool ok = true;
    float vinf = glm::length(glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz));
    if (vinf < 1e-6f) {
        logTest("  SKIP: zero velocity");
        return true;
    }
    // Тест Cp в нескольких точках
    int nanCount = 0;
    int outOfRange = 0;
    for (size_t i = 0; i < g_vertices.size(); i += 3*10) { // каждая 10-я вершина
        glm::vec3 p(g_vertices[i], g_vertices[i+1], g_vertices[i+2]);
        glm::vec3 v = computeVelocityFieldCPU(p, flowParams);
        if (!isValidVec3(v)) { nanCount++; continue; }
        float speed = glm::length(v);
        float speedRatio = speed / vinf;
        float cp = 1.0f - speedRatio*speedRatio;
        // Учитываем след как в forces.cpp
        float rx = p.x - flowParams.centerX;
        float ry = p.y - flowParams.centerY;
        float rz = p.z - flowParams.centerZ;
        float fl = 1.0f / vinf;
        float along = rx*(flowParams.vx*fl) + ry*(flowParams.vy*fl) + rz*(flowParams.vz*fl);
        float D = 2.0f * fmaxf(flowParams.radiusY, flowParams.radiusZ);
        if (along > D * 0.3f) {
            float w = (along - D*0.3f) / D;
            if (w > 1.0f) w = 1.0f;
            cp -= 0.8f * w * w;
        }
        if (!isValidFloat(cp)) { nanCount++; continue; }
        if (cp > 1.5f || cp < -3.5f) outOfRange++;
    }
    if (nanCount > 0) {
        logTest("  FAIL: pressure Cp produced " + std::to_string(nanCount) + " NaN");
        ok = false;
    }
    if (outOfRange > (int)(g_vertices.size()/3/10) / 2) {
        logTest("  FAIL: too many Cp out of range [-3.5,1.5]: " + std::to_string(outOfRange));
        ok = false;
    }
    // Проверка цветов давления
    if (!g_vertexColors.empty()) {
        for (size_t i = 0; i < g_vertexColors.size(); i++) {
            if (!isValidFloat(g_vertexColors[i]) || g_vertexColors[i] < -0.1f || g_vertexColors[i] > 1.1f) {
                logTest("  FAIL: vertex color out of range or NaN at " + std::to_string(i));
                ok = false;
                break;
            }
        }
    }
    return ok;
}

bool testParticleSystem() {
    logTest("Testing particle system...");
    if (particlePositions.empty()) {
        logTest("  SKIP: no particles");
        return true;
    }
    bool ok = true;
    int nanPos = 0;
    int outOfBounds = 0;
    int nanCol = 0;
    int n = particleDrawCount;
    if (n <= 0) n = numParticles;
    n = std::min(n, (int)(particlePositions.size()/3));
    for (int i = 0; i < n; i++) {
        glm::vec3 p(particlePositions[3*i], particlePositions[3*i+1], particlePositions[3*i+2]);
        if (!isValidVec3(p)) { nanPos++; continue; }
        // Частицы могут быть немного вне границ из-за респавна, но не сильно
        float margin = maxDim * 3.0f;
        if (p.x < flowParams.minX - margin || p.x > flowParams.maxX + margin ||
            p.y < flowParams.minY - margin || p.y > flowParams.maxY + margin ||
            p.z < flowParams.minZ - margin || p.z > flowParams.maxZ + margin) {
            outOfBounds++;
        }
        if (i*3+2 < (int)particleColors.size()) {
            glm::vec3 c(particleColors[3*i], particleColors[3*i+1], particleColors[3*i+2]);
            if (!isValidVec3(c) || c.x < -0.1f || c.x > 1.5f || c.y < -0.1f || c.y > 1.5f || c.z < -0.1f || c.z > 1.5f) {
                nanCol++;
            }
        }
    }
    if (nanPos > 0) {
        logTest("  FAIL: " + std::to_string(nanPos) + " particles with NaN/Inf positions");
        ok = false;
    }
    if (outOfBounds > n/2) {
        logTest("  FAIL: too many particles out of bounds: " + std::to_string(outOfBounds) + "/" + std::to_string(n));
        ok = false;
    }
    if (nanCol > 0) {
        logTest("  FAIL: " + std::to_string(nanCol) + " particles with invalid colors");
        ok = false;
    }
    return ok;
}

bool testForceCalculation() {
    logTest("Testing force calculation...");
    if (g_vertices.empty()) {
        logTest("  SKIP: no model");
        return true;
    }
    bool ok = true;
    if (!isValidFloat(liftMagnitude) || !isValidFloat(dragMagnitude)) {
        logTest("  FAIL: lift/drag NaN: lift=" + std::to_string(liftMagnitude) + " drag=" + std::to_string(dragMagnitude));
        ok = false;
    }
    if (!isValidVec3(liftVector) || !isValidVec3(dragVector)) {
        logTest("  FAIL: lift/drag vector NaN");
        ok = false;
    }
    if (!isValidVec3(centerOfPressure)) {
        logTest("  FAIL: centerOfPressure NaN");
        ok = false;
    }
    // Силы не должны быть астрономическими
    float maxReasonable = maxDim * maxDim * 1000.0f; // эвристика
    if (fabsf(liftMagnitude) > maxReasonable || fabsf(dragMagnitude) > maxReasonable) {
        logTest("  FAIL: forces too large: lift=" + std::to_string(liftMagnitude) + " drag=" + std::to_string(dragMagnitude) + " maxReasonable=" + std::to_string(maxReasonable));
        ok = false;
    }
    return ok;
}

bool testVoxelGrid() {
    logTest("Testing voxel grid...");
    bool ok = true;
    if (g_distanceField.empty()) {
        logTest("  SKIP: no voxel grid");
        return true;
    }
    if (g_voxNx <= 0 || g_voxNy <= 0 || g_voxNz <= 0) {
        logTest("  FAIL: invalid voxel dimensions");
        return false;
    }
    int total = g_voxNx * g_voxNy * g_voxNz;
    if ((int)g_distanceField.size() != total) {
        logTest("  FAIL: distance field size mismatch: " + std::to_string(g_distanceField.size()) + " vs " + std::to_string(total));
        ok = false;
    }
    if ((int)g_voxelData.size() != total) {
        logTest("  FAIL: voxel data size mismatch");
        ok = false;
    }
    // Проверка на NaN в distance field
    int nanCount = 0;
    float minD = std::numeric_limits<float>::max();
    float maxD = std::numeric_limits<float>::lowest();
    for (float d : g_distanceField) {
        if (!isValidFloat(d)) nanCount++;
        else {
            if (d < minD) minD = d;
            if (d > maxD) maxD = d;
        }
    }
    if (nanCount > 0) {
        logTest("  FAIL: " + std::to_string(nanCount) + " NaN in distance field");
        ok = false;
    }
    logTest("  Distance field range: [" + std::to_string(minD) + ", " + std::to_string(maxD) + "]");
    if (minD > 0) {
        logTest("  WARN: distance field has no negative values (no inside?)");
    }
    if (maxD < 0) {
        logTest("  WARN: distance field all negative");
    }
    // Проверка границ воксельной сетки
    if (g_voxMinX >= g_voxMaxX || g_voxMinY >= g_voxMaxY || g_voxMinZ >= g_voxMaxZ) {
        logTest("  FAIL: invalid voxel bounds");
        ok = false;
    }
    return ok;
}

bool testNaNChecks() {
    logTest("Testing for NaN/Inf in globals...");
    bool ok = true;
    // Проверка всех важных глобальных переменных на NaN
    struct Check { const char* name; float val; };
    Check checks[] = {
        {"flowSpeed", flowSpeed},
        {"flowAzimuth", flowAzimuth},
        {"flowElevation", flowElevation},
        {"timeScale", timeScale},
        {"strouhal", strouhal},
        {"wakeStrength", wakeStrength},
        {"wakeLength", wakeLength},
        {"altitude", altitude},
        {"airDensity", airDensity},
        {"airPressure", airPressure},
        {"airTemperature", airTemperature},
        {"maxDim", maxDim},
        {"deltaTime", deltaTime},
    };
    for (auto& c : checks) {
        if (!isValidFloat(c.val)) {
            logTest(std::string("  FAIL: ") + c.name + " is NaN/Inf: " + std::to_string(c.val));
            ok = false;
        }
    }
    if (!isValidVec3(cameraPos) || !isValidVec3(cameraFront) || !isValidVec3(center)) {
        logTest("  FAIL: camera or center NaN");
        ok = false;
    }
    return ok;
}

void runAllTests() {
    auto t0 = std::chrono::high_resolution_clock::now();
    lastTestResults.clear();
    testLog.clear();
    testsPassed = 0;
    testsFailed = 0;

    logTest("=== Starting Aeros Engine Tests v1.3.0 ===");
    logTest("Model: " + std::to_string(modelVertexCount) + " vertices, Voxel: " + std::to_string(g_voxNx) + "x" + std::to_string(g_voxNy) + "x" + std::to_string(g_voxNz));

    struct TestCase {
        const char* name;
        bool (*func)();
    };
    TestCase tests[] = {
        {"Speed Conversion", testSpeedConversion},
        {"ISA Atmosphere", testAtmosphereModel},
        {"SDF Sampling", testSDFSampling},
        {"Velocity Field", testVelocityField},
        {"Pressure Calculation", testPressureCalculation},
        {"Particle System", testParticleSystem},
        {"Force Calculation", testForceCalculation},
        {"Voxel Grid", testVoxelGrid},
        {"NaN Checks", testNaNChecks},
    };

    for (auto& tc : tests) {
        bool passed = false;
        std::string msg = "";
        try {
            passed = tc.func();
        } catch (const std::exception& e) {
            msg = std::string("EXCEPTION: ") + e.what();
            passed = false;
        } catch (...) {
            msg = "UNKNOWN EXCEPTION";
            passed = false;
        }
        TestResult r;
        r.name = tc.name;
        r.passed = passed;
        r.message = msg.empty() ? (passed ? "OK" : "FAILED") : msg;
        lastTestResults.push_back(r);
        if (passed) testsPassed++; else testsFailed++;
        logTest(std::string(tc.name) + ": " + (passed ? "PASS" : "FAIL") + (msg.empty() ? "" : " - " + msg));
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    lastTestTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    logTest("=== Tests finished: " + std::to_string(testsPassed) + " passed, " + std::to_string(testsFailed) + " failed in " + std::to_string(lastTestTimeMs) + " ms ===");

    if (testsFailed > 0) {
        logTest("!!! PHYSICS ERRORS DETECTED !!!");
    } else {
        logTest("All tests passed — physics OK");
    }
}

void validateFrame() {
    if (!testContinuous) return;
    // Быстрая проверка каждый кадр — только критичное
    bool hasError = false;
    if (!isValidFloat(flowSpeed) || !isValidFloat(altitude) || !isValidFloat(airDensity)) hasError = true;
    if (!isValidFloat(liftMagnitude) || !isValidFloat(dragMagnitude)) hasError = true;
    if (hasError) {
        logTest("FRAME VALIDATION FAILED at t=" + std::to_string(flowParams.time));
    }
    // Проверка частиц на NaN — первые 100
    for (int i = 0; i < std::min(100, particleDrawCount); i++) {
        glm::vec3 p(particlePositions[3*i], particlePositions[3*i+1], particlePositions[3*i+2]);
        if (!isValidVec3(p)) {
            logTest("Particle NaN at frame " + std::to_string(flowParams.time) + " idx " + std::to_string(i));
            break;
        }
    }
}
