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
    int maxSteps = 10000;
    float convergenceThreshold = 1e-5f;
    bool useGround = false;
    float groundHeight = 0.0f;
    // v1.36.2 — TRT: две частоты релаксации вместо одной (BGK). s+ = 1/tau
    // отвечает за вязкость, s- выводится из «магического» параметра
    // L = (1/s+ - 1/2)(1/s- - 1/2) = 3/16. На точном решении (Пуазейль)
    // L2 = 0.071 % против 0.163 % у BGK; вязкость и дрейф массы те же.
    // По умолчанию выключено: включение сдвигает установившиеся числа во всех
    // эталонах, а они измеряются на коротком окне (см. docs/LBM-STABILITY...).
    bool useTRT = false;
    bool useConvectiveOutlet = true;
    float inletTurbulence = 0.02f;
    // v1.34.0 — масштаб внешней области вокруг модели: 1.0 = 2L вверх по потоку,
    // 4L вниз и 2.5L по бокам (стандарт для внешнего обтекания). Меньше 1 имеет
    // смысл только для быстрых тестов и предпросмотра: блокировка потока растёт,
    // и это честно пишется в журнал при инициализации.
    // 1.0 — стандарт внешнего обтекания (2L вверх по потоку, 4L вниз, 2.5L по
    // бокам). Но стоимость шага растёт как объём: на модели 48 ячеек по длине
    // это ~7.7 млн ячеек и ~2 с на шаг даже на 2 потоках. Поэтому по умолчанию
    // берётся четверть масштаба (0.5L/1L/0.6L) — след уже существует и виден,
    // а расчёт остаётся интерактивным. Увеличивать — ползунком в панели LBM,
    // с ростом перекрытия домена и времени шага.
    float domainPadding = 0.25f;
    // v1.34.0 — как выбирается вязкость (tau):
    //   true  — из физического числа Рейнольдса (Sutherland), с явным отчётом о
    //           том, какой Re реально разрешает решётка;
    //   false — ровно то, что задал пользователь ползунком tau.
    // Раньше стоял «тихий откат»: если tau из физики оказывалась ниже 0.52,
    // молча подставлялась пользовательская. В отчёте при этом оставался
    // физический Re, а считался совсем другой — цифры не соответствовали расчёту.
    bool autoViscosity = true;

    // v1.36.0 — ВНЕШНЯЯ ОБЪЁМНАЯ СИЛА и периодичность по X.
    //
    // Зачем это вообще нужно в решателе внешнего обтекания: эталонное течение
    // Пуазейля задаётся не скоростью на входе, а постоянным градиентом давления.
    // Без объёмной силы такой эталон в этом решателе невоспроизводим, то есть
    // проверить вязкость/пристеночное трение было не на чем (сравнивать Cd
    // с корреляцией — это проверка целого тракта, а не отдельной физики).
    //
    // Единицы — решёточные: a = F/rho, [F] = ячейка/шаг². Ноль по умолчанию,
    // поэтому на обычные расчёты поле не влияет вообще (слагаемое не добавляется).
    float bodyForceX = 0.0f;
    float bodyForceY = 0.0f;
    float bodyForceZ = 0.0f;
    // Периодичность по X: поток, выходящий за границу, входит с другой стороны
    // (масса и импульс сохраняются). Внешние вход/выход при этом отключаются —
    // иначе на одной и той же границе стояли бы два разных условия.
    bool periodicX = false;
    // Периодичность по Z: боковые границы канала. Нужна вместе с periodicX для
    // течения в канале: иначе на z-границах остаётся ветка «скопировать
    // популяцию на месте», которая не является физическим условием и на
    // длинном прогоне раскачивает поле.
    bool periodicZ = false;
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
