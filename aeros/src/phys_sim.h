#ifndef PHYS_SIM_H
#define PHYS_SIM_H
// =====================================================
// Aeros Engine v1.39.0 — Двухэтапный конвейер физики (C++17):
//   Этап 1: высокоскоростная предсимуляция LBM (+ Thermal) в фоне или CLI
//           с записью временного ряда и 3D-снимков поля;
//   Этап 2: постинспекция 3D-снимков (скруббинг во времени), спектральный
//           анализ (DFT -> Струхаль), классификация режима течения и подробный
//           отчёт о физическом поведении системы во времени (.md / .csv / .json).
//
// Вся физика и анализ выполняются строго на C++17 без зависимостей от
// OpenGL, GLFW и ImGui — модуль работает как в UI, так и в headless/CLI.
// =====================================================

#include <string>
#include <vector>

// Конфигурация прогона предсимуляции
struct PhysSimConfig {
    int  targetSteps        = 1500;   // сколько шагов LBM предрассчитать
    int  metricsEvery       = 5;      // частота снятия скаляров (шаги)
    int  snapshotEvery      = 30;     // частота снятия 3D-снимка поля (шаги)
    int  snapshotRes        = 24;     // разрешение куба снимка (NxNxN, 8..32)
    bool resetBeforeRun     = true;   // переинициализировать решатель перед прогоном
    bool flagshipMode       = true;   // применить флагманские настройки (MRT + WALE + BFL + Sponge)
    bool computeVorticity   = true;   // считать завихренность и Q-критерий на снимках
    bool autoAnalyze        = true;   // сразу выполнить анализ поведения после Этапа 1
    bool autoExport         = true;   // сразу выгрузить .md/.csv/.json после Этапа 1
    std::string outputDir   = "logs"; // каталог для выгрузки отчётов
    std::string runTag      = "aeros_sim"; // префикс имён файлов
};

// Один сэмпл скалярных и интегральных величин на шаге t
struct PhysSimSample {
    int   step = 0;
    float timePhys = 0.0f;       // физическое время (с) = step * dt_phys
    float avgRho = 1.0f;         // средняя плотность (LB)
    float avgKineticLB = 0.0f;   // средняя кинетическая энергия (LB)
    float maxVelLB = 0.0f;       // максимальная скорость (LB)
    float maxVelWorld = 0.0f;    // максимальная скорость (м/с)
    float enstrophy = 0.0f;      // энстрофия (1/с^2 в нормировке velScale^2)
    float tke = 0.0f;            // турбулентная кинетическая энергия (м^2/с^2)
    float maxNuTRatio = 0.0f;    // max(nu_t / nu_0)
    float residualRho = 0.0f;    // RMS изменения плотности
    float residualU = 0.0f;      // изменение max скорости
    float dragN = 0.0f;          // лобовое сопротивление (Н)
    float liftN = 0.0f;          // подъёмная сила (Н)
    float momentNm = 0.0f;       // момент (Н*м)
    float cd = 0.0f;             // коэффициент сопротивления Cd
    float cl = 0.0f;             // коэффициент подъёмной силы Cl
    float ld = 0.0f;             // аэродинамическое качество Cl/Cd
    float cpMin = 0.0f;          // минимум Cp по поверхности
    float cpMax = 0.0f;          // максимум Cp по поверхности (точка торможения)
    float cop[3] = {0, 0, 0};    // центр давления (мировые координаты)
    float wakeDeficit1L = 0.0f;  // дефицит скорости в следе на x + 1L
    float wakeDeficit2L = 0.0f;  // дефицит скорости в следе на x + 2L
    float wakeDeficit4L = 0.0f;  // дефицит скорости в следе на x + 4L
    float thermalMeanT = 0.0f;   // средняя безразмерная температура (если включена)
    float thermalNusselt = 1.0f; // число Нуссельта (если включено)
    long long negPops = 0;
    long long badPops = 0;
    long long uLimited = 0;
};

// Огрублённый 3D-снимок поля для постинспекции (Этап 2)
struct PhysSimSnapshot {
    int   step = 0;
    float timePhys = 0.0f;
    int   rx = 0, ry = 0, rz = 0;
    float minX = 0, minY = 0, minZ = 0;
    float maxX = 0, maxY = 0, maxZ = 0;
    float dragN = 0, liftN = 0, momentNm = 0;
    float cd = 0, cl = 0;
    float cop[3] = {0, 0, 0};
    float cpMin = 0, cpMax = 0;
    float maxVorticity = 0.0f;
    float maxQCrit = 0.0f;
    std::vector<unsigned char> solid; // 1 = твёрдое тело/земля, 0 = жидкость
    std::vector<float> p;             // давление (Па)
    std::vector<float> ux, uy, uz;    // скорость (м/с)
    std::vector<float> vort;          // |rot u|
    std::vector<float> qcrit;         // Q-критерий
};

// Классификация режима течения
enum class PhysFlowRegime {
    Unknown = 0,
    SteadyLaminar,            // стационарный ламинарный / установившийся
    PeriodicVortexShedding,   // периодический срыв вихрей (Карман)
    TurbulentUnsteady,        // развитый нестационарный / турбулентный
    TransitionalDeveloping,   // переходный / ещё развивается
    UnstableDiverged          // расходимость / нарушение устойчивости
};

// Хронологическое событие физического поведения
struct PhysSimEvent {
    int   step = 0;
    float timePhys = 0.0f;
    std::string phase;
    std::string title;
    std::string detail;
};

// Итоговый отчёт о физическом поведении системы
struct PhysBehaviorReport {
    bool valid = false;
    PhysFlowRegime regime = PhysFlowRegime::Unknown;
    std::string regimeName;
    std::string regimeReason;

    // Параметры постановки
    int   gridNx = 0, gridNy = 0, gridNz = 0;
    long long totalCells = 0;
    long long fluidCells = 0;
    float blockageRatio = 0.0f;
    float lChar = 1.0f;
    float refArea = 1.0f;
    float flowSpeed = 0.0f;
    float rePhys = 0.0f;
    float reLB = 0.0f;
    float mach = 0.0f;
    float tau0 = 0.6f;
    std::string collisionName;
    std::string turbulenceName;
    std::string boundarySummary;
    bool  curvedBFL = false;
    bool  thermalEnabled = false;

    // Длительность и производительность Этапа 1
    int   stepsSimulated = 0;
    float totalPhysTimeS = 0.0f;
    double wallTimeS = 0.0;
    double achievedMLUPS = 0.0;
    int   samplesCount = 0;
    int   snapshotsCount = 0;

    // Законы сохранения и устойчивость
    float rhoInitial = 1.0f;
    float rhoFinal = 1.0f;
    float rhoMin = 1.0f;
    float rhoMax = 1.0f;
    float massDriftRel = 0.0f;
    float maxMachLB = 0.0f;
    long long totalNegPops = 0;
    long long totalBadPops = 0;
    long long totalULimited = 0;
    bool  hadDivergence = false;

    // Фазы развития течения
    int   transientEndStep = 0;
    float transientEndTimeS = 0.0f;
    int   settledStartStep = 0;
    float settledStartTimeS = 0.0f;

    // Статистика в установившемся окне (последние 40% прогона)
    float cdMean = 0.0f, cdStd = 0.0f, cdMin = 0.0f, cdMax = 0.0f;
    float clMean = 0.0f, clStd = 0.0f, clMin = 0.0f, clMax = 0.0f;
    float ldMean = 0.0f;
    float dragMeanN = 0.0f, liftMeanN = 0.0f, momentMeanNm = 0.0f;
    float cpMinPeak = 0.0f, cpMaxStagnation = 0.0f;

    // Спектральный анализ (DFT по Cl и Cd)
    bool  hasDominantShedding = false;
    float dominantFreqHz = 0.0f;
    float strouhalNumber = 0.0f;
    float sheddingPeriodS = 0.0f;
    float spectralProminence = 0.0f; // отношение пика к среднему уровню спектра

    // Завихренность, турбулентность и след
    float peakEnstrophy = 0.0f;
    int   peakEnstrophyStep = 0;
    float finalEnstrophy = 0.0f;
    float peakTKE = 0.0f;
    float peakNuTRatio = 0.0f;
    float finalWakeDeficit1L = 0.0f;
    float finalWakeDeficit2L = 0.0f;
    float finalWakeDeficit4L = 0.0f;
    float finalResidualRho = 0.0f;
    float finalResidualU = 0.0f;

    // Хронология ключевых физических событий
    std::vector<PhysSimEvent> events;
    // Полный текстовый рассказ (Markdown)
    std::string markdownNarrative;
};

// Состояние двухэтапного движка (для UI и CLI)
enum class PhysSimStage {
    Idle = 0,         // ожидание запуска
    Simulating,       // Этап 1: идёт расчёт физики
    InspectReady,     // Этап 2: расчёт завершён, доступна инспекция и отчёт
    Error             // ошибка (например, нет модели / отмена без данных)
};

// --- Управление Этапом 1 (Предсимуляция) ---------------------------------
PhysSimConfig& physSimConfig();

// Блокирующий запуск (для CLI и headless-тестов). Возвращает true при успехе.
bool physSimRunBlocking(const PhysSimConfig& cfg);

// Асинхронный запуск в фоновом потоке C++17 (для UI без зависания окна).
bool physSimStartAsync(const PhysSimConfig& cfg);
void physSimCancel();
bool physSimIsRunning();
PhysSimStage physSimStage();
float physSimProgress();          // 0..1
int   physSimCurrentStepDone();
int   physSimTargetSteps();
double physSimElapsedSec();
double physSimCurrentMLUPS();
std::string physSimStatusText();

// Сброс накопленных результатов и отмена фоновой задачи при завершении программы
void physSimShutdown();
void physSimClear();

// --- Этап 2: Инспекция результатов, скруббинг 3D-снимков и отчёт ----------
int physSimSampleCount();
bool physSimGetSample(int idx, PhysSimSample* out);
const std::vector<PhysSimSample>& physSimSamples();

int physSimSnapshotCount();
bool physSimGetSnapshot(int idx, PhysSimSnapshot* out);
int physSimActiveSnapshotIndex();

// Загрузить выбранный предрассчитанный 3D-снимок в глобальные поля LBM и сил
// (для 3D-визуализации давления, скоростей, линий тока и векторов без пересчёта физики).
bool physSimApplySnapshotToWorld(int snapIdx);

// Анализ физического поведения по временному ряду и снимкам
bool physSimAnalyzeBehavior();
const PhysBehaviorReport& physSimReport();

// Экспорт полного аккаунта поведения (.md), временного ряда (.csv) и сводки (.json).
// Возвращает true, если файлы успешно записаны; пути записываются в out*Path (если != nullptr).
bool physSimExportAll(const std::string& outDir,
                      const std::string& tag,
                      std::string* outMdPath = nullptr,
                      std::string* outCsvPath = nullptr,
                      std::string* outJsonPath = nullptr);

const std::string& physSimLastMdPath();
const std::string& physSimLastCsvPath();
const std::string& physSimLastJsonPath();

#endif // PHYS_SIM_H
