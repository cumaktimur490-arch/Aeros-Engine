#pragma once
// =====================================================
// LBM — Lattice Boltzmann Method D3Q19
// v1.9.0 Ultra Realistic+ — фотореалистичная аэродинамика + новые режимы
// v1.39.0 Flagship Physics — MRT, Regularized, WALE SGS, BFL curved walls,
//         Non-Equilibrium Extrapolation + Sponge Layer, 3D Cache-Blocked Tiling
// =====================================================

#include <glm/glm.hpp>
#include <vector>
#include <string>

enum class LBMCollisionMode {
    BGK = 0,          // Single-relaxation-time BGK
    TRT = 1,          // Two-relaxation-time (magic Lambda = 3/16)
    MRT = 2,          // d'Humieres 2002 orthogonal 19-moment MRT
    Regularized = 3   // Latt-Chopard 2006 2nd-order Hermite regularized
};

enum class LBMTurbulenceModel {
    None = 0,
    Smagorinsky = 1,  // Smagorinsky-Lilly + Van Driest wall damping
    WALE = 2          // Nicoud-Ducros 1999 Wall-Adapting Local Eddy-viscosity
};

struct LBMParams {
    bool enabled = false;
    bool useGPU = false;
    int stepsPerFrame = 3;
    float tau = 0.6f;
    float U0 = 0.1f;
    float viscosity = 0.033f;
    float smagorinskyC = 0.12f;
    bool useTurbulence = true;
    int maxSteps = 10000;
    float convergenceThreshold = 1e-5f;
    bool useGround = false;
    float groundHeight = 0.0f;
    // v1.36.2 — TRT: две частоты релаксации вместо одной (BGK).
    // По умолчанию выключено для совместимости с короткими эталонными тестами.
    bool useTRT = false;
    bool useConvectiveOutlet = true;
    float inletTurbulence = 0.02f;
    float domainPadding = 0.25f;
    bool autoViscosity = true;

    // v1.39.0 — Флагманская физика:
    // collisionMode: 0=BGK, 1=TRT, 2=MRT (d'Humieres), 3=Regularized.
    // Если collisionMode==0 и useTRT==true, автоматически используется TRT.
    int collisionMode = 0;
    // turbulenceModel: 0=выкл, 1=Smagorinsky+VanDriest, 2=WALE (Nicoud-Ducros).
    int turbulenceModel = 1;
    float waleC = 0.325f;
    // Неравновесная экстраполяция (Guo 2002) на входе/выходе + свободное
    // скольжение на боковых границах + поглощающий губчатый слой на выходе.
    bool useFlagshipBoundaries = false;
    int spongeLayers = 6;
    float spongeStrength = 0.6f;
    // Субвоксельная интерполяция криволинейных стенок (Bouzidi-Firdaouss-Lallemand)
    // по полю расстояний g_distanceField (2-й порядок точности геометрии).
    bool useCurvedBoundary = false;

    // v1.36.0 — ВНЕШНЯЯ ОБЪЁМНАЯ СИЛА и периодичность по X/Z.
    float bodyForceX = 0.0f;
    float bodyForceY = 0.0f;
    float bodyForceZ = 0.0f;
    bool periodicX = false;
    bool periodicZ = false;

    // v1.40.0 — Плавный разгон скорости на входе (убирает акустический удар при старте)
    // и быстрый прогрев с грубой сетки (Coarse-to-Fine) при инициализации.
    // По умолчанию 0/false для совместимости с короткими эталонными тестами;
    // включаются флагманским пресетом (lbmApplyFlagshipPreset).
    int  rampSteps  = 0;
    bool autoWarmup = false;
};

extern LBMParams lbmParams;
extern bool lbmInitialized;
extern bool lbmConverged;
extern int lbmCurrentStep;
extern float lbmAvgRho;
extern float lbmAvgKineticEnergy;
extern float lbmMaxVelocityLB;
extern float lbmMaxVelocityWorld;
extern float lbmReynolds;      // физическое Re = rho*V*L/mu (что обтекается)
extern float lbmReynoldsLB;    // решёточное Re = U0*L_LB/nu_LB (что реально считается)
// v1.34.0 — доля перекрытия расчётной области телом (фронтальная площадь /
// сечение домена). Для внешнего обтекания норма < 5 %: выше — стенки домена
// сами поджимают поток, и сопротивление/подъёмная сила зависят от размера
// области, а не только от формы тела. Считается и показывается, чтобы это
// было видно, а не угадывалось.
extern float lbmBlockageRatio;
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
// v1.36.0 — мёртвые переключатели удалены, а не «заблокированы».
//
// До этой версии в LBMParams висели useMRT, useZouHeBC и useRegularized: они
// были видны в панели с подписью «НЕ РЕАЛИЗОВАНО», ничего не делали и
// дублировали диагностику. Поле, которое заведомо не читается решателем, —
// это не честность, а мусор: его видно в отчёте, его можно включить из
// старого файла настроек, и оно создаёт впечатление опции. Флаги удалены;
// что реально используется — написано в панели LBM одной строкой.
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
// v1.39.0 — пакетный шаг для автономного пре-расчёта: если updateWorldFields == false,
// мировые поля (lbmUxWorld..lbmPressure) и статистика сходимости обновляются
// только при необходимости (экономит память и время в пакете).
void stepLBMCPUEx(int steps, bool updateWorldFields);
// v1.29.0 — forceSteps: сколько шагов решётки сделать. >=0 задаёт число шагов
// явно (это путь фиксированного такта физики), -1 означает прежнее поведение —
// по lbmParams.stepsPerFrame с адаптацией к длине кадра.
void updateLBM(float deltaTime, int forceSteps = -1);

// v1.39.0 — Флагманский пресет и диагностика
void lbmApplyFlagshipPreset();
void lbmWarmupCoarseToFine(int coarseSteps = 100);
LBMCollisionMode lbmEffectiveCollisionMode();
LBMTurbulenceModel lbmEffectiveTurbulenceModel();
const char* lbmCollisionModeName(LBMCollisionMode m);
const char* lbmTurbulenceModelName(LBMTurbulenceModel m);
float lbmMaxNuTRatio();
float lbmMassDrift();
float lbmEnstrophy();
long long lbmFluidCellCount();
float lbmGetResidualRho();
float lbmGetResidualU();
bool lbmVerifyMRTOrthogonality();

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

// Асинхронный фоновый поток LBM для интерактивного режима (zero-blocking UI)
void lbmStopAsyncWorker();
bool lbmIsAsyncWorkerRunning();
// Сила на тело в Ньютонах по закону сохранения импульса в контрольном объёме (если применима)
bool lbmControlVolumeForceWorld(glm::vec3* outForceN);

// v1.35.0 — сила на тело методом обмена импульсом (momentum exchange).
// До этой версии сила считалась только интегрированием давления по поверхности
// треугольников (forces.cpp), то есть БЕЗ трения: на Re ~ 100 трение даёт до
// двух третей сопротивления, и Cd получался заниженным. Здесь суммируется
// импульс, который популяции передают твёрдым узлам на каждой граничной связи
// (для bounce-back это 2*f_q*c_q). Результат — в решёточных единицах
// (m=1, dx=1, dt=1 на ячейку); для коэффициентов этого достаточно:
//   Cd = Fx / (0.5 * rho0 * U0^2 * A_proj),  rho0 = 1.
// Для перевода в ньютоны: F_phys = F_lat * rho_phys * dx^3 / dt^2.
glm::vec3 lbmControlVolumeForce(float boxHalfX, float boxHalfY, float boxHalfZ);
// Проекция тела на плоскость, перпендикулярную потоку, в ячейках (для Cd/Cl).
float lbmProjectedAreaCells(int axis);
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
