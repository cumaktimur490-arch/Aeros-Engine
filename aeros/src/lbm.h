#pragma once
// =====================================================
// LBM — Lattice Boltzmann Method D3Q19
// v1.9.0 Ultra Realistic+ — фотореалистичная аэродинамика + новые режимы
// =====================================================

#include <glm/glm.hpp>
#include <vector>
#include <string>

struct LBMParams {
    bool enabled = false;
    bool useGPU = false;
    int stepsPerFrame = 3;
    float tau = 0.6f;
    float U0 = 0.1f;
    float viscosity = 0.033f;
    float smagorinskyC = 0.12f;
    bool useTurbulence = true;
    bool useMRT = false;
    int maxSteps = 10000;
    float convergenceThreshold = 1e-5f;
    bool useGround = false;
    float groundHeight = 0.0f;
    bool useZouHeBC = true;
    bool useConvectiveOutlet = true;
    float inletTurbulence = 0.02f;
    bool useRegularized = false; // v1.8.0 — regularized LBM for stability
};

extern LBMParams lbmParams;
extern bool lbmInitialized;
extern bool lbmConverged;
extern int lbmCurrentStep;
extern float lbmAvgRho;
extern float lbmAvgKineticEnergy;
extern float lbmMaxVelocityLB;
extern float lbmMaxVelocityWorld;
extern float lbmReynolds;
extern float lbmConvergence;
// v1.27.0 — диагностика устойчивости (нули — норма, рост означает развал расчёта)
extern long long lbmNegativePops;
extern long long lbmBadPops;
extern float lbmTimeMs;
extern float lbmTKE;

extern int lbmNx, lbmNy, lbmNz;
extern float lbmMinX, lbmMinY, lbmMinZ;
extern float lbmMaxX, lbmMaxY, lbmMaxZ;
extern float lbmCellSizeX, lbmCellSizeY, lbmCellSizeZ;

extern std::vector<float> lbmRho;
extern std::vector<float> lbmUx, lbmUy, lbmUz;
extern std::vector<float> lbmUxWorld, lbmUyWorld, lbmUzWorld;
extern std::vector<float> lbmVorticityMag;
extern std::vector<float> lbmQCriterion;
extern std::vector<float> lbmPressure;
extern std::vector<float> lbmTKEField;
extern std::vector<float> lbmStrainMag;
extern std::vector<char>  lbmIsSolid;
extern std::vector<char>  lbmIsGround;

void initLBM();
void resetLBM();
void shutdownLBM();

void stepLBMCPU(int steps = 1);
void updateLBM(float deltaTime);

glm::vec3 getLBMVelocityWorld(const glm::vec3& worldPos);
float getLBMDensityWorld(const glm::vec3& worldPos);
// v1.26.0: давление (Па, избыточное) с трилинейной интерполяцией.
// Раньше все скаляры брались «ближайшей ячейкой» с приведением (int), из-за чего
// карта давления была ступенчатой, а точки чуть вне области читали ячейку 0.
float getLBMPressureWorld(const glm::vec3& worldPos);
bool  getLBMPressureWorldChecked(const glm::vec3& worldPos, float* outPa);
glm::vec3 getLBMVelocityLB(const glm::vec3& worldPos);
bool isLBMSolidWorld(const glm::vec3& worldPos);
float getLBMVorticityWorld(const glm::vec3& worldPos);
float getLBMQWorld(const glm::vec3& worldPos);
float getLBMTKEWorld(const glm::vec3& worldPos);
float getLBMVelocityMagWorld(const glm::vec3& worldPos);
float getLBMStrainWorld(const glm::vec3& worldPos);

void computeLBMVorticityAndQ();
void computeLBMForcesFromLBM();
float computeLBMRe();
float computeLBMRefArea();
glm::vec3 getRealisticPressureColor(float cp);
// v1.26.0 — то же, но с явно заданным диапазоном. Нужно для воспроизведения
// таймлапса: снимки поля квантованы в диапазоне, который действовал в момент
// записи, а не в текущем.
glm::vec3 pressureColorInRange(float cp, float lo, float hi);
glm::vec3 getVelocityMagnitudeColor(float velMag, float maxVel);
glm::vec3 getVorticityColor(float vortMag);
glm::vec3 getQCriterionColor(float q);
glm::vec3 getTKEColor(float tke);
glm::vec3 getMachColor(float mach);
glm::vec3 getHelicityColor(float helicity);
glm::vec3 getTotalPressureColor(float pt, float ptInf);
float getLBMMachWorld(const glm::vec3& worldPos);
float getLBMHelicityWorld(const glm::vec3& worldPos);
float getLBMTotalPressureWorld(const glm::vec3& worldPos);

void drawLBMUI();

// Tests
bool lbmValidateInitialization();
bool lbmValidateConservation();
bool lbmValidateBoundaryConditions();
bool lbmValidateSolidHandling();
bool lbmValidateRealisticAero();
bool lbmValidateStability();
