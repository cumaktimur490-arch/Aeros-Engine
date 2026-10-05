#pragma once
// =====================================================
// Режим теста для проверки ошибок вычислений
// v1.3.0 — валидация физики, SDF, частиц, атмосферы
// =====================================================

#include <string>
#include <vector>

struct TestResult {
    std::string name;
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
extern float lastTestTimeMs;
extern std::string testLog;

// Основные тесты
bool testSpeedConversion();
bool testAtmosphereModel();
bool testSDFSampling();
bool testVelocityField();
bool testPressureCalculation();
bool testParticleSystem();
bool testForceCalculation();
bool testVoxelGrid();
bool testNaNChecks();

// Запуск всех тестов
void runAllTests();

// Проверка в реальном времени (каждый кадр)
void validateFrame();

// UI
void drawTestModeUI();

// Логирование
void logTest(const std::string& msg);
