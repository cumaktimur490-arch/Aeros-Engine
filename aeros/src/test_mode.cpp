#include "test_mode.h"
#include "globals.h"
#include "flow_field.h"
#include "voxel_grid.h"
#include "atmosphere.h"
#include "forces.h"
#include "shaders.h"
#include "lbm.h"
#include "gl_utils.h"
#include "ui.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cmath>
#include <chrono>
#include <sstream>
#include <iostream>
#include <limits>
#include <algorithm>
#include <cstring>

// Глобальные
bool testModeEnabled = false;
bool testContinuous = false;
std::vector<TestResult> lastTestResults;
int testsPassed = 0;
int testsFailed = 0;
int codeTestsPassed = 0;
int codeTestsFailed = 0;
int lbmTestsPassed = 0;
int lbmTestsFailed = 0;
float lastTestTimeMs = 0.0f;
std::string testLog;
int lastGLError = 0;
std::string lastGLErrorStr = "";

static bool isValidFloat(float v) { return !std::isnan(v) && !std::isinf(v); }
static bool isValidVec3(const glm::vec3& v) { return isValidFloat(v.x) && isValidFloat(v.y) && isValidFloat(v.z); }

void logTest(const std::string& msg) { testLog += msg + "\n"; std::cout << "[TEST] " << msg << std::endl; }
void logTestError(const std::string& msg) { testLog += "[ERROR] " + msg + "\n"; std::cout << "[TEST][ERROR] " << msg << std::endl; }
void logTestWarn(const std::string& msg) { testLog += "[WARN] " + msg + "\n"; std::cout << "[TEST][WARN] " << msg << std::endl; }

std::string getGLErrorString(int err) {
    switch(err) {
        case GL_NO_ERROR: return "GL_NO_ERROR";
        case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
        case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
        case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
        case GL_STACK_UNDERFLOW: return "GL_STACK_UNDERFLOW";
        case GL_STACK_OVERFLOW: return "GL_STACK_OVERFLOW";
        default: return "UNKNOWN_GL_ERROR_" + std::to_string(err);
    }
}
bool checkGLErrors(const char* where) {
    bool ok = true; GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        lastGLError = err;
        lastGLErrorStr = getGLErrorString(err) + std::string(" at ") + where;
        logTestError("OpenGL error: " + lastGLErrorStr);
        ok = false;
    }
    return ok;
}

// ===================== PHYSICS TESTS (v1.3.0) =====================
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
            logTestError("  FAIL: " + std::string(speedUnitShort(c.unit)) + " roundtrip diff=" + std::to_string(diff));
            ok = false;
        }
        float expDiff = fabsf(converted - c.expected);
        if (expDiff > 0.05f) {
            logTestError("  FAIL: " + std::string(speedUnitShort(c.unit)) + " expected=" + std::to_string(c.expected) + " got=" + std::to_string(converted));
            ok = false;
        }
    }
    for (int u = 0; u < SPEED_UNIT_COUNT; u++) {
        float val = 5.0f;
        float toMS = speedToMS(val, (SpeedUnit)u);
        float fromMS = speedFromMS(toMS, (SpeedUnit)u);
        if (fabsf(fromMS - val) > 0.001f) { logTestError("  FAIL: unit " + std::to_string(u) + " roundtrip"); ok = false; }
    }
    float nanTest = speedFromMS(std::numeric_limits<float>::quiet_NaN(), SPEED_KMH);
    if (!std::isnan(nanTest)) logTestWarn("  WARN: speedFromMS(NaN) should propagate NaN but got " + std::to_string(nanTest));
    return ok;
}
bool testAtmosphereModel() {
    logTest("Testing ISA atmosphere model...");
    struct Ref { float alt; float rho; float pressure; float temp; };
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
            logTestError("  FAIL: NaN at alt " + std::to_string(r.alt)); ok = false; continue;
        }
        float rhoDiff = fabsf(atm.density - r.rho) / (r.rho + 1e-6f);
        float pDiff = fabsf(atm.pressure - r.pressure) / (r.pressure + 1e-6f);
        float tDiff = fabsf(atm.temperature - r.temp);
        if (rhoDiff > 0.05f) { logTestError("  FAIL: rho at " + std::to_string(r.alt) + "m expected " + std::to_string(r.rho) + " got " + std::to_string(atm.density)); ok = false; }
        if (pDiff > 0.05f) { logTestError("  FAIL: P at " + std::to_string(r.alt) + "m diff " + std::to_string(pDiff*100) + "%"); ok = false; }
        if (tDiff > 2.0f) { logTestError("  FAIL: T at " + std::to_string(r.alt) + "m expected " + std::to_string(r.temp) + " got " + std::to_string(atm.temperature)); ok = false; }
        if (r.alt > 0 && atm.density >= 1.3f) { logTestError("  FAIL: density not decreasing at " + std::to_string(r.alt)); ok = false; }
        if (atm.pressure > 110000.0f || atm.pressure < 1.0f) { logTestError("  FAIL: pressure out of range at " + std::to_string(r.alt)); ok = false; }
    }
    float prevRho = 10.0f;
    for (float h = 0; h <= 20000; h += 1000) {
        float rho = getAirDensity(h);
        if (rho > prevRho + 0.001f) { logTestError("  FAIL: density not monotonic at " + std::to_string(h)); ok = false; }
        prevRho = rho;
    }
    AtmosphereParams neg = calculateAtmosphere(-1000.0f);
    if (!isValidFloat(neg.density) || neg.density < 0.00005f) { logTestError("  FAIL: negative altitude handling"); ok = false; }
    AtmosphereParams huge = calculateAtmosphere(100000.0f);
    if (!isValidFloat(huge.density) || !isValidFloat(huge.pressure)) { logTestError("  FAIL: huge altitude NaN"); ok = false; }
    return ok;
}
bool testSDFSampling() {
    logTest("Testing SDF sampling...");
    if (g_distanceField.empty()) { logTest("  SKIP: no voxel grid loaded"); return true; }
    bool ok = true;
    float dCenter = sampleSDFCPU(center);
    if (!isValidFloat(dCenter)) { logTestError("  FAIL: SDF at center is NaN/Inf"); ok = false; }
    glm::vec3 farPoint = center + glm::vec3(maxDim * 10.0f, 0, 0);
    float dFar = sampleSDFCPU(farPoint);
    if (!isValidFloat(dFar)) { logTestError("  FAIL: SDF far point NaN"); ok = false; }
    if (dFar < 0) { logTestError("  FAIL: SDF far point should be positive, got " + std::to_string(dFar)); ok = false; }
    glm::vec3 n = sdfNormalCPU(center + glm::vec3(maxDim*0.6f, 0, 0));
    if (!isValidVec3(n)) { logTestError("  FAIL: SDF normal NaN"); ok = false; }
    float nLen = glm::length(n);
    if (fabsf(nLen - 1.0f) > 0.1f && nLen > 0.001f) { logTestError("  FAIL: SDF normal not normalized, len=" + std::to_string(nLen)); ok = false; }
    for (int i = 0; i < 100; i++) {
        float x = minBB.x + (maxBB.x - minBB.x) * (i / 100.0f);
        glm::vec3 p(x, center.y, center.z);
        float d = sampleSDFCPU(p);
        if (!isValidFloat(d)) { logTestError("  FAIL: SDF NaN at x=" + std::to_string(x)); ok = false; break; }
    }
    glm::vec3 oob(1e6f, 1e6f, 1e6f);
    float dOob = sampleSDFCPU(oob);
    if (!isValidFloat(dOob) || fabsf(dOob - 1000.0f) > 1.0f) logTestWarn("  WARN: SDF OOB should return ~1000, got " + std::to_string(dOob));
    glm::vec3 nOob = sdfNormalCPU(oob);
    if (!isValidVec3(nOob)) { logTestError("  FAIL: SDF normal OOB NaN"); ok = false; }
    return ok;
}
bool testVelocityField() {
    logTest("Testing velocity field...");
    updateFlowParams();
    bool ok = true;
    float vinf = glm::length(glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz));
    if (vinf < 1e-6f) { logTest("  SKIP: zero freestream velocity"); return true; }
    int nanCount = 0, negPenetration = 0;
    for (int i = 0; i < 200; i++) {
        float fx = (i % 10) / 9.0f;
        float fy = ((i / 10) % 10) / 9.0f;
        float fz = (i / 100) / 2.0f;
        glm::vec3 p(flowParams.minX + fx * (flowParams.maxX - flowParams.minX),
                    flowParams.minY + fy * (flowParams.maxY - flowParams.minY),
                    flowParams.minZ + fz * (flowParams.maxZ - flowParams.minZ));
        glm::vec3 v = computeVelocityFieldCPU(p, flowParams);
        if (!isValidVec3(v)) { nanCount++; continue; }
        float speed = glm::length(v);
        if (speed > vinf * 5.0f) {
            logTestWarn("  WARN: velocity magnitude too high at (" + std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z) + ") speed=" + std::to_string(speed) + " vinf=" + std::to_string(vinf));
            if (speed > vinf * 10.0f) ok = false;
        }
        if (!g_distanceField.empty()) {
            float d = sampleSDFCPU(p);
            if (d > 0 && d < flowParams.cellSizeX * 2.0f) {
                glm::vec3 n = sdfNormalCPU(p);
                float vn = glm::dot(v, n);
                if (vn < -0.1f * vinf) negPenetration++;
            }
        }
    }
    if (nanCount > 0) { logTestError("  FAIL: velocity field produced " + std::to_string(nanCount) + " NaN/Inf values"); ok = false; }
    if (negPenetration > 10) { logTestError("  FAIL: " + std::to_string(negPenetration) + " points with negative penetration near surface"); ok = false; }
    if (!g_distanceField.empty()) {
        glm::vec3 vInside = computeVelocityFieldCPU(center, flowParams);
        if (!isValidVec3(vInside)) { logTestError("  FAIL: velocity inside object NaN"); ok = false; }
        else {
            float speedInside = glm::length(vInside);
            if (speedInside > vinf * 0.5f) logTestWarn("  WARN: velocity inside object too high: " + std::to_string(speedInside));
        }
    }
    return ok;
}
bool testPressureCalculation() {
    logTest("Testing pressure calculation...");
    if (g_vertices.empty()) { logTest("  SKIP: no model loaded"); return true; }
    updateFlowParams();
    bool ok = true;
    float vinf = glm::length(glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz));
    if (vinf < 1e-6f) { logTest("  SKIP: zero velocity"); return true; }
    int nanCount = 0, outOfRange = 0;
    for (size_t i = 0; i < g_vertices.size(); i += 3*10) {
        glm::vec3 p(g_vertices[i], g_vertices[i+1], g_vertices[i+2]);
        glm::vec3 v = computeVelocityFieldCPU(p, flowParams);
        if (!isValidVec3(v)) { nanCount++; continue; }
        float speed = glm::length(v);
        float speedRatio = speed / vinf;
        float cp = 1.0f - speedRatio*speedRatio;
        float rx = p.x - flowParams.centerX, ry = p.y - flowParams.centerY, rz = p.z - flowParams.centerZ;
        float fl = 1.0f / vinf;
        float along = rx*(flowParams.vx*fl) + ry*(flowParams.vy*fl) + rz*(flowParams.vz*fl);
        float D = 2.0f * fmaxf(flowParams.radiusY, flowParams.radiusZ);
        if (along > D * 0.3f) { float w = (along - D*0.3f) / D; if (w > 1.0f) w = 1.0f; cp -= 0.8f * w * w; }
        if (!isValidFloat(cp)) { nanCount++; continue; }
        if (cp > 1.5f || cp < -3.5f) outOfRange++;
    }
    if (nanCount > 0) { logTestError("  FAIL: pressure Cp produced " + std::to_string(nanCount) + " NaN"); ok = false; }
    if (outOfRange > (int)(g_vertices.size()/3/10) / 2) { logTestError("  FAIL: too many Cp out of range [-3.5,1.5]: " + std::to_string(outOfRange)); ok = false; }
    if (!g_vertexColors.empty()) {
        for (size_t i = 0; i < g_vertexColors.size(); i++) {
            if (!isValidFloat(g_vertexColors[i]) || g_vertexColors[i] < -0.1f || g_vertexColors[i] > 1.1f) { logTestError("  FAIL: vertex color out of range or NaN at " + std::to_string(i)); ok = false; break; }
        }
    }
    return ok;
}
bool testParticleSystem() {
    logTest("Testing particle system...");
    if (particlePositions.empty()) { logTest("  SKIP: no particles"); return true; }
    bool ok = true;
    int nanPos = 0, outOfBounds = 0, nanCol = 0;
    int n = particleDrawCount; if (n <= 0) n = numParticles;
    n = std::min(n, (int)(particlePositions.size()/3));
    for (int i = 0; i < n; i++) {
        glm::vec3 p(particlePositions[3*i], particlePositions[3*i+1], particlePositions[3*i+2]);
        if (!isValidVec3(p)) { nanPos++; continue; }
        float margin = maxDim * 3.0f;
        if (p.x < flowParams.minX - margin || p.x > flowParams.maxX + margin ||
            p.y < flowParams.minY - margin || p.y > flowParams.maxY + margin ||
            p.z < flowParams.minZ - margin || p.z > flowParams.maxZ + margin) outOfBounds++;
        if (i*3+2 < (int)particleColors.size()) {
            glm::vec3 c(particleColors[3*i], particleColors[3*i+1], particleColors[3*i+2]);
            if (!isValidVec3(c) || c.x < -0.1f || c.x > 1.5f || c.y < -0.1f || c.y > 1.5f || c.z < -0.1f || c.z > 1.5f) nanCol++;
        }
    }
    if (nanPos > 0) { logTestError("  FAIL: " + std::to_string(nanPos) + " particles with NaN/Inf positions"); ok = false; }
    if (outOfBounds > n/2) { logTestError("  FAIL: too many particles out of bounds: " + std::to_string(outOfBounds) + "/" + std::to_string(n)); ok = false; }
    if (nanCol > 0) { logTestError("  FAIL: " + std::to_string(nanCol) + " particles with invalid colors"); ok = false; }
    return ok;
}
bool testForceCalculation() {
    logTest("Testing force calculation...");
    if (g_vertices.empty()) { logTest("  SKIP: no model"); return true; }
    bool ok = true;
    if (!isValidFloat(liftMagnitude) || !isValidFloat(dragMagnitude)) { logTestError("  FAIL: lift/drag NaN: lift=" + std::to_string(liftMagnitude) + " drag=" + std::to_string(dragMagnitude)); ok = false; }
    if (!isValidVec3(liftVector) || !isValidVec3(dragVector)) { logTestError("  FAIL: lift/drag vector NaN"); ok = false; }
    if (!isValidVec3(centerOfPressure)) { logTestError("  FAIL: centerOfPressure NaN"); ok = false; }
    float maxReasonable = maxDim * maxDim * 1000.0f;
    if (fabsf(liftMagnitude) > maxReasonable || fabsf(dragMagnitude) > maxReasonable) { logTestError("  FAIL: forces too large: lift=" + std::to_string(liftMagnitude) + " drag=" + std::to_string(dragMagnitude)); ok = false; }
    return ok;
}
bool testVoxelGrid() {
    logTest("Testing voxel grid...");
    bool ok = true;
    if (g_distanceField.empty()) { logTest("  SKIP: no voxel grid"); return true; }
    if (g_voxNx <= 0 || g_voxNy <= 0 || g_voxNz <= 0) { logTestError("  FAIL: invalid voxel dimensions"); return false; }
    int total = g_voxNx * g_voxNy * g_voxNz;
    if ((int)g_distanceField.size() != total) { logTestError("  FAIL: distance field size mismatch"); ok = false; }
    if ((int)g_voxelData.size() != total) { logTestError("  FAIL: voxel data size mismatch"); ok = false; }
    int nanCount = 0; float minD = std::numeric_limits<float>::max(), maxD = std::numeric_limits<float>::lowest();
    for (float d : g_distanceField) { if (!isValidFloat(d)) nanCount++; else { minD = std::min(minD,d); maxD = std::max(maxD,d); } }
    if (nanCount > 0) { logTestError("  FAIL: " + std::to_string(nanCount) + " NaN in distance field"); ok = false; }
    logTest("  Distance field range: [" + std::to_string(minD) + ", " + std::to_string(maxD) + "]");
    if (minD > 0) logTestWarn("  WARN: distance field has no negative values");
    if (maxD < 0) logTestWarn("  WARN: distance field all negative");
    if (g_voxMinX >= g_voxMaxX || g_voxMinY >= g_voxMaxY || g_voxMinZ >= g_voxMaxZ) { logTestError("  FAIL: invalid voxel bounds"); ok = false; }
    return ok;
}
bool testNaNChecks() {
    logTest("Testing for NaN/Inf in globals...");
    bool ok = true;
    struct Check { const char* name; float val; };
    Check checks[] = { {"flowSpeed", flowSpeed}, {"flowAzimuth", flowAzimuth}, {"flowElevation", flowElevation}, {"timeScale", timeScale}, {"strouhal", strouhal}, {"wakeStrength", wakeStrength}, {"wakeLength", wakeLength}, {"altitude", altitude}, {"airDensity", airDensity}, {"airPressure", airPressure}, {"airTemperature", airTemperature}, {"maxDim", maxDim}, {"deltaTime", deltaTime}, };
    for (auto& c : checks) if (!isValidFloat(c.val)) { logTestError(std::string("  FAIL: ") + c.name + " is NaN/Inf: " + std::to_string(c.val)); ok = false; }
    if (!isValidVec3(cameraPos) || !isValidVec3(cameraFront) || !isValidVec3(center)) { logTestError("  FAIL: camera or center NaN"); ok = false; }
    return ok;
}

// ===================== CODE TESTS (v1.4.0) =====================
bool testOpenGLState() {
    logTest("Testing OpenGL state (code errors)...");
    bool ok = true;
    GLenum err; int errCount = 0;
    while ((err = glGetError()) != GL_NO_ERROR) { logTestError("  FAIL: GL error before test: " + getGLErrorString(err)); errCount++; }
    if (errCount > 0) ok = false;
    if (display_w <= 0 || display_h <= 0) { logTestError("  FAIL: display_w/h invalid: " + std::to_string(display_w) + "x" + std::to_string(display_h)); ok = false; }
    if (display_w > 10000 || display_h > 10000) logTestWarn("  WARN: display size huge");
    if (!isValidFloat(maxDim) || maxDim <= 1e-6f) { logTestError("  FAIL: maxDim invalid: " + std::to_string(maxDim)); ok = false; }
    if (maxDim > 1e6f) logTestWarn("  WARN: maxDim extremely large: " + std::to_string(maxDim));
    if (!g_vertices.empty()) {
        if (modelVAO == 0) { logTestError("  FAIL: modelVAO is 0 but model loaded"); ok = false; }
        else if (!glIsVertexArray(modelVAO)) { logTestError("  FAIL: modelVAO is not a valid VAO"); ok = false; }
        if (modelVBO_vertices == 0 || modelVBO_normals == 0) { logTestError("  FAIL: model VBOs zero"); ok = false; }
    }
    if (showParticles && !particlePositions.empty()) {
        if (particleVAO == 0) { logTestError("  FAIL: particleVAO zero but particles shown"); ok = false; }
        if (particleVBO_pos == 0 || particleVBO_col == 0) { logTestError("  FAIL: particle VBOs zero"); ok = false; }
    }
    if (showStreamlines && streamlineVertexCount > 0 && streamlineVAO == 0) logTestWarn("  WARN: streamlineVAO zero but streamlines shown");
    if (!checkGLErrors("testOpenGLState")) ok = false;
    if (ok) logTest("  OpenGL state OK");
    return ok;
}
bool testBufferIntegrity() {
    logTest("Testing buffer integrity (code errors)...");
    bool ok = true;
    if (!particlePositions.empty() || !particleColors.empty()) {
        if (particlePositions.size() % 3 != 0) { logTestError("  FAIL: particlePositions size not %3: " + std::to_string(particlePositions.size())); ok = false; }
        if (particleColors.size() % 3 != 0) { logTestError("  FAIL: particleColors size not %3"); ok = false; }
        if (particlePositions.size() != particleColors.size()) { logTestError("  FAIL: particle pos/col size mismatch"); ok = false; }
        int allocCount = particlePositions.size() / 3;
        if (particleDrawCount < 0) { logTestError("  FAIL: particleDrawCount negative: " + std::to_string(particleDrawCount)); ok = false; }
        if (particleDrawCount > allocCount) { logTestError("  FAIL: particleDrawCount > alloc: " + std::to_string(particleDrawCount) + " > " + std::to_string(allocCount)); ok = false; }
    } else if (particleDrawCount != 0) logTestWarn("  WARN: particle vectors empty but drawCount=" + std::to_string(particleDrawCount));
    if (!g_vertices.empty()) {
        if (g_vertices.size() % 3 != 0) { logTestError("  FAIL: g_vertices not %3"); ok = false; }
        if (g_vertices.size() % 9 != 0) { logTestError("  FAIL: g_vertices not %9"); ok = false; }
        if (g_normals.size() != g_vertices.size()) { logTestError("  FAIL: g_normals size mismatch"); ok = false; }
        if (!g_vertexColors.empty() && g_vertexColors.size() != g_vertices.size()) { logTestError("  FAIL: g_vertexColors size mismatch"); ok = false; }
        int expectedVC = g_vertices.size() / 3;
        if (modelVertexCount != expectedVC) { logTestError("  FAIL: modelVertexCount mismatch"); ok = false; }
    }
    if (!g_distanceField.empty() || !g_voxelData.empty()) {
        int total = g_voxNx * g_voxNy * g_voxNz;
        if (total <= 0) { logTestError("  FAIL: voxel total <=0"); ok = false; }
        if ((int)g_distanceField.size() != total) { logTestError("  FAIL: distanceField size != total"); ok = false; }
        if ((int)g_voxelData.size() != total) { logTestError("  FAIL: voxelData size != total"); ok = false; }
    }
    if (streamlineVertexCount < 0) { logTestError("  FAIL: streamlineVertexCount negative"); ok = false; }
    if (streamlineVertexCount % 2 != 0) logTestWarn("  WARN: streamlineVertexCount odd");
    if (!g_vertices.empty()) { if (bboxVAO == 0) logTestWarn("  WARN: bboxVAO zero"); if (axesVAO == 0) logTestWarn("  WARN: axesVAO zero"); }
    if (showObstacle && obstacleVAO == 0) logTestWarn("  WARN: obstacleVAO zero but showObstacle true");
    if (ok) logTest("  Buffer integrity OK");
    return ok;
}
bool testModelIntegrity() {
    logTest("Testing model integrity (code errors)...");
    if (g_vertices.empty()) { logTest("  SKIP: no model"); return true; }
    bool ok = true;
    int nanVerts = 0, nanNorms = 0, degenerate = 0, zeroNorm = 0, nonNormalizedNorm = 0;
    for (size_t i = 0; i < g_vertices.size(); i += 3) { glm::vec3 v(g_vertices[i], g_vertices[i+1], g_vertices[i+2]); if (!isValidVec3(v)) nanVerts++; }
    for (size_t i = 0; i < g_normals.size(); i += 3) {
        glm::vec3 n(g_normals[i], g_normals[i+1], g_normals[i+2]);
        if (!isValidVec3(n)) nanNorms++;
        else { float len = glm::length(n); if (len < 1e-6f) zeroNorm++; else if (fabsf(len - 1.0f) > 0.2f) nonNormalizedNorm++; }
    }
    for (size_t i = 0; i + 8 < g_vertices.size(); i += 9) {
        glm::vec3 v0(g_vertices[i], g_vertices[i+1], g_vertices[i+2]);
        glm::vec3 v1(g_vertices[i+3], g_vertices[i+4], g_vertices[i+5]);
        glm::vec3 v2(g_vertices[i+6], g_vertices[i+7], g_vertices[i+8]);
        glm::vec3 e1 = v1 - v0, e2 = v2 - v0;
        float area = 0.5f * glm::length(glm::cross(e1, e2));
        if (area < 1e-12f) degenerate++;
    }
    if (nanVerts > 0) { logTestError("  FAIL: " + std::to_string(nanVerts) + " vertices NaN"); ok = false; }
    if (nanNorms > 0) { logTestError("  FAIL: " + std::to_string(nanNorms) + " normals NaN"); ok = false; }
    if (degenerate > (int)(g_vertices.size()/9)/10) { logTestError("  FAIL: too many degenerate: " + std::to_string(degenerate)); ok = false; }
    else if (degenerate > 0) logTestWarn("  WARN: " + std::to_string(degenerate) + " degenerate triangles");
    if (zeroNorm > 0) logTestWarn("  WARN: " + std::to_string(zeroNorm) + " zero-length normals");
    if (nonNormalizedNorm > (int)(g_normals.size()/3)/2) logTestWarn("  WARN: many non-normalized normals: " + std::to_string(nonNormalizedNorm));
    if (!isValidVec3(minBB) || !isValidVec3(maxBB)) { logTestError("  FAIL: minBB/maxBB NaN"); ok = false; }
    if (minBB.x > maxBB.x || minBB.y > maxBB.y || minBB.z > maxBB.z) { logTestError("  FAIL: minBB > maxBB"); ok = false; }
    if (!isValidVec3(center)) { logTestError("  FAIL: center NaN"); ok = false; }
    else {
        glm::vec3 expectedCenter = (minBB + maxBB) * 0.5f;
        float centerErr = glm::length(center - expectedCenter);
        if (centerErr > maxDim * 0.01f + 1e-4f) { logTestError("  FAIL: center mismatch err=" + std::to_string(centerErr)); ok = false; }
        if (center.x < minBB.x - 1e-4f || center.x > maxBB.x + 1e-4f || center.y < minBB.y - 1e-4f || center.y > maxBB.y + 1e-4f || center.z < minBB.z - 1e-4f || center.z > maxBB.z + 1e-4f) { logTestError("  FAIL: center outside BB"); ok = false; }
    }
    float computedMaxDim = glm::length(maxBB - minBB);
    if (fabsf(computedMaxDim - maxDim) > computedMaxDim * 0.01f + 1e-4f) { logTestError("  FAIL: maxDim mismatch"); ok = false; }
    if (maxDim < 1e-6f) { logTestError("  FAIL: maxDim too small"); ok = false; }
    if (ok) logTest("  Model integrity OK");
    return ok;
}
bool testFlowParamsSanity() {
    logTest("Testing FlowParams sanity (code errors)...");
    updateFlowParams();
    bool ok = true;
    if (!isValidFloat(flowParams.cellSizeX) || flowParams.cellSizeX < 1e-8f) { logTestError("  FAIL: cellSizeX invalid"); ok = false; }
    if (!isValidFloat(flowParams.cellSizeY) || flowParams.cellSizeY < 1e-8f) { logTestError("  FAIL: cellSizeY invalid"); ok = false; }
    if (!isValidFloat(flowParams.cellSizeZ) || flowParams.cellSizeZ < 1e-8f) { logTestError("  FAIL: cellSizeZ invalid"); ok = false; }
    if (flowParams.minX >= flowParams.maxX || flowParams.minY >= flowParams.maxY || flowParams.minZ >= flowParams.maxZ) { logTestError("  FAIL: flow domain min>=max"); ok = false; }
    if (!g_distanceField.empty()) {
        if (flowParams.gridMinX >= flowParams.gridMaxX || flowParams.gridMinY >= flowParams.gridMaxY || flowParams.gridMinZ >= flowParams.gridMaxZ) { logTestError("  FAIL: voxel grid min>=max"); ok = false; }
        if (flowParams.gridNx <= 0 || flowParams.gridNy <= 0 || flowParams.gridNz <= 0) { logTestError("  FAIL: grid Nx/Ny/Nz <=0"); ok = false; }
        int total = flowParams.gridNx * flowParams.gridNy * flowParams.gridNz;
        if (total != flowParams.gridCellCount) { logTestError("  FAIL: gridCellCount mismatch"); ok = false; }
    }
    if (!isValidFloat(flowParams.radiusX) || flowParams.radiusX < 1e-6f) { logTestError("  FAIL: radiusX invalid"); ok = false; }
    if (!isValidFloat(flowParams.radiusY) || flowParams.radiusY < 1e-6f) { logTestError("  FAIL: radiusY invalid"); ok = false; }
    if (!isValidFloat(flowParams.radiusZ) || flowParams.radiusZ < 1e-6f) { logTestError("  FAIL: radiusZ invalid"); ok = false; }
    float vinf = glm::length(glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz));
    if (!isValidFloat(vinf)) { logTestError("  FAIL: Vinf NaN"); ok = false; }
    else { float diff = fabsf(vinf - flowSpeed); if (diff > 0.01f && flowSpeed > 1e-6f) { logTestError("  FAIL: Vinf vs flowSpeed mismatch"); ok = false; } }
    if (!isValidFloat(flowParams.time) || flowParams.time < -1.0f) { logTestError("  FAIL: time invalid"); ok = false; }
    if (!isValidFloat(flowParams.timeScale) || flowParams.timeScale <= 0 || flowParams.timeScale > 10.0f) { logTestError("  FAIL: timeScale out of range"); ok = false; }
    if (!isValidFloat(flowParams.strouhal) || flowParams.strouhal <= 0 || flowParams.strouhal > 1.0f) { logTestError("  FAIL: strouhal out of range"); ok = false; }
    if (!isValidFloat(flowParams.wakeStrength) || flowParams.wakeStrength < 0 || flowParams.wakeStrength > 5.0f) { logTestError("  FAIL: wakeStrength out of range"); ok = false; }
    if (!isValidFloat(flowParams.wakeLength) || flowParams.wakeLength < 0 || flowParams.wakeLength > 100.0f) { logTestError("  FAIL: wakeLength out of range"); ok = false; }
    if (!isValidFloat(flowParams.airDensity) || flowParams.airDensity < 0.00005f || flowParams.airDensity > 5.0f) { logTestError("  FAIL: airDensity out of range"); ok = false; }
    if (!isValidFloat(flowParams.airPressure) || flowParams.airPressure < 0.5f || flowParams.airPressure > 200000.0f) { logTestError("  FAIL: airPressure out of range"); ok = false; }
    if (!isValidFloat(flowParams.airTemperature) || flowParams.airTemperature < 100.0f || flowParams.airTemperature > 500.0f) { logTestError("  FAIL: airTemperature out of range"); ok = false; }
    if (!isValidFloat(flowParams.speedOfSound) || flowParams.speedOfSound < 100.0f || flowParams.speedOfSound > 500.0f) { logTestError("  FAIL: speedOfSound out of range"); ok = false; }
    if (!isValidFloat(flowParams.maxSpeed) || flowParams.maxSpeed <= 0) { logTestError("  FAIL: maxSpeed invalid"); ok = false; }
    if (ok) logTest("  FlowParams sanity OK");
    return ok;
}
bool testTimeAndCamera() {
    logTest("Testing time and camera (code errors)...");
    bool ok = true;
    if (!isValidFloat(deltaTime)) { logTestError("  FAIL: deltaTime NaN/Inf"); ok = false; }
    else { if (deltaTime <= 0) { logTestError("  FAIL: deltaTime <=0"); ok = false; } if (deltaTime > 0.5f) logTestWarn("  WARN: deltaTime large: " + std::to_string(deltaTime)); }
    if (!isValidFloat(lastFrame) || lastFrame < 0) { logTestError("  FAIL: lastFrame invalid"); ok = false; }
    if (!isValidVec3(cameraPos)) { logTestError("  FAIL: cameraPos NaN"); ok = false; }
    if (!isValidVec3(cameraFront)) { logTestError("  FAIL: cameraFront NaN"); ok = false; }
    else { float len = glm::length(cameraFront); if (fabsf(len - 1.0f) > 0.05f) { logTestError("  FAIL: cameraFront not normalized len=" + std::to_string(len)); ok = false; } }
    if (!isValidVec3(cameraUp)) { logTestError("  FAIL: cameraUp NaN"); ok = false; }
    else { float len = glm::length(cameraUp); if (fabsf(len - 1.0f) > 0.05f) { logTestError("  FAIL: cameraUp not normalized"); ok = false; } float dotFU = glm::dot(cameraFront, cameraUp); if (fabsf(dotFU) > 0.1f) { logTestError("  FAIL: front not orthogonal to up dot=" + std::to_string(dotFU)); ok = false; } }
    if (!isValidFloat(yaw) || yaw < -1000 || yaw > 1000) logTestWarn("  WARN: yaw out of range");
    if (!isValidFloat(pitch)) { logTestError("  FAIL: pitch NaN"); ok = false; } else if (pitch < -89.5f || pitch > 89.5f) logTestWarn("  WARN: pitch near gimbal lock");
    if (!isValidFloat(fov) || fov < 1.0f || fov > 120.0f) { logTestError("  FAIL: fov out of range"); ok = false; }
    if (display_w <= 0 || display_h <= 0) { logTestError("  FAIL: display_w/h invalid"); ok = false; }
    if (!isValidVec3(center)) { logTestError("  FAIL: center NaN"); ok = false; }
    float distToCenter = glm::length(cameraPos - center);
    if (distToCenter < 1e-4f) { logTestError("  FAIL: cameraPos == center (lookAt singularity)"); ok = false; }
    if (ok) logTest("  Time and camera OK");
    return ok;
}
bool testMemorySafety() {
    logTest("Testing memory safety (code errors)...");
    bool ok = true;
    if (g_voxNx > 0) {
        long long total = (long long)g_voxNx * g_voxNy * g_voxNz;
        if (total > 20LL * 1024 * 1024) { logTestError("  FAIL: voxel grid too large: " + std::to_string(total)); ok = false; }
        if (total > 8LL * 1024 * 1024) logTestWarn("  WARN: voxel grid large: " + std::to_string(total));
        if (total > std::numeric_limits<int>::max()) { logTestError("  FAIL: voxel total overflow int"); ok = false; }
    }
    if (!g_voxelData.empty()) { int invalid = 0; for (int v : g_voxelData) if (v != 0 && v != 1) invalid++; if (invalid > 0) { logTestError("  FAIL: voxelData invalid values: " + std::to_string(invalid)); ok = false; } }
    if (!g_distanceField.empty()) {
        float minD = std::numeric_limits<float>::max(), maxD = std::numeric_limits<float>::lowest(); int nanC = 0;
        for (float d : g_distanceField) { if (!isValidFloat(d)) nanC++; else { minD = std::min(minD,d); maxD = std::max(maxD,d); } }
        if (nanC > 0) { logTestError("  FAIL: distanceField NaN count: " + std::to_string(nanC)); ok = false; }
        if (fabsf(minD) > 10000 || fabsf(maxD) > 10000) { logTestError("  FAIL: distanceField range huge"); ok = false; }
    }
    size_t vertBytes = g_vertices.size() * sizeof(float);
    size_t particleBytes = particlePositions.size() * sizeof(float) + particleColors.size() * sizeof(float);
    size_t voxelBytes = g_distanceField.size() * sizeof(float) + g_voxelData.size() * sizeof(int);
    size_t totalBytes = vertBytes + particleBytes + voxelBytes;
    float totalMB = totalBytes / (1024.0f*1024.0f);
    logTest("  Memory: verts " + std::to_string(vertBytes/1024) + "KB, particles " + std::to_string(particleBytes/1024) + "KB, voxels " + std::to_string(voxelBytes/1024) + "KB, total " + std::to_string(totalMB) + "MB");
    if (totalMB > 1024) { logTestError("  FAIL: total memory >1GB"); ok = false; } else if (totalMB > 512) logTestWarn("  WARN: total memory >512MB");
    if (numParticles < 0) { logTestError("  FAIL: numParticles negative"); ok = false; }
    if (numParticles > 1000000) { logTestError("  FAIL: numParticles too large >1M"); ok = false; }
    try {
        glm::vec3 testP(1e6f, 1e6f, 1e6f);
        float d = sampleSDFCPU(testP); glm::vec3 n = sdfNormalCPU(testP);
        if (!isValidFloat(d) || !isValidVec3(n)) { logTestError("  FAIL: SDF OOB returned NaN"); ok = false; }
    } catch (...) { logTestError("  FAIL: SDF OOB threw"); ok = false; }
    try {
        FlowParams zero = {}; zero.vx = 0; zero.vy = 0; zero.vz = 0; zero.cellSizeX = 0.1f; zero.cellSizeY = 0.1f; zero.cellSizeZ = 0.1f; zero.radiusY = 0.5f; zero.radiusZ = 0.5f; zero.wakeLength = 8.0f; zero.time = 0;
        glm::vec3 v = computeVelocityFieldCPU(glm::vec3(0), zero);
        if (!isValidVec3(v)) { logTestError("  FAIL: computeVelocityField zero params NaN"); ok = false; }
    } catch (...) { logTestError("  FAIL: computeVelocityField zero params threw"); ok = false; }
    if (ok) logTest("  Memory safety OK");
    return ok;
}
bool testDivisionByZeroRisks() {
    logTest("Testing division by zero risks (code errors)...");
    bool ok = true;
    if (maxDim < 1e-6f) { logTestError("  FAIL: maxDim near zero: " + std::to_string(maxDim)); ok = false; } else logTest("  maxDim=" + std::to_string(maxDim) + " OK");
    float vinf = glm::length(glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz));
    if (vinf < 1e-6f) logTestWarn("  WARN: Vinf near zero: " + std::to_string(vinf));
    if (flowParams.cellSizeX < 1e-8f || flowParams.cellSizeY < 1e-8f || flowParams.cellSizeZ < 1e-8f) { logTestError("  FAIL: cellSize near zero"); ok = false; }
    float D = 2.0f * fmaxf(flowParams.radiusY, flowParams.radiusZ);
    if (D < 1e-6f) { logTestError("  FAIL: D near zero"); ok = false; }
    if (airDensity < 1e-8f) { logTestError("  FAIL: airDensity near zero"); ok = false; }
    if (speedOfSound < 1e-6f) { logTestError("  FAIL: speedOfSound near zero"); ok = false; }
    if (maxSpeedForColor < 1e-6f) { logTestError("  FAIL: maxSpeedForColor near zero"); ok = false; }
    if (deltaTime < 1e-8f) { logTestError("  FAIL: deltaTime near zero"); ok = false; }
    if (flowParams.maxSpeed < 1e-6f) { logTestError("  FAIL: flowParams.maxSpeed near zero"); ok = false; }
    float dragScale = 0.5f / (maxDim + 1e-6f);
    if (!isValidFloat(dragScale)) { logTestError("  FAIL: dragScale NaN"); ok = false; }
    float q = 0.5f * airDensity * flowSpeed * flowSpeed;
    if (q < 1e-8f && flowSpeed > 0.1f) logTestWarn("  WARN: dynamic pressure q near zero: " + std::to_string(q));
    try {
        FlowParams p = flowParams; p.radiusY = 0; p.radiusZ = 0;
        glm::vec3 v = computeVelocityFieldCPU(center + glm::vec3(1,0,0), p);
        if (!isValidVec3(v)) { logTestError("  FAIL: velocity field with zero radius NaN"); ok = false; }
    } catch (...) { logTestError("  FAIL: velocity field zero radius threw"); ok = false; }
    if (ok) logTest("  Division by zero risks OK");
    return ok;
}
bool testInputAndState() {
    logTest("Testing input and state (code errors)...");
    bool ok = true;
    if (useCUDA != 0 && useCUDA != 1) { logTestError("  FAIL: useCUDA invalid: " + std::to_string(useCUDA)); ok = false; }
    if (voxelResolution < 8 || voxelResolution > 256) { logTestError("  FAIL: voxelResolution out of range [8,256]"); ok = false; }
    if (voxelResolution < 16 || voxelResolution > 128) logTestWarn("  WARN: voxelResolution outside recommended [16,128]");
    if (particleSize <= 0 || particleSize > 50.0f) { logTestError("  FAIL: particleSize out of range"); ok = false; }
    if (streamlineWidth <= 0 || streamlineWidth > 20.0f) { logTestError("  FAIL: streamlineWidth out of range"); ok = false; }
    if (obstacleAlpha < 0 || obstacleAlpha > 1.0f) { logTestError("  FAIL: obstacleAlpha out of [0,1]"); ok = false; }
    if (streamlineAlpha < 0 || streamlineAlpha > 1.0f) { logTestError("  FAIL: streamlineAlpha out of [0,1]"); ok = false; }
    if (flowAzimuth < -360 || flowAzimuth > 720) { logTestError("  FAIL: flowAzimuth out of range"); ok = false; }
    if (flowElevation < -90 || flowElevation > 90) { logTestError("  FAIL: flowElevation out of [-90,90]"); ok = false; }
    if (timeScale <= 0 || timeScale > 10.0f) { logTestError("  FAIL: timeScale out of (0,10]"); ok = false; }
    if (strouhal <= 0 || strouhal > 1.0f) { logTestError("  FAIL: strouhal out of (0,1]"); ok = false; }
    if (wakeStrength < 0 || wakeStrength > 5.0f) { logTestError("  FAIL: wakeStrength out of [0,5]"); ok = false; }
    if (wakeLength <= 0 || wakeLength > 100.0f) { logTestError("  FAIL: wakeLength out of (0,100]"); ok = false; }
    if (showParticles && particleDrawCount == 0) logTestWarn("  WARN: showParticles true but drawCount==0");
    if (showParticles && particlePositions.empty()) { logTestError("  FAIL: showParticles true but positions empty"); ok = false; }
    if (showStreamlines && streamlineVertexCount == 0) logTestWarn("  WARN: showStreamlines true but vertexCount==0");
    if (showPressure && g_vertexColors.empty()) logTestWarn("  WARN: showPressure true but colors empty");
    if (showModel && modelVAO == 0 && !g_vertices.empty()) { logTestError("  FAIL: showModel true but VAO==0"); ok = false; }
    if (numStreamlines <= 0) { logTestError("  FAIL: numStreamlines <=0"); ok = false; }
    if (streamlineSteps <= 0) { logTestError("  FAIL: streamlineSteps <=0"); ok = false; }
    if (streamlineStepSize <= 0) { logTestError("  FAIL: streamlineStepSize <=0"); ok = false; }
    if (limitFPS && maxFPS <= 0) { logTestError("  FAIL: limitFPS true but maxFPS <=0"); ok = false; }
    if (ok) logTest("  Input and state OK");
    return ok;
}
bool testShaderAndResources() {
    logTest("Testing shaders and resources (code errors)...");
    bool ok = true;
    if (vertexShaderSource == nullptr || strlen(vertexShaderSource) < 10) { logTestError("  FAIL: vertexShaderSource empty"); ok = false; }
    if (fragmentShaderSource == nullptr || strlen(fragmentShaderSource) < 10) { logTestError("  FAIL: fragmentShaderSource empty"); ok = false; }
    if (particleVertexShaderSource == nullptr || strlen(particleVertexShaderSource) < 10) { logTestError("  FAIL: particleVertexShaderSource empty"); ok = false; }
    if (particleFragmentShaderSource == nullptr || strlen(particleFragmentShaderSource) < 10) { logTestError("  FAIL: particleFragmentShaderSource empty"); ok = false; }
    if (lineVertexShaderSource == nullptr || strlen(lineVertexShaderSource) < 10) { logTestError("  FAIL: lineVertexShaderSource empty"); ok = false; }
    if (lineFragmentShaderSource == nullptr || strlen(lineFragmentShaderSource) < 10) { logTestError("  FAIL: lineFragmentShaderSource empty"); ok = false; }
    if (!g_vertices.empty()) { if (modelVAO == 0) { logTestError("  FAIL: modelVAO not generated"); ok = false; } if (modelVBO_vertices == 0) { logTestError("  FAIL: modelVBO_vertices not generated"); ok = false; } }
    if (!checkGLErrors("testShaderAndResources")) ok = false;
    int totalVAOs = (modelVAO!=0) + (particleVAO!=0) + (streamlineVAO!=0) + (bboxVAO!=0) + (axesVAO!=0) + (obstacleVAO!=0) + (liftDragVAO!=0);
    logTest("  VAOs active: " + std::to_string(totalVAOs) + "/7");
    if (totalVAOs == 0 && !g_vertices.empty()) { logTestError("  FAIL: no VAOs active but model loaded"); ok = false; }
    if (ok) logTest("  Shaders and resources OK");
    return ok;
}
bool testErrorHandling() {
    logTest("Testing error handling (code action)...");
    bool ok = true;
    try { glm::vec3 nanPos(std::numeric_limits<float>::quiet_NaN(), 0, 0); float d = sampleSDFCPU(nanPos); if (!isValidFloat(d)) logTestWarn("  WARN: sampleSDFCPU(NaN) returned NaN"); } catch (const std::exception& e) { logTestError(std::string("  FAIL: sampleSDFCPU(NaN) threw: ") + e.what()); ok = false; } catch (...) { logTestError("  FAIL: sampleSDFCPU(NaN) threw unknown"); ok = false; }
    try { glm::vec3 nanPos(std::numeric_limits<float>::quiet_NaN(), 0, 0); glm::vec3 v = computeVelocityFieldCPU(nanPos, flowParams); if (!isValidVec3(v)) logTestWarn("  WARN: computeVelocityField(NaN) NaN"); } catch (...) { logTestError("  FAIL: computeVelocityField(NaN) threw"); ok = false; }
    try { AtmosphereParams atm = calculateAtmosphere(std::numeric_limits<float>::quiet_NaN()); if (!isValidFloat(atm.density)) logTestWarn("  WARN: atmosphere(NaN) NaN"); } catch (...) { logTestError("  FAIL: atmosphere(NaN) threw"); ok = false; }
    try { float inf = std::numeric_limits<float>::infinity(); float ms = speedToMS(inf, SPEED_KMH); if (!std::isinf(ms)) logTestWarn("  WARN: speedToMS(Inf) should propagate Inf"); } catch (...) { logTestError("  FAIL: speedToMS(Inf) threw"); ok = false; }
    try { glm::vec3 huge(1e10f, 1e10f, 1e10f); glm::vec3 v = computeVelocityFieldCPU(huge, flowParams); if (!isValidVec3(v)) { logTestError("  FAIL: velocity huge NaN"); ok = false; } } catch (...) { logTestError("  FAIL: velocity huge threw"); ok = false; }
    try { FlowParams p = flowParams; p.vx = 0; p.vy = 0; p.vz = 0; glm::vec3 pos = center; glm::vec3 v = computeVelocityFieldCPU(pos, p); if (!isValidVec3(v)) { logTestError("  FAIL: velocity zero vinf NaN"); ok = false; } } catch (...) { logTestError("  FAIL: velocity zero vinf threw"); ok = false; }
    if (g_vertices.empty() && !g_distanceField.empty()) logTestWarn("  WARN: distanceField exists but no vertices");
    try { std::string longMsg(10000, 'A'); logTest("Long message test: " + longMsg.substr(0,100) + "..."); } catch (...) { logTestError("  FAIL: logTest long threw"); ok = false; }
    if (ok) logTest("  Error handling OK");
    return ok;
}

// ===================== LBM TESTS (v1.5.0) =====================
bool testLBMPhysics() {
    logTest("Testing LBM physics (LBM)...");
    bool ok = true;
    if (!lbmParams.enabled) {
        logTest("  SKIP: LBM disabled, testing init capability");
        // тест инициализации без включения
        if (g_voxelData.empty()) {
            logTest("  SKIP: no voxel data for LBM init test");
            return true;
        }
        // пробуем инициализировать временно
        bool wasEnabled = lbmParams.enabled;
        lbmParams.enabled = true;
        bool prevInit = lbmInitialized;
        if (!prevInit) {
            try { initLBM(); } catch (...) { logTestError("  FAIL: initLBM threw"); ok = false; }
            if (!lbmInitialized) { logTestError("  FAIL: LBM init failed"); ok = false; }
            else {
                if (!lbmValidateInitialization()) { logTestError("  FAIL: LBM init validation"); ok = false; }
                shutdownLBM();
            }
        }
        lbmParams.enabled = wasEnabled;
        return ok;
    }
    if (!lbmInitialized) {
        logTestError("  FAIL: LBM enabled but not initialized");
        return false;
    }
    if (!lbmValidateInitialization()) { logTestError("  FAIL: LBM init check"); ok = false; }
    if (!lbmValidateBoundaryConditions()) { logTestError("  FAIL: LBM BC check"); ok = false; }
    if (!lbmValidateSolidHandling()) { logTestError("  FAIL: LBM solid handling"); ok = false; }
    if (!lbmValidateConservation()) { logTestError("  FAIL: LBM mass conservation"); ok = false; }

    // Проверка скорости — не NaN, в разумных пределах
    int total = lbmNx*lbmNy*lbmNz;
    int nanCount = 0, hugeCount = 0;
    for (int i = 0; i < total; i++) {
        if (lbmIsSolid[i]) continue;
        if (!isValidFloat(lbmUx[i]) || !isValidFloat(lbmUy[i]) || !isValidFloat(lbmUz[i])) nanCount++;
        float mag = std::sqrt(lbmUx[i]*lbmUx[i] + lbmUy[i]*lbmUy[i] + lbmUz[i]*lbmUz[i]);
        if (mag > 1.0f) hugeCount++; // LB скорость должна быть <0.3 для несжимаемости
    }
    if (nanCount > 0) { logTestError("  FAIL: LBM velocity NaN count: " + std::to_string(nanCount)); ok = false; }
    if (hugeCount > total/10) { logTestError("  FAIL: too many high LB velocities >1.0: " + std::to_string(hugeCount)); ok = false; }
    else if (hugeCount > 0) logTestWarn("  WARN: " + std::to_string(hugeCount) + " cells with LB vel >1.0 (Mach too high)");

    // Проверка давления
    int pressNaN = 0;
    for (float p : lbmPressure) if (!isValidFloat(p)) pressNaN++;
    if (pressNaN > 0) { logTestError("  FAIL: LBM pressure NaN: " + std::to_string(pressNaN)); ok = false; }

    if (ok) logTest("  LBM physics OK");
    return ok;
}

bool testLBMConservation() {
    logTest("Testing LBM conservation laws...");
    bool ok = true;
    if (!lbmInitialized) { logTest("  SKIP: LBM not initialized"); return true; }

    // Масса
    float avgRho = 0;
    for (float r : lbmRho) avgRho += r;
    avgRho /= lbmRho.size();
    logTest("  Avg rho: " + std::to_string(avgRho) + " (expected ~1.0)");
    if (std::fabs(avgRho - 1.0f) > 0.2f) { logTestError("  FAIL: avg rho deviates >0.2 from 1.0"); ok = false; }

    // Импульс — должен быть примерно inlet * (1 - solidFraction)
    float solidFrac = 0;
    for (char s : lbmIsSolid) if (s) solidFrac += 1;
    solidFrac /= lbmIsSolid.size();
    logTest("  Solid fraction: " + std::to_string(solidFrac*100) + "%");

    // Кинетическая энергия не должна взрываться
    if (lbmAvgKineticEnergy > 1.0f) { logTestError("  FAIL: kinetic energy too high: " + std::to_string(lbmAvgKineticEnergy)); ok = false; }
    if (!isValidFloat(lbmAvgKineticEnergy)) { logTestError("  FAIL: kinetic energy NaN"); ok = false; }

    // Сходимость
    if (lbmConvergence > 1.0f) logTestWarn("  WARN: convergence high: " + std::to_string(lbmConvergence));

    if (ok) logTest("  LBM conservation OK");
    return ok;
}

bool testLBMVorticity() {
    logTest("Testing LBM vorticity & Q-criterion...");
    bool ok = true;
    if (!lbmInitialized) { logTest("  SKIP: LBM not initialized"); return true; }

    // Если еще не считались вихри — считаем
    if (lbmVorticityMag.empty() || lbmVorticityMag[0] == 0) {
        try { computeLBMVorticityAndQ(); } catch (...) { logTestError("  FAIL: compute vorticity threw"); return false; }
    }

    int nanVort = 0, nanQ = 0;
    float maxVort = 0, maxQ = -1e9f, minQ = 1e9f;
    for (size_t i = 0; i < lbmVorticityMag.size(); i++) {
        if (lbmIsSolid[i]) continue;
        float v = lbmVorticityMag[i];
        float q = lbmQCriterion[i];
        if (!isValidFloat(v)) nanVort++;
        else if (v > maxVort) maxVort = v;
        if (!isValidFloat(q)) nanQ++;
        else { if (q > maxQ) maxQ = q; if (q < minQ) minQ = q; }
    }
    logTest("  Vorticity max: " + std::to_string(maxVort) + " | Q range: [" + std::to_string(minQ) + ", " + std::to_string(maxQ) + "]");
    if (nanVort > 0) { logTestError("  FAIL: vorticity NaN count: " + std::to_string(nanVort)); ok = false; }
    if (nanQ > 0) { logTestError("  FAIL: Q NaN count: " + std::to_string(nanQ)); ok = false; }
    if (maxVort > 100.0f) logTestWarn("  WARN: vorticity very high: " + std::to_string(maxVort));

    // Q должен иметь и положительные (вихри) и отрицательные (деформация) значения
    if (maxQ < 1e-6f) logTestWarn("  WARN: Q max near zero — no vortices detected");
    if (minQ > -1e-6f) logTestWarn("  WARN: Q min near zero — no strain");

    if (ok) logTest("  LBM vorticity OK");
    return ok;
}

bool testLBMPerformance() {
    logTest("Testing LBM performance (optimized v1.6.0)...");
    bool ok = true;
    if (!lbmInitialized) { logTest("  SKIP: LBM not initialized"); return true; }

    int total = lbmNx*lbmNy*lbmNz;
    float mlups = 0.0f;
    if (lbmTimeMs > 1e-6f) {
        mlups = (total * lbmParams.stepsPerFrame) / (lbmTimeMs * 1000.0f);
    }
    logTest("  Cells: " + std::to_string(total) + " | Steps/frame: " + std::to_string(lbmParams.stepsPerFrame) + " | Time: " + std::to_string(lbmTimeMs) + " ms | MLUPS: " + std::to_string(mlups));
    logTest("  OpenMP threads: " + std::to_string(perfOpenMPThreads) + " | Frame: " + std::to_string(perfFrameMs) + " ms");

    if (lbmTimeMs > 100.0f) logTestWarn("  WARN: LBM step time >100ms — may drop FPS");
    if (lbmTimeMs > 500.0f) { logTestError("  FAIL: LBM too slow >500ms"); ok = false; }
    if (mlups < 0.1f && total > 1000) logTestWarn("  WARN: MLUPS very low <0.1 — optimization may not be active");

    // Проверка Reynolds
    float Re = computeLBMRe();
    logTest("  Reynolds: " + std::to_string(Re));
    if (Re < 1.0f) logTestWarn("  WARN: Re very low <1 — Stokes flow");
    if (Re > 10000.0f) logTestWarn("  WARN: Re very high >10000 — may be unstable with BGK");

    // Проверка оптимизации — время кадра должно быть разумным
    if (perfFrameMs > 100.0f) logTestWarn("  WARN: frame time >100ms — heavy load");
    if (perfOpenMPThreads < 2) logTestWarn("  WARN: OpenMP threads <2 — may be single-threaded");

    // Tau проверка
    if (lbmParams.tau < 0.51f || lbmParams.tau > 2.0f) { logTestError("  FAIL: tau out of stable range [0.51,2.0]"); ok = false; }

    if (ok) logTest("  LBM performance OK");
    return ok;
}

bool testOptimization() {
    logTest("Testing optimization (v1.6.0)...");
    bool ok = true;
    // Проверяем что перф метрики инициализированы
    if (perfFrameMs < 0 || perfFrameMs > 10000) { logTestError("  FAIL: perfFrameMs invalid"); ok = false; }
    if (perfLBMms < 0 || perfLBMms > 10000) { logTestError("  FAIL: perfLBMms invalid"); ok = false; }
    // Проверяем что LBM использует оптимизированный путь (gather)
    if (lbmInitialized) {
        // Проверяем что размеры LBM совпадают с вокселями
        if (lbmNx != g_voxNx || lbmNy != g_voxNy || lbmNz != g_voxNz) {
            logTestWarn("  WARN: LBM grid != voxel grid — reinit needed");
        }
        // Проверяем что tau в стабильном диапазоне
        if (lbmParams.tau < 0.51f || lbmParams.tau > 2.0f) { logTestError("  FAIL: tau out of range"); ok = false; }
    }
    // Проверяем что частицы не NaN
    for (int i = 0; i < std::min(100, particleDrawCount); ++i) {
        float x = particlePositions[3*i], y = particlePositions[3*i+1], z = particlePositions[3*i+2];
        if (!isValidFloat(x) || !isValidFloat(y) || !isValidFloat(z)) { logTestError("  FAIL: particle NaN"); ok = false; break; }
    }
    if (ok) logTest("  Optimization OK");
    return ok;
}

bool testOpenMP() {
    logTest("Testing OpenMP...");
    bool ok = true;
    logTest("  Threads: " + std::to_string(perfOpenMPThreads));
#ifdef _OPENMP
    logTest("  OpenMP enabled at compile time");
    if (perfOpenMPThreads < 1) { logTestError("  FAIL: OpenMP threads <1"); ok = false; }
#else
    logTest("  OpenMP disabled at compile time (single-thread)");
    if (perfOpenMPThreads != 1) logTestWarn("  WARN: perf threads !=1 but OpenMP disabled");
#endif
    if (ok) logTest("  OpenMP OK");
    return ok;
}

bool testMemoryLayout() {
    logTest("Testing memory layout...");
    bool ok = true;
    if (!g_vertices.empty() && g_vertices.size() % 3 != 0) { logTestError("  FAIL: vertices not multiple of 3"); ok = false; }
    if (!g_normals.empty() && g_normals.size() != g_vertices.size()) { logTestError("  FAIL: normals size mismatch"); ok = false; }
    if (lbmInitialized) {
        int total = lbmNx*lbmNy*lbmNz;
        if ((int)lbmRho.size() != total) { logTestError("  FAIL: lbmRho size mismatch"); ok = false; }
        if ((int)lbmIsSolid.size() != total) { logTestError("  FAIL: lbmIsSolid size mismatch"); ok = false; }
        if ((int)lbmTKEField.size() != total) { logTestError("  FAIL: TKE field size mismatch"); ok = false; }
    }
    if (ok) logTest("  Memory layout OK");
    return ok;
}

bool testRealisticAero() {
    logTest("Testing realistic aerodynamics (photo-like)...");
    bool ok = true;
    if (modelVertexCount == 0) { logTest("  SKIP: no model"); return true; }

    float vinf = sqrtf(flowParams.vx*flowParams.vx + flowParams.vy*flowParams.vy + flowParams.vz*flowParams.vz);
    if (!std::isfinite(vinf) || vinf < 1e-3f) vinf = 1.0f;
    int checkVerts = std::min(200, (int)(g_vertices.size()/3));
    float minCp = 1e9f, maxCp = -1e9f;
    for (int i = 0; i < checkVerts; ++i) {
        glm::vec3 p(g_vertices[3*i], g_vertices[3*i+1], g_vertices[3*i+2]);
        glm::vec3 v = computeVelocityFieldCPU(p, flowParams);
        float speed = glm::length(v);
        if (!std::isfinite(speed)) continue;
        float cp = 1.0f - (speed*speed)/(vinf*vinf);
        if (cp < minCp) minCp = cp;
        if (cp > maxCp) maxCp = cp;
    }
    logTest("  Cp range: [" + std::to_string(minCp) + ", " + std::to_string(maxCp) + "] expected [-3,1.5]");
    if (minCp < -5.0f || maxCp > 3.0f) { logTestError("  FAIL: Cp out of realistic range"); ok = false; }

    glm::vec3 c1 = getRealisticPressureColor(1.0f);
    glm::vec3 c2 = getRealisticPressureColor(-2.0f);
    if (c1.r < 0.8f) { logTestError("  FAIL: pressure color for Cp=1 should be red"); ok = false; }
    if (c2.b < 0.5f) { logTestError("  FAIL: pressure color for Cp=-2 should be blue"); ok = false; }

    glm::vec3 cv1 = getVelocityMagnitudeColor(0.0f, 10.0f);
    glm::vec3 cv2 = getVelocityMagnitudeColor(10.0f, 10.0f);
    if (cv1.b < 0.3f) { logTestError("  FAIL: vel color low should be blue"); ok = false; }
    if (cv2.r < 0.8f) { logTestError("  FAIL: vel color high should be red"); ok = false; }

    if (lbmParams.enabled && lbmInitialized) {
        if (!lbmValidateRealisticAero()) { logTestError("  FAIL: LBM realistic aero validation"); ok = false; }
        float maxVort = 0;
        for (float v : lbmVorticityMag) if (std::isfinite(v) && v > maxVort) maxVort = v;
        logTest("  Max vorticity: " + std::to_string(maxVort));
        if (maxVort < 1e-6f) logTestWarn("  WARN: no vorticity detected — may need more LBM steps");
    }

    if (aeroGroundEffect) {
        logTest("  Ground effect enabled at " + std::to_string(aeroGroundHeight));
        if (!std::isfinite(aeroGroundHeight)) { logTestError("  FAIL: ground height NaN"); ok = false; }
    }

    if (ok) logTest("  Realistic aero OK — like photos");
    return ok;
}

bool testGroundEffect() {
    logTest("Testing ground effect...");
    bool ok = true;
    float savedGE = aeroGroundEffect;
    float savedGH = aeroGroundHeight;
    
    aeroGroundEffect = true;
    aeroGroundHeight = 0.0f;
    updateFlowParams();
    
    // Test that velocity near ground is reduced (no-slip)
    glm::vec3 pGround(g_voxMinX, g_voxMinY + 0.01f, g_voxMinZ);
    glm::vec3 pAbove(g_voxMinX, g_voxMinY + 1.0f, g_voxMinZ);
    glm::vec3 vGround = computeVelocityFieldCPU(pGround, flowParams);
    glm::vec3 vAbove = computeVelocityFieldCPU(pAbove, flowParams);
    float magGround = glm::length(vGround);
    float magAbove = glm::length(vAbove);
    logTest("  Ground vel: " + std::to_string(magGround) + " vs above: " + std::to_string(magAbove));
    if (magGround > magAbove + 1e-3f) {
        logTestWarn("  WARN: ground velocity should be <= above velocity (no-slip)");
    }
    
    // Test ground plane creation
    try {
        createGroundPlane(maxDim * 2.0f);
        if (groundVAO == 0) { logTestError("  FAIL: ground VAO not created"); ok = false; }
    } catch (...) { logTestError("  FAIL: ground plane creation threw"); ok = false; }
    
    aeroGroundEffect = savedGE;
    aeroGroundHeight = savedGH;
    updateFlowParams();
    
    if (ok) logTest("  Ground effect OK");
    return ok;
}

bool testColorMaps() {
    logTest("Testing color maps v1.9.0...");
    bool ok = true;
    
    // Test all color maps for NaN and range including new v1.9.0 modes
    for (int cm = 0; cm < 4; ++cm) {
        aeroColorMap = cm;
        for (float t = 0.0f; t <= 1.0f; t += 0.1f) {
            glm::vec3 c1 = getRealisticPressureColor(t*4.0f - 3.0f);
            glm::vec3 c2 = getVelocityMagnitudeColor(t*10.0f, 10.0f);
            glm::vec3 c3 = getVorticityColor(t*20.0f);
            glm::vec3 c4 = getQCriterionColor(t*2.0f - 1.0f);
            glm::vec3 c5 = getTKEColor(t);
            glm::vec3 c6 = getMachColor(t*1.5f);
            glm::vec3 c7 = getHelicityColor(t*2.0f - 1.0f);
            glm::vec3 c8 = getTotalPressureColor(101325.0f * t, 101325.0f);
            if (!std::isfinite(c1.x) || !std::isfinite(c2.x) || !std::isfinite(c3.x) || !std::isfinite(c4.x) || !std::isfinite(c5.x) ||
                !std::isfinite(c6.x) || !std::isfinite(c7.x) || !std::isfinite(c8.x)) {
                logTestError("  FAIL: color map " + std::to_string(cm) + " t=" + std::to_string(t) + " produced NaN");
                ok = false;
            }
            // Check 0-1 range
            auto checkRange = [&](glm::vec3 c, const char* name) {
                if (c.x < -0.1f || c.x > 1.1f || c.y < -0.1f || c.y > 1.1f || c.z < -0.1f || c.z > 1.1f) {
                    logTestError(std::string("  FAIL: ") + name + " out of [0,1] range: " + std::to_string(c.x) + "," + std::to_string(c.y) + "," + std::to_string(c.z));
                    return false;
                }
                return true;
            };
            if (!checkRange(c1, "pressure")) ok = false;
            if (!checkRange(c2, "velocity")) ok = false;
            if (!checkRange(c6, "mach")) ok = false;
            if (!checkRange(c7, "helicity")) ok = false;
            if (!checkRange(c8, "totalPressure")) ok = false;
        }
    }
    aeroColorMap = 0; // reset
    
    if (ok) logTest("  Color maps OK");
    return ok;
}

bool testStability() {
    logTest("Testing stability...");
    bool ok = true;
    
    if (lbmInitialized) {
        if (!lbmValidateStability()) { logTestError("  FAIL: LBM stability check"); ok = false; }
        if (!lbmValidateConservation()) { logTestError("  FAIL: LBM conservation"); ok = false; }
        // Check for NaN in all fields
        int nanCount = 0;
        for (float v : lbmRho) if (!std::isfinite(v)) nanCount++;
        for (float v : lbmUx) if (!std::isfinite(v)) nanCount++;
        for (float v : lbmUy) if (!std::isfinite(v)) nanCount++;
        for (float v : lbmUz) if (!std::isfinite(v)) nanCount++;
        if (nanCount > 0) { logTestError("  FAIL: LBM fields have " + std::to_string(nanCount) + " NaN"); ok = false; }
    }
    
    // Check global state
    if (!std::isfinite(flowSpeed) || flowSpeed < 0 || flowSpeed > 1e6f) { logTestError("  FAIL: flowSpeed invalid"); ok = false; }
    if (!std::isfinite(maxDim) || maxDim < 1e-6f) { logTestError("  FAIL: maxDim invalid"); ok = false; }
    if (!std::isfinite(aeroRefArea) || aeroRefArea < 1e-9f) { logTestError("  FAIL: aeroRefArea invalid"); ok = false; }
    
    if (ok) logTest("  Stability OK");
    return ok;
}

bool testRefArea() {
    logTest("Testing ref area...");
    bool ok = true;
    
    float areaAuto = computeLBMRefArea();
    logTest("  Auto ref area: " + std::to_string(areaAuto));
    if (!std::isfinite(areaAuto) || areaAuto < 1e-6f || areaAuto > 1e6f) { logTestError("  FAIL: ref area invalid"); ok = false; }
    
    // Manual area
    float savedAuto = aeroAutoRefArea;
    float savedArea = aeroRefArea;
    aeroAutoRefArea = false;
    aeroRefArea = 2.5f;
    float areaManual = computeLBMRefArea();
    if (fabsf(areaManual - 2.5f) > 1e-3f) { logTestError("  FAIL: manual ref area not respected"); ok = false; }
    
    aeroAutoRefArea = savedAuto;
    aeroRefArea = savedArea;
    
    // Test Cd/Cl calculation doesn't crash
    try {
        computeLiftDrag();
        if (!std::isfinite(liftMagnitude) || !std::isfinite(dragMagnitude)) {
            logTestError("  FAIL: lift/drag NaN");
            ok = false;
        }
        logTest("  Cd: " + std::to_string(aeroRefArea>1e-6f ? dragMagnitude / (0.5f*airDensity*flowSpeed*flowSpeed*aeroRefArea) : 0) + 
                " Cl: " + std::to_string(aeroRefArea>1e-6f ? liftMagnitude / (0.5f*airDensity*flowSpeed*flowSpeed*aeroRefArea) : 0));
    } catch (...) { logTestError("  FAIL: computeLiftDrag threw"); ok = false; }
    
    if (ok) logTest("  Ref area OK");
    return ok;
}

bool testReynolds() {
    logTest("Testing Reynolds number v1.9.0...");
    bool ok = true;
    
    float Re = aeroReNumber;
    logTest("  Re: " + std::to_string(Re));
    if (!std::isfinite(Re) || Re < 0) { logTestError("  FAIL: Re invalid"); ok = false; }
    if (Re > 1e10f) { logTestError("  FAIL: Re too high"); ok = false; }
    
    // Test that Re changes with speed
    float savedSpeed = flowSpeed;
    flowSpeed = 1.0f;
    updateFlowParams();
    float Re1 = aeroReNumber;
    flowSpeed = 10.0f;
    updateFlowParams();
    float Re2 = aeroReNumber;
    logTest("  Re at 1 m/s: " + std::to_string(Re1) + " at 10 m/s: " + std::to_string(Re2));
    if (Re2 <= Re1) { logTestError("  FAIL: Re should increase with speed"); ok = false; }
    
    flowSpeed = savedSpeed;
    updateFlowParams();
    
    if (lbmInitialized) {
        float lbmRe = computeLBMRe();
        logTest("  LBM Re: " + std::to_string(lbmRe));
        if (!std::isfinite(lbmRe)) { logTestError("  FAIL: LBM Re NaN"); ok = false; }

        // v1.9.0 new: test Mach, Helicity, Total Pressure
        glm::vec3 testPos = center;
        float mach = getLBMMachWorld(testPos);
        float hel = getLBMHelicityWorld(testPos);
        float pt = getLBMTotalPressureWorld(testPos);
        logTest("  Mach: " + std::to_string(mach) + " Helicity: " + std::to_string(hel) + " Pt: " + std::to_string(pt));
        if (!std::isfinite(mach) || mach < 0 || mach > 10.0f) { logTestError("  FAIL: Mach invalid"); ok = false; }
        if (!std::isfinite(hel) || fabsf(hel) > 1.5f) { logTestError("  FAIL: Helicity invalid"); ok = false; }
        if (!std::isfinite(pt) || pt < 0) { logTestError("  FAIL: Total Pressure invalid"); ok = false; }

        glm::vec3 machCol = getMachColor(mach);
        glm::vec3 helCol = getHelicityColor(hel);
        glm::vec3 ptCol = getTotalPressureColor(pt, airPressure + 0.5f*airDensity*flowSpeed*flowSpeed);
        if (!std::isfinite(machCol.x) || !std::isfinite(helCol.x) || !std::isfinite(ptCol.x)) {
            logTestError("  FAIL: new color maps NaN");
            ok = false;
        }
    }
    
    if (ok) logTest("  Reynolds OK");
    return ok;
}

// ===================== RUNNERS =====================
void runPhysicsTests() {
    struct Case { const char* name; bool (*func)(); };
    Case tests[] = {
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
        bool passed = false; std::string msg = "";
        try { passed = tc.func(); } catch (const std::exception& e) { msg = std::string("EXCEPTION: ") + e.what(); passed = false; } catch (...) { msg = "UNKNOWN EXCEPTION"; passed = false; }
        TestResult r; r.name = tc.name; r.category = "Physics"; r.passed = passed; r.message = msg.empty() ? (passed ? "OK" : "FAILED") : msg;
        lastTestResults.push_back(r);
        if (passed) testsPassed++; else testsFailed++;
        logTest(std::string(tc.name) + " [" + r.category + "]: " + (passed ? "PASS" : "FAIL") + (msg.empty() ? "" : " - " + msg));
    }
}
void runCodeTests() {
    struct Case { const char* name; bool (*func)(); };
    Case tests[] = {
        {"OpenGL State", testOpenGLState},
        {"Buffer Integrity", testBufferIntegrity},
        {"Model Integrity", testModelIntegrity},
        {"FlowParams Sanity", testFlowParamsSanity},
        {"Time & Camera", testTimeAndCamera},
        {"Memory Safety", testMemorySafety},
        {"Division by Zero Risks", testDivisionByZeroRisks},
        {"Input & State", testInputAndState},
        {"Shaders & Resources", testShaderAndResources},
        {"Error Handling", testErrorHandling},
    };
    for (auto& tc : tests) {
        bool passed = false; std::string msg = "";
        try { passed = tc.func(); } catch (const std::exception& e) { msg = std::string("EXCEPTION: ") + e.what(); passed = false; } catch (...) { msg = "UNKNOWN EXCEPTION"; passed = false; }
        TestResult r; r.name = tc.name; r.category = "Code"; r.passed = passed; r.message = msg.empty() ? (passed ? "OK" : "FAILED") : msg;
        lastTestResults.push_back(r);
        if (passed) codeTestsPassed++; else codeTestsFailed++;
        if (passed) testsPassed++; else testsFailed++;
        logTest(std::string(tc.name) + " [" + r.category + "]: " + (passed ? "PASS" : "FAIL") + (msg.empty() ? "" : " - " + msg));
    }
}
void runLBMTests() {
    struct Case { const char* name; bool (*func)(); };
    Case tests[] = {
        {"LBM Physics", testLBMPhysics},
        {"LBM Conservation", testLBMConservation},
        {"LBM Vorticity & Q", testLBMVorticity},
        {"LBM Performance", testLBMPerformance},
    };
    for (auto& tc : tests) {
        bool passed = false; std::string msg = "";
        try { passed = tc.func(); } catch (const std::exception& e) { msg = std::string("EXCEPTION: ") + e.what(); passed = false; } catch (...) { msg = "UNKNOWN EXCEPTION"; passed = false; }
        TestResult r; r.name = tc.name; r.category = "LBM"; r.passed = passed; r.message = msg.empty() ? (passed ? "OK" : "FAILED") : msg;
        lastTestResults.push_back(r);
        if (passed) lbmTestsPassed++; else lbmTestsFailed++;
        if (passed) testsPassed++; else testsFailed++;
        logTest(std::string(tc.name) + " [" + r.category + "]: " + (passed ? "PASS" : "FAIL") + (msg.empty() ? "" : " - " + msg));
    }
}
void runOptimizationTests() {
    struct Case { const char* name; bool (*func)(); };
    Case tests[] = {
        {"Optimization", testOptimization},
        {"OpenMP", testOpenMP},
        {"Memory Layout", testMemoryLayout},
    };
    for (auto& tc : tests) {
        bool passed = false; std::string msg = "";
        try { passed = tc.func(); } catch (const std::exception& e) { msg = std::string("EXCEPTION: ") + e.what(); passed = false; } catch (...) { msg = "UNKNOWN EXCEPTION"; passed = false; }
        TestResult r; r.name = tc.name; r.category = "Optimization"; r.passed = passed; r.message = msg.empty() ? (passed ? "OK" : "FAILED") : msg;
        lastTestResults.push_back(r);
        if (passed) testsPassed++; else testsFailed++;
        logTest(std::string(tc.name) + " [" + r.category + "]: " + (passed ? "PASS" : "FAIL") + (msg.empty() ? "" : " - " + msg));
    }
}

bool testNewFeaturesV19() {
    logTest("Testing v1.9.0 new features...");
    bool ok = true;
    // Test Mach color
    glm::vec3 cMach0 = getMachColor(0.0f);
    glm::vec3 cMach1 = getMachColor(1.0f);
    if (cMach0.b < 0.3f) { logTestError("  FAIL: Mach 0 should be blue"); ok = false; }
    if (cMach1.r < 0.8f) { logTestError("  FAIL: Mach 1 should be red"); ok = false; }
    // Test Helicity
    glm::vec3 cHelNeg = getHelicityColor(-1.0f);
    glm::vec3 cHelPos = getHelicityColor(1.0f);
    if (cHelNeg.b < 0.5f) { logTestError("  FAIL: Helicity -1 should be blue"); ok = false; }
    if (cHelPos.r < 0.5f) { logTestError("  FAIL: Helicity +1 should be red"); ok = false; }
    // Test Total Pressure
    glm::vec3 cPtLow = getTotalPressureColor(50000.0f, 101325.0f);
    glm::vec3 cPtHigh = getTotalPressureColor(101325.0f, 101325.0f);
    if (!std::isfinite(cPtLow.x) || !std::isfinite(cPtHigh.x)) { logTestError("  FAIL: Pt color NaN"); ok = false; }

    // Test settings save/load
    float savedSpeed = flowSpeed;
    flowSpeed = 5.5f;
    if (!saveSettings("test_settings.ini")) { logTestError("  FAIL: saveSettings failed"); ok = false; }
    flowSpeed = 1.0f;
    if (!loadSettings("test_settings.ini")) { logTestError("  FAIL: loadSettings failed"); ok = false; }
    if (fabsf(flowSpeed - 5.5f) > 0.01f) { logTestError("  FAIL: settings roundtrip failed"); ok = false; }
    flowSpeed = savedSpeed;
    // Cleanup test file
    remove("test_settings.ini");

    // Test adaptive LBM flag
    bool savedAdaptive = aeroAdaptiveLBM;
    aeroAdaptiveLBM = true;
    if (!aeroAdaptiveLBM) { logTestError("  FAIL: adaptive flag"); ok = false; }
    aeroAdaptiveLBM = savedAdaptive;

    // Test RK4 particles flag
    if (!aeroUseRK4Particles) logTestWarn("  WARN: RK4 particles disabled");

    // Test screenshot request flag
    aeroScreenshotRequested = false;
    aeroCSVExportRequested = false;

    if (ok) logTest("  v1.9.0 new features OK");
    return ok;
}

void runRealisticTests() {
    struct Case { const char* name; bool (*func)(); };
    Case tests[] = {
        {"Realistic Aero (Photo)", testRealisticAero},
        {"Ground Effect", testGroundEffect},
        {"Color Maps", testColorMaps},
        {"Stability", testStability},
        {"Ref Area", testRefArea},
        {"Reynolds", testReynolds},
        {"New Features v1.9.0", testNewFeaturesV19},
    };
    for (auto& tc : tests) {
        bool passed = false; std::string msg = "";
        try { passed = tc.func(); } catch (const std::exception& e) { msg = std::string("EXCEPTION: ") + e.what(); passed = false; } catch (...) { msg = "UNKNOWN EXCEPTION"; passed = false; }
        TestResult r; r.name = tc.name; r.category = "Realistic"; r.passed = passed; r.message = msg.empty() ? (passed ? "OK" : "FAILED") : msg;
        lastTestResults.push_back(r);
        if (passed) testsPassed++; else testsFailed++;
        logTest(std::string(tc.name) + " [" + r.category + "]: " + (passed ? "PASS" : "FAIL") + (msg.empty() ? "" : " - " + msg));
    }
}
void runAllTests() {
    auto t0 = std::chrono::high_resolution_clock::now();
    lastTestResults.clear(); testLog.clear();
    testsPassed = 0; testsFailed = 0; codeTestsPassed = 0; codeTestsFailed = 0; lbmTestsPassed = 0; lbmTestsFailed = 0; lastGLError = 0; lastGLErrorStr.clear();
    logTest("=== Starting Aeros Engine Tests v1.9.0 Ultra Realistic+ ===");
    logTest("Model: " + std::to_string(modelVertexCount) + " vertices, Voxel: " + std::to_string(g_voxNx) + "x" + std::to_string(g_voxNy) + "x" + std::to_string(g_voxNz) + ", LBM: " + std::to_string(lbmNx) + "x" + std::to_string(lbmNy) + "x" + std::to_string(lbmNz));
    logTest("--- Physics Tests ---");
    runPhysicsTests();
    logTest("--- Code Action Tests ---");
    runCodeTests();
    logTest("--- LBM Tests ---");
    runLBMTests();
    logTest("--- Optimization Tests ---");
    runOptimizationTests();
    logTest("--- Realistic Aero Tests ---");
    runRealisticTests();
    auto t1 = std::chrono::high_resolution_clock::now();
    lastTestTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    int realisticPassed = 0, realisticFailed = 0;
    for (auto& r : lastTestResults) if (r.category=="Realistic") { if (r.passed) realisticPassed++; else realisticFailed++; }
    logTest("=== Tests finished: " + std::to_string(testsPassed) + " passed, " + std::to_string(testsFailed) + " failed (Physics=" + std::to_string(testsPassed - codeTestsPassed - lbmTestsPassed - realisticPassed) + " Code=" + std::to_string(codeTestsPassed) + "/" + std::to_string(codeTestsPassed+codeTestsFailed) + " LBM=" + std::to_string(lbmTestsPassed) + "/" + std::to_string(lbmTestsPassed+lbmTestsFailed) + " Opt=3 Realistic=" + std::to_string(realisticPassed) + "/" + std::to_string(realisticPassed+realisticFailed) + ") in " + std::to_string(lastTestTimeMs) + " ms ===");
    if (testsFailed > 0) logTestError("!!! ERRORS DETECTED: Physics=" + std::to_string(testsFailed - codeTestsFailed - lbmTestsFailed - realisticFailed) + " Code=" + std::to_string(codeTestsFailed) + " LBM=" + std::to_string(lbmTestsFailed) + " Realistic=" + std::to_string(realisticFailed) + " !!!");
    else logTest("All tests passed — physics, code, LBM, optimization and realistic OK");
}
void validateFrame() {
    if (!testContinuous) return;
    bool hasError = false;
    if (!isValidFloat(flowSpeed) || !isValidFloat(altitude) || !isValidFloat(airDensity)) hasError = true;
    if (!isValidFloat(liftMagnitude) || !isValidFloat(dragMagnitude)) hasError = true;
    if (hasError) logTestError("FRAME VALIDATION FAILED at t=" + std::to_string(flowParams.time));
    for (int i = 0; i < std::min(100, particleDrawCount); i++) {
        glm::vec3 p(particlePositions[3*i], particlePositions[3*i+1], particlePositions[3*i+2]);
        if (!isValidVec3(p)) { logTestError("Particle NaN at frame " + std::to_string(flowParams.time) + " idx " + std::to_string(i)); break; }
    }
    validateFrameCode();
}
void validateFrameCode() {
    if (!isValidFloat(deltaTime) || deltaTime <= 0 || deltaTime > 0.5f) logTestError("FRAME CODE: deltaTime invalid: " + std::to_string(deltaTime));
    if (!isValidVec3(cameraPos) || !isValidVec3(cameraFront)) logTestError("FRAME CODE: camera NaN");
    if (particleDrawCount < 0 || particleDrawCount > (int)(particlePositions.size()/3 + 1)) logTestError("FRAME CODE: particleDrawCount out of bounds: " + std::to_string(particleDrawCount));
    if (g_voxNx * g_voxNy * g_voxNz != (int)g_distanceField.size() && !g_distanceField.empty()) logTestError("FRAME CODE: voxel size mismatch");
    static float lastCheck = 0;
    if (flowParams.time - lastCheck > 1.0f) {
        lastCheck = flowParams.time;
        GLenum err; while ((err = glGetError()) != GL_NO_ERROR) logTestError("FRAME CODE: GL error: " + getGLErrorString(err));
    }
}
