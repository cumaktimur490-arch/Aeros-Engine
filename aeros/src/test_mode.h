#pragma once
// =====================================================
// Режим теста для проверки ошибок вычислений и кода
// v1.6.0 — физика + код + LBM + perf
// =====================================================

#include <string>
#include <vector>

struct TestResult {
    std::string name;
    std::string category; // "Physics", "Code", "LBM"
    bool passed;
    std::string message;
    float value;
    float expected;
    float tolerance;
};

extern bool testModeEnabled;
extern bool testContinuous;
extern std::vector<TestResult> lastTestResults;
extern int testsPassed;
extern int testsFailed;
extern int codeTestsPassed;
extern int codeTestsFailed;
extern int lbmTestsPassed;
extern int lbmTestsFailed;
extern float lastTestTimeMs;
extern std::string testLog;
extern int lastGLError;
extern std::string lastGLErrorStr;

// ===== Физика (v1.3.0) =====
bool testSpeedConversion();
bool testAtmosphereModel();
bool testSDFSampling();
bool testVelocityField();
bool testPressureCalculation();
bool testParticleSystem();
bool testForceCalculation();
bool testVoxelGrid();
bool testNaNChecks();

// ===== Код / Runtime (v1.4.0) =====
bool testOpenGLState();
bool testBufferIntegrity();
bool testModelIntegrity();
bool testFlowParamsSanity();
bool testTimeAndCamera();
bool testMemorySafety();
bool testDivisionByZeroRisks();
bool testInputAndState();
bool testShaderAndResources();
bool testErrorHandling();

// ===== LBM (v1.5.0) =====
bool testLBMPhysics();
bool testLBMConservation();
bool testLBMVorticity();
bool testLBMPerformance();

// ===== Optimization (v1.6.0) =====
bool testOptimization();
bool testOpenMP();
bool testMemoryLayout();

// ===== Realistic Aero (v1.7.0) =====
bool testRealisticAero();

// Утилиты
bool checkGLErrors(const char* where);
std::string getGLErrorString(int err);
void runAllTests();
void runPhysicsTests();
void runCodeTests();
void runLBMTests();
void runOptimizationTests();
void validateFrame();
void validateFrameCode();
void drawTestModeUI();
void logTest(const std::string& msg);
void logTestError(const std::string& msg);
void logTestWarn(const std::string& msg);
