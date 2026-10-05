#pragma once
// =====================================================
// LBM — Lattice Boltzmann Method D3Q19
// v1.5.0 — высокопроизводительная физика для Aeros Engine
// =====================================================

#include <glm/glm.hpp>
#include <vector>
#include <string>

// LBM параметры
struct LBMParams {
    bool enabled = false;          // использовать LBM вместо потенциального течения
    bool useGPU = false;           // GPU ускорение (CUDA LBM)
    int stepsPerFrame = 3;         // шагов LBM на кадр
    float tau = 0.6f;              // время релаксации (0.5+nu/cs2) — стабильность
    float U0 = 0.1f;               // характерная решеточная скорость
    float viscosity = 0.033f;      // решеточная вязкость (tau-0.5)/3
    float smagorinskyC = 0.12f;    // константа Смагоринского для LES
    bool useTurbulence = true;     // LES Smagorinsky
    bool useMRT = false;           // MRT (пока BGK, MRT для будущего)
    int maxSteps = 10000;          // макс шагов для сходимости
    float convergenceThreshold = 1e-5f;
};

// Глобальные LBM
extern LBMParams lbmParams;
extern bool lbmInitialized;
extern bool lbmConverged;
extern int lbmCurrentStep;
extern float lbmAvgRho;
extern float lbmAvgKineticEnergy;
extern float lbmMaxVelocityLB;     // в решеточных единицах
extern float lbmMaxVelocityWorld;  // в м/с
extern float lbmReynolds;
extern float lbmConvergence;
extern float lbmTimeMs;            // время последнего шага

// Сетка LBM (мир)
extern int lbmNx, lbmNy, lbmNz;
extern float lbmMinX, lbmMinY, lbmMinZ;
extern float lbmMaxX, lbmMaxY, lbmMaxZ;
extern float lbmCellSizeX, lbmCellSizeY, lbmCellSizeZ;

// Поля
extern std::vector<float> lbmRho;          // Nx*Ny*Nz
extern std::vector<float> lbmUx, lbmUy, lbmUz; // скорость в решеточных ед.
extern std::vector<float> lbmUxWorld, lbmUyWorld, lbmUzWorld; // в мировых
extern std::vector<float> lbmVorticityMag; // |ω|
extern std::vector<float> lbmQCriterion;   // Q-критерий
extern std::vector<float> lbmPressure;     // давление (из rho)
extern std::vector<char>  lbmIsSolid;      // 0/1

// Инициализация / сброс
void initLBM();
void resetLBM();
void shutdownLBM();

// Шаги
void stepLBMCPU(int steps = 1);
void updateLBM(float deltaTime); // вызывается каждый кадр

// Сэмплинг скорости в мировых координатах
glm::vec3 getLBMVelocityWorld(const glm::vec3& worldPos);
float getLBMDensityWorld(const glm::vec3& worldPos);
glm::vec3 getLBMVelocityLB(const glm::vec3& worldPos);
bool isLBMSolidWorld(const glm::vec3& worldPos);

// Утилиты
void computeLBMVorticityAndQ();
void computeLBMForcesFromLBM(); // пересчет lift/drag из LBM давления
float computeLBMRe();

// UI
void drawLBMUI();

// Внутренние — для тестов (переименованы чтобы не конфликтовать с test_mode)
bool lbmValidateInitialization();
bool lbmValidateConservation();
bool lbmValidateBoundaryConditions();
bool lbmValidateSolidHandling();
