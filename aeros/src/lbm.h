#pragma once
// =====================================================
// LBM — Lattice Boltzmann Method D3Q19
// v1.7.0 Realistic Aero — фотореалистичная аэродинамика
// =====================================================

#include <glm/glm.hpp>
#include <vector>
#include <string>

// LBM параметры — v1.7.0 расширено
struct LBMParams {
    bool enabled = false;          // использовать LBM вместо потенциального течения
    bool useGPU = false;           // GPU ускорение (CUDA LBM)
    int stepsPerFrame = 3;         // шагов LBM на кадр
    float tau = 0.6f;              // время релаксации (0.5+nu/cs2) — стабильность
    float U0 = 0.1f;               // характерная решеточная скорость
    float viscosity = 0.033f;      // решеточная вязкость (tau-0.5)/3
    float smagorinskyC = 0.12f;    // константа Смагоринского для LES
    bool useTurbulence = true;     // LES Smagorinsky
    bool useMRT = false;           // MRT для высоких Re (v1.7.0)
    int maxSteps = 10000;          // макс шагов для сходимости
    float convergenceThreshold = 1e-5f;
    // v1.7.0 новое
    bool useGround = false;        // земля для авто
    float groundHeight = 0.0f;     // высота земли
    bool useZouHeBC = true;        // Zou/He inlet BC — более точный
    bool useConvectiveOutlet = true; // конвективный outlet
    float inletTurbulence = 0.02f; // турбулентность на входе 0-0.1
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
extern float lbmTKE;               // turbulent kinetic energy avg

// Сетка LBM (мир)
extern int lbmNx, lbmNy, lbmNz;
extern float lbmMinX, lbmMinY, lbmMinZ;
extern float lbmMaxX, lbmMaxY, lbmMaxZ;
extern float lbmCellSizeX, lbmCellSizeY, lbmCellSizeZ;

// Поля — v1.7.0 расширено
extern std::vector<float> lbmRho;          // Nx*Ny*Nz
extern std::vector<float> lbmUx, lbmUy, lbmUz; // скорость в решеточных ед.
extern std::vector<float> lbmUxWorld, lbmUyWorld, lbmUzWorld; // в мировых
extern std::vector<float> lbmVorticityMag; // |ω|
extern std::vector<float> lbmQCriterion;   // Q-критерий
extern std::vector<float> lbmPressure;     // давление (из rho)
extern std::vector<float> lbmTKEField;     // turbulent kinetic energy field
extern std::vector<float> lbmStrainMag;    // |S| — деформация
extern std::vector<char>  lbmIsSolid;      // 0/1
extern std::vector<char>  lbmIsGround;     // земля

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
float getLBMVorticityWorld(const glm::vec3& worldPos);
float getLBMQWorld(const glm::vec3& worldPos);
float getLBMTKEWorld(const glm::vec3& worldPos);
float getLBMVelocityMagWorld(const glm::vec3& worldPos);

// Утилиты
void computeLBMVorticityAndQ();
void computeLBMForcesFromLBM(); // пересчет lift/drag из LBM давления
float computeLBMRe();
float computeLBMRefArea(); // референсная площадь
glm::vec3 getRealisticPressureColor(float cp); // rainbow как на фото
glm::vec3 getVelocityMagnitudeColor(float velMag, float maxVel); // как на фото 1,2
glm::vec3 getVorticityColor(float vortMag);

// UI
void drawLBMUI();

// Внутренние — для тестов
bool lbmValidateInitialization();
bool lbmValidateConservation();
bool lbmValidateBoundaryConditions();
bool lbmValidateSolidHandling();
bool lbmValidateRealisticAero(); // новый тест реалистичности
