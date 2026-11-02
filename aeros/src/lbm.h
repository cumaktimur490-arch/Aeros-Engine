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
    bool useMRT = false;         // НЕ РЕАЛИЗОВАНО: коллизия везде BGK, MRT нет (v1.30.0 — в UI заблокирован)
    int maxSteps = 10000;
    float convergenceThreshold = 1e-5f;
    bool useGround = false;
    float groundHeight = 0.0f;
    bool useZouHeBC = false;     // НЕ РЕАЛИЗОВАНО: вход — равновесные популяции, схемы Zou/He нет (v1.30.0)
    bool useConvectiveOutlet = true;
    float inletTurbulence = 0.02f;
    bool useRegularized = false; // НЕ РЕАЛИЗОВАНО: неравновесная часть не регуляризуется (v1.30.0)
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
// v1.30.0 — явная диагностика вместо тихой подмены данных. Раньше плотность
// молча обрезалась до [0.8; 1.2] внутри расчёта равновесных популяций
// (computeEquilibriumFast), из-за чего sum(feq) != rho и масса с давлением поля
// искажались без следа. Обрезки больше нет, а состояния, требующие
// вмешательства, считаются и показываются в панели, журнале и отчёте:
extern long long lbmRhoOutOfRange;  // плотностей вне [0.5; 1.5] (расходимость)
extern long long lbmRhoNonFinite;   // плотностей NaN/Inf
extern long long lbmULimited;       // ячеек, где сработал ограничитель |u| < 0.3
// v1.30.0 — физическое давление (lbmPressure, Па) больше НЕ обрезается по
// скоростному напору: обрезали данные, которые читают силы, экспорт и карта.
// Считается только число неконечных значений — признак развала расчёта.
extern long long lbmPressureNonFinite;
// v1.30.0 — масса/импульс равновесных популяций: отдельная тестируемая функция
// (computeEquilibriumFast объявлена inline внутри lbm.cpp и из теста недоступна).
// Проверяет sum_i feq_i = rho и sum_i feq_i*c_i = rho*u. Возвращает false при
// неконечных входных данных. ВАЖНО: при |u| > 0.3 срабатывает ограничитель
// устойчивости, поэтому импульс равен rho*u_limited — масса сохраняется всегда.
bool lbmEquilibriumMoments(float rho, float ux, float uy, float uz,
                           float* sumFeq, float* momX, float* momY, float* momZ);
// v1.30.0 — честность настроек: через запятую перечисляет поля LBMParams,
// которые показаны в UI, но НЕ реализованы в решателе (пустая строка = все
// видимые настройки работают). Используется панелью, журналом и отчётом, чтобы
// переключатель больше не выдавал себя за то, чего в коде нет.
const char* lbmInertSettings();
extern float lbmTimeMs;
extern float lbmTKE;

extern int lbmNx, lbmNy, lbmNz;
extern float lbmMinX, lbmMinY, lbmMinZ;
extern float lbmMaxX, lbmMaxY, lbmMaxZ;
extern float lbmCellSizeX, lbmCellSizeY, lbmCellSizeZ;

extern std::vector<float> lbmRho;
extern std::vector<float> lbmUx, lbmUy, lbmUz;
extern std::vector<float> lbmUxWorld, lbmUyWorld, lbmUzWorld;
// v1.30.0 — смысл и единицы каждого поля, чтобы постобработка и решатель больше
// не писали в один массив разные по природе величины:
//   lbmVorticityMag [1/с]    — модуль вихря |ω| = |rot u| по мировым скоростям
//                               (центральные разности), computeLBMVorticityAndQ();
//   lbmQCriterion   [1/с2]   — Q-критерий 0.5*(Ω:Ω − S:S), там же;
//   lbmStrainMag    [1/с]    — sqrt(2*S:S), там же;
//   lbmTKEField     [м2/с2]  — подсеточная турбулентная кинетическая энергия
//                               k = 0.5*(Cs*Δ)²*|S|² из ШАГА СОУДАРЕНИЙ решателя
//                               (модель Смагоринского). Постобработка её НЕ
//                               пересчитывает: раньше она подменяла k эвристикой
//                               0.5|ω|²*0.01 + S²*0.005 с потолком 10 (1/с2);
//   lbmPressure     [Па]     — p = rho0*cs2*(rho-1)*scale2, физическое давление
//                               БЕЗ обрезки по скоростному напору.
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
// v1.29.0 — forceSteps: сколько шагов решётки сделать. >=0 задаёт число шагов
// явно (это путь фиксированного такта физики), -1 означает прежнее поведение —
// по lbmParams.stepsPerFrame с адаптацией к длине кадра.
void updateLBM(float deltaTime, int forceSteps = -1);

// v1.29.0 — сколько ФИЗИЧЕСКИХ секунд занимает один шаг решётки LBM.
// В решёточных единицах c = Δx/Δt = 1, поэтому Δt = Δx_физ / velScale, где
// velScale = flowSpeed / |u_LB| — тот же масштаб скорости, что внутри шага
// решателя (из него считаются скорости в м/с и TKE в м²/с²). Возвращает 0, если
// решатель не инициализирован или масштаб не определён.
float lbmLatticeDtWorld();

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
