#ifndef AEROS_STUDY_H
#define AEROS_STUDY_H
// =====================================================
// Aeros CAE — модель инженерного исследования (study)
//
// Единый объект, в котором живёт вся постановка задачи и её результаты:
//   геометрия (ссылка на загруженную модель и её воксельное представление),
//   материалы и их свойства,
//   сетка и её параметры,
//   нагрузки и граничные условия,
//   тип анализа,
//   настройки решателя,
//   результаты и сведения о завершении расчёта.
//
// ЕДИНИЦЫ. Вычислительное ядро всегда считает в СИ: метр, килограмм, секунда,
// кельвин, ньютон, паскаль, джоуль, ватт, Вт/(м·К), кг/м3, Вт/м2. Файл проекта
// тоже хранит значения в СИ (плюс запись о том, в какой системе их показывали).
// Интерфейс отображает величины в выбранной системе и подписывает единицы рядом
// с каждым числом. Так единицы интерфейса, файла и ядра не смешиваются:
// преобразование выполняется ровно один раз — на границе отображения, функциями
// unitToDisplay()/unitFromDisplay() ниже.
//
// Система «мм–МПа» собрана как в ANSYS (мм, т, с, K, Н, МПа, мДж, мВт): все
// производные единицы выводятся из базовых (длина, напряжение, время,
// температура), поэтому они СОГЛАСОВАНЫ — мощность получается в мВт,
// теплопроводность в мВт/(мм·K), плотность в т/мм3. Никаких «подгоночных»
// коэффициентов в коде нет: если система единиц выбрана верно, результат в СИ
// одинаков (это проверяет headless-тест).
// =====================================================

#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "lang.h"      // Language currentLanguage — заголовок самодостаточен

// -----------------------------------------------------
// Система единиц: базовые размеры единиц в СИ, всё остальное выводится
// -----------------------------------------------------
enum class StudyUnitSystem {
    SI = 0,      // м, кг, с, K, Н, Па, Дж, Вт
    mmMPa = 1    // мм, т, с, K, Н, МПа, мДж, мВт  (как в ANSYS)
};

struct StudyUnits {
    // размеры отображаемых единиц, выраженные в СИ
    double length;      // м
    double stress;      // Па
    double time;        // с
    double temp;        // K
    // производные (вычисляются, не задаются вручную)
    double force;       // Н
    double mass;        // кг
    double density;     // кг/м3
    double power;       // Вт
    double energy;      // Дж
    double conductivity;// Вт/(м·K)
    double heatFlux;    // Вт/м2
    double heatGen;     // Вт/м3
    double hCoeff;      // Вт/(м2·K)
};

// Размеры базовых единиц для выбранной системы (в СИ).
inline StudyUnits studyUnitsOf(StudyUnitSystem sys) {
    StudyUnits u;
    if (sys == StudyUnitSystem::mmMPa) { u.length = 1e-3; u.stress = 1e6; }
    else                               { u.length = 1.0;  u.stress = 1.0;  }
    u.time = 1.0;
    u.temp = 1.0;
    // Вывод из базовых: сила = напряжение*площадь, масса = сила*время^2/длина, ...
    u.force        = u.stress * u.length * u.length;
    u.mass         = u.force * u.time * u.time / u.length;
    u.density      = u.mass / (u.length * u.length * u.length);
    u.power        = u.force * u.length / u.time;
    u.energy       = u.power * u.time;
    u.conductivity = u.power / (u.length * u.temp);
    u.heatFlux     = u.power / (u.length * u.length);
    u.heatGen      = u.power / (u.length * u.length * u.length);
    u.hCoeff       = u.power / (u.length * u.length * u.temp);
    return u;
}

// Перевод СИ -> отображение и обратно (значение делится/умножается на размер единицы).
inline double unitToDisplay  (double si, double unitSize) { return si / unitSize; }
inline double unitFromDisplay(double disp, double unitSize) { return disp * unitSize; }

// Подписи единиц для интерфейса и отчётов. Всегда показываются рядом с числом.
inline const char* unitNameLength(StudyUnitSystem s)      { return s == StudyUnitSystem::mmMPa ? "мм" : "м"; }
inline const char* unitNameStress(StudyUnitSystem s)      { return s == StudyUnitSystem::mmMPa ? "МПа" : "Па"; }
inline const char* unitNameForce(StudyUnitSystem s)       { (void)s; return "Н"; }
inline const char* unitNameDensity(StudyUnitSystem s)     { return s == StudyUnitSystem::mmMPa ? "т/мм3" : "кг/м3"; }
inline const char* unitNamePower(StudyUnitSystem s)       { return s == StudyUnitSystem::mmMPa ? "мВт" : "Вт"; }
inline const char* unitNameEnergy(StudyUnitSystem s)      { return s == StudyUnitSystem::mmMPa ? "мДж" : "Дж"; }
inline const char* unitNameConductivity(StudyUnitSystem s){ return s == StudyUnitSystem::mmMPa ? "мВт/(мм·K)" : "Вт/(м·K)"; }
inline const char* unitNameHeatFlux(StudyUnitSystem s)    { return s == StudyUnitSystem::mmMPa ? "мВт/мм2" : "Вт/м2"; }
inline const char* unitNameHeatGen(StudyUnitSystem s)     { return s == StudyUnitSystem::mmMPa ? "мВт/мм3" : "Вт/м3"; }
inline const char* unitNameHCoeff(StudyUnitSystem s)      { return s == StudyUnitSystem::mmMPa ? "мВт/(мм2·K)" : "Вт/(м2·K)"; }
inline const char* unitNameTemp(StudyUnitSystem s)        { (void)s; return "K"; }
inline const char* unitSystemName(StudyUnitSystem s)      { return s == StudyUnitSystem::mmMPa ? "мм–МПа–т–мВт" : "СИ (м–кг–с–K)"; }

// -----------------------------------------------------
// Тип анализа. В интерфейсе доступны только реально работающие режимы:
// аэродинамика — решатель LBM (существует с ранних версий), линейная статика и
// стационарная теплопроводность — новый FEM-модуль (fem_mesh.h/fem_solver.h).
// -----------------------------------------------------
enum class StudyAnalysis {
    AeroCFD = 0,        // обтекание: LBM (существующие модули Aeros)
    LinearStatic = 1,   // линейный статический прочностной анализ
    SteadyThermal = 2   // стационарный тепловой анализ
};

inline const char* studyAnalysisName(StudyAnalysis a) {
    switch (a) {
        case StudyAnalysis::AeroCFD:       return "Аэродинамика (LBM)";
        case StudyAnalysis::LinearStatic:  return "Линейная статика";
        case StudyAnalysis::SteadyThermal: return "Стационарная теплопередача";
    }
    return "не задан";
}

// -----------------------------------------------------
// Статус узла дерева проекта и статус расчёта
// -----------------------------------------------------
enum class StudyNodeState {
    NotConfigured = 0,  // не настроено
    Ready,              // готово к расчёту
    Warning,            // есть предупреждение
    Error,              // ошибка постановки
    Solved              // расчёт выполнен
};

inline const char* studyNodeStateName(StudyNodeState s) {
    switch (s) {
        case StudyNodeState::NotConfigured: return "не настроено";
        case StudyNodeState::Ready:         return "готово";
        case StudyNodeState::Warning:       return "предупреждение";
        case StudyNodeState::Error:         return "ошибка";
        case StudyNodeState::Solved:        return "решено";
    }
    return "?";
}

enum class StudySolveStatus {
    NotStarted = 0,
    Preparing,      // подготовка: построение сетки, сборка
    Running,        // выполняется
    Converged,      // завершён, сходимость достигнута
    NotConverged,   // не сошёлся за отведённое число итераций
    Error           // ошибка (нет сетки, сингулярная задача, память)
};

inline const char* studyStatusName(StudySolveStatus s) {
    switch (s) {
        case StudySolveStatus::NotStarted:  return "не запускался";
        case StudySolveStatus::Preparing:   return "подготовка";
        case StudySolveStatus::Running:     return "выполняется";
        case StudySolveStatus::Converged:   return "завершён";
        case StudySolveStatus::NotConverged:return "не сошёлся";
        case StudySolveStatus::Error:       return "ошибка";
    }
    return "?";
}

// -----------------------------------------------------
// Материал: изотропный упругий + тепловые свойства
// -----------------------------------------------------
struct StudyMaterial {
    std::string name      = "Сталь конструкционная";
    double youngE         = 2.10e11;  // Па
    double poisson        = 0.30;     // -
    double density        = 7850.0;   // кг/м3
    double conductivity   = 45.0;     // Вт/(м·K)
    double specificHeat   = 460.0;    // Дж/(кг·K) — для справки/будущих нестационарных задач
    double yieldStrength  = 3.55e8;   // Па — предел текучести: сравнивается с фон Мизесом в отчёте
    bool   enabled        = true;

    // Физическая корректность: 0 < nu < 0.5 (иначе D вырождается), E > 0, k > 0.
    bool valid(char* why, size_t sz) const {
        if (!std::isfinite(youngE) || youngE <= 0.0) {
            std::snprintf(why, sz, "модуль Юнга должен быть больше нуля"); return false; }
        if (!std::isfinite(poisson) || poisson <= -1.0 || poisson >= 0.5) {
            std::snprintf(why, sz, "коэффициент Пуассона вне диапазона (-1; 0.5)"); return false; }
        if (!std::isfinite(density) || density < 0.0) {
            std::snprintf(why, sz, "плотность не может быть отрицательной"); return false; }
        if (!std::isfinite(conductivity) || conductivity <= 0.0) {
            std::snprintf(why, sz, "теплопроводность должна быть больше нуля"); return false; }
        why[0] = 0; return true;
    }
};

// Библиотека материалов (справочные значения при 20 °C). Значения можно менять —
// это только стартовая точка, в файле проекта хранятся отредактированные.
inline std::vector<StudyMaterial> studyMaterialLibrary() {
    std::vector<StudyMaterial> v;
    auto add = [&](const char* n, double E, double nu, double rho, double k, double cp, double sy) {
        StudyMaterial m; m.name = n; m.youngE = E; m.poisson = nu; m.density = rho;
        m.conductivity = k; m.specificHeat = cp; m.yieldStrength = sy; v.push_back(m);
    };
    add("Сталь конструкционная",  2.10e11, 0.30, 7850.0,  45.0,  460.0, 3.55e8);
    add("Сталь нержавеющая 304",  1.93e11, 0.29, 8000.0,  16.2,  500.0, 2.15e8);
    add("Алюминий 6061-T6",       6.89e10, 0.33, 2700.0, 167.0,  896.0, 2.76e8);
    add("Титан Ti-6Al-4V",        1.14e11, 0.34, 4430.0,  11.4,  526.0, 8.80e8);
    add("Медь M1",                1.17e11, 0.34, 8960.0, 401.0,  385.0, 7.00e7);
    add("Магний AZ31",            4.50e10, 0.35, 1770.0,  96.0, 1000.0, 2.00e8);
    add("Стекло силикатное",      7.00e10, 0.22, 2500.0,   1.0,  840.0, 3.00e7);
    add("Бетон B25",              3.00e10, 0.20, 2400.0,   1.7,  880.0, 2.50e7);
    add("ABS-пластик",            2.30e9,  0.35, 1050.0,   0.17, 1400.0, 4.00e7);
    add("PLA (печать)",           3.50e9,  0.36, 1240.0,   0.13, 1800.0, 6.00e7);
    add("Дуб (вдоль волокон)",    1.10e10, 0.30,  750.0,   0.15, 2000.0, 4.00e7);
    return v;
}

// -----------------------------------------------------
// Область приложения условия: прямоугольный бокс в координатах модели (СИ, метры)
// плюс признак «только узлы на внешней границе тела».
//
// Почему бокс: STL-поверхность не несёт топологии граней, поэтому named-выборки
// как в CAD здесь взять неоткуда. Бокс — честный и проверяемый способ задать
// область: он виден в свойствах, его можно подогнать по габаритам модели, а для
// типовых постановок (защемление одного торца, нагрузка на другом, горячая и
// холодная стенки) его достаточно. Пресеты «грань X-», «грань X+» и т.д.
// заполняют бокс по габариту модели автоматически.
// -----------------------------------------------------
struct StudyRegion {
    std::string name = "Область 1";
    double minX = 0, minY = 0, minZ = 0;
    double maxX = 0, maxY = 0, maxZ = 0;
    bool   onlyBoundary = false;  // только узлы на внешней поверхности тела
    bool   enabled      = true;

    // Фильтр по нормали грани — для условий, заданных на поверхности (давление,
    // тепловой поток, конвекция). Это аналог выбора ГРАНИ в CAD-пакете: без него
    // бокс с допуском в пол-ячейки захватывал и боковые грани у торца, и нагрузка
    // ложилась не туда, куда смотрел пользователь.
    //   normalAxis: -1 = любая нормаль, 0 = X, 1 = Y, 2 = Z
    //   normalPositive: true = нормаль в положительном направлении оси
    int    normalAxis     = -1;
    bool   normalPositive = true;

    bool contains(double x, double y, double z, double tol) const {
        return x >= minX - tol && x <= maxX + tol &&
               y >= minY - tol && y <= maxY + tol &&
               z >= minZ - tol && z <= maxZ + tol;
    }
    // Пресет грани: область задана плоскостью грани габарита (а не произвольным
    // боксом). Тогда узлы берутся в тонкой полосе у этой плоскости — см.
    // regionOnFacePlane(). Значение -1 означает «обычный бокс».
    bool isFacePreset() const { return normalAxis >= 0 && normalAxis <= 2; }

    // Координата плоскости грани вдоль оси фильтра: для «X+» это maxX, для «X-» minX.
    double facePlane() const {
        if (!isFacePreset()) return 0.0;
        if (normalAxis == 0) return normalPositive ? maxX : minX;
        if (normalAxis == 1) return normalPositive ? maxY : minY;
        return normalPositive ? maxZ : minZ;
    }

    // Узел лежит на плоскости грани (в пределах допуска). Для пресета грани это
    // главный критерий отбора: он не зависит от разрешения сетки и не пускает в
    // область второй слой узлов.
    bool regionOnFacePlane(double x, double y, double z, double tol) const {
        if (!isFacePreset()) return true;
        const double c = (normalAxis == 0) ? x : (normalAxis == 1 ? y : z);
        return std::fabs(c - facePlane()) <= tol;
    }

    // Подходит ли грань с данной внешней нормалью под условие области.
    bool matchesNormal(double fnx, double fny, double fnz) const {
        if (normalAxis < 0 || normalAxis > 2) return true;
        const double n = (normalAxis == 0) ? fnx : (normalAxis == 1 ? fny : fnz);
        return normalPositive ? (n > 0.5) : (n < -0.5);
    }
    const char* normalName() const {
        switch (normalAxis) {
            case 0: return normalPositive ? "грань X+" : "грань X-";
            case 1: return normalPositive ? "грань Y+" : "грань Y-";
            case 2: return normalPositive ? "грань Z+" : "грань Z-";
            default: return "любая грань";
        }
    }
    bool degenerate() const {
        return !(std::isfinite(minX) && std::isfinite(maxX) &&
                 std::isfinite(minY) && std::isfinite(maxZ) &&
                 std::isfinite(minZ) && std::isfinite(maxY)) ||
               maxX < minX || maxY < minY || maxZ < minZ;
    }
};

// -----------------------------------------------------
// Граничные условия и нагрузки
// -----------------------------------------------------
enum class StudyBCKind {
    // прочностной анализ
    Fixed = 0,        // защемление: u = 0
    Displacement,     // заданное перемещение (ux, uy, uz)
    Force,            // суммарная сила на область, распределённая по узлам (Fx, Fy, Fz)
    Pressure,         // давление на внешние грани области (нормаль наружу тела)
    Gravity,          // объёмная сила тяжести (g_x, g_y, g_z) — действует на всё тело
    // тепловой анализ
    Temperature,      // заданная температура на области
    HeatFlux,         // тепловой поток через внешние грани области (Вт/м2 в СИ)
    HeatGeneration,   // объёмное тепловыделение (Вт/м3 в СИ)
    Convection        // конвекция на внешних гранях: h и T_окр
};

inline const char* studyBCKindName(StudyBCKind k) {
    switch (k) {
        case StudyBCKind::Fixed:          return "Защемление";
        case StudyBCKind::Displacement:   return "Заданное перемещение";
        case StudyBCKind::Force:          return "Сила";
        case StudyBCKind::Pressure:       return "Давление на грани";
        case StudyBCKind::Gravity:        return "Сила тяжести";
        case StudyBCKind::Temperature:    return "Заданная температура";
        case StudyBCKind::HeatFlux:       return "Тепловой поток";
        case StudyBCKind::HeatGeneration: return "Тепловыделение в объёме";
        case StudyBCKind::Convection:     return "Конвекция";
    }
    return "?";
}

// К какому типу анализа относится условие (для дерева и валидации).
inline bool studyBCIsStructural(StudyBCKind k) {
    return k == StudyBCKind::Fixed || k == StudyBCKind::Displacement || k == StudyBCKind::Force ||
           k == StudyBCKind::Pressure || k == StudyBCKind::Gravity;
}
// Условия, заданные на ПОВЕРХНОСТИ тела (нужен выбор грани по нормали).
inline bool studyBCIsSurface(StudyBCKind k) {
    return k == StudyBCKind::Pressure || k == StudyBCKind::HeatFlux || k == StudyBCKind::Convection;
}

inline bool studyBCIsThermal(StudyBCKind k) {
    return k == StudyBCKind::Temperature || k == StudyBCKind::HeatFlux ||
           k == StudyBCKind::HeatGeneration || k == StudyBCKind::Convection;
}

struct StudyLoad {
    std::string name = "Условие 1";
    StudyBCKind kind = StudyBCKind::Fixed;
    StudyRegion region;
    int    materialIndex = 0;    // для условий, привязанных к материалу (не используется напрямую)
    // Значения ВСЕГДА в СИ. Интерфейс показывает их в выбранной системе единиц.
    double vx = 0, vy = 0, vz = 0;   // перемещение [м] / сила [Н] / ускорение [м/с2]
    double value = 0;                // давление [Па] / температура [K] / поток [Вт/м2]
                                     // / тепловыделение [Вт/м3] / h [Вт/(м2·K)]
    double ambient = 293.15;         // температура окружающей среды для конвекции [K]
    bool   enabled = true;
};

// -----------------------------------------------------
// Параметры сетки. Объёмная сетка — гексаэдры Q1, построенные по воксельному
// представлению геометрии (см. fem_mesh.h). STL сам по себе объёмной сеткой не
// является, поэтому поверхность сначала вокселизируется (это уже делает движок
// для LBM), а затем по заполненным ячейкам строятся гексаэдральные элементы.
// -----------------------------------------------------
struct StudyMeshParams {
    int  resolution       = 32;    // ячеек вдоль наибольшей стороны габарита
    int  resolutionCap    = 96;    // жёсткий предел: память и время растут как N^3
    bool useModelExtent   = true;  // домен = габарит модели (иначе — заданный бокс)
    double domMinX = 0, domMinY = 0, domMinZ = 0;
    double domMaxX = 1, domMaxY = 1, domMaxZ = 1;
    bool   includeGravityInMass = false; // считать ли массу тела в сводке
};

// -----------------------------------------------------
// Настройки решателя (МКЭ, метод сопряжённых градиентов с диагональным
// предусловливанием). Остатки, критерий сходимости и статус сохраняются в
// результатах и показываются во вкладке «Сходимость».
// -----------------------------------------------------
struct StudySolverSettings {
    int    maxIterations  = 5000;
    double relTolerance   = 1e-8;  // ||r|| / ||f|| — относительная невязка
    bool   jacobiPrecond  = true;
    bool   vonMises       = true;  // считать эквивалентное напряжение
    bool   reactions      = true;  // считать реакции в закреплениях
    bool   nodalAverage   = true;  // осреднять напряжения/потоки по узлам
    int    threads        = 0;     // 0 = все доступные
};

// -----------------------------------------------------
// Результаты расчёта. Поля хранятся по узлам сетки (fem_mesh.h) в СИ.
// -----------------------------------------------------
struct StudyResults {
    bool            valid      = false;
    StudyAnalysis   analysis   = StudyAnalysis::LinearStatic;
    StudySolveStatus status    = StudySolveStatus::NotStarted;

    // размеры задачи
    int    nodes = 0, elements = 0, dofs = 0;

    // прочностной анализ
    std::vector<double> ux, uy, uz;          // перемещения, м
    std::vector<double> sxx, syy, szz;       // напряжения, Па
    std::vector<double> sxy, sxz, syz;       // касательные, Па
    std::vector<double> vonMises;            // эквивалентное напряжение, Па
    std::vector<double> rx, ry, rz;          // реакции в закреплённых узлах, Н

    // тепловой анализ
    std::vector<double> temperature;         // K
    std::vector<double> qx, qy, qz;          // компоненты теплового потока, Вт/м2
    std::vector<double> heatFluxMag;         // модуль потока, Вт/м2

    // сводные величины
    double maxDisplacement   = 0;   // м
    double maxVonMises       = 0;   // Па
    double minTemperature    = 0;   // K
    double maxTemperature    = 0;   // K
    double maxHeatFlux       = 0;   // Вт/м2
    double reactionResultant = 0;   // Н — модуль суммы реакций
    double appliedLoadSum    = 0;   // Н — модуль суммы приложенных сил
    double totalHeatRate     = 0;   // Вт — сквозной поток через тело (max из подвода/отвода)
    double heatRateIn        = 0;   // Вт — подвод через границы с заданной температурой
    double heatRateOut       = 0;   // Вт — отвод через те же границы и через конвекцию
    double heatRateConvective = 0;  // Вт — съём тепла конвекцией (>0 — тепло уходит в среду)
    double heatRateImposed   = 0;   // Вт — заданный подвод: тепловыделение + тепловой поток
    std::vector<double> heatReaction;  // Вт на узел с заданной температурой (R = K·T - f)
    double strainEnergy      = 0;   // Дж
    double massKg            = 0;   // кг
    double volumeM3          = 0;   // м3

    // сходимость
    int    iterations       = 0;
    double initialResidual  = 0;
    double finalResidual    = 0;    // относительная невязка ||r||/||f||
    double solveTimeMs      = 0;
    // История невязки — в double. Раньше здесь был float: малые невязки
    // (1e-45…1e-160, что нормально для точно решённой системы) обнулялись,
    // и интерфейс показывал «начальная невязка 0» либо плоский график сходимости.
    std::vector<double> residualHistory;  // для графика сходимости
    std::vector<std::string> warnings;    // предупреждения расчёта
    std::string errorMessage;             // если status == Error

    void clear() {
        valid = false; status = StudySolveStatus::NotStarted;
        nodes = elements = dofs = 0;
        ux.clear(); uy.clear(); uz.clear();
        sxx.clear(); syy.clear(); szz.clear();
        sxy.clear(); sxz.clear(); syz.clear();
        vonMises.clear(); rx.clear(); ry.clear(); rz.clear();
        temperature.clear(); qx.clear(); qy.clear(); qz.clear(); heatFluxMag.clear();
        maxDisplacement = maxVonMises = minTemperature = maxTemperature = 0;
        maxHeatFlux = reactionResultant = appliedLoadSum = totalHeatRate = 0;
        heatRateIn = 0; heatRateOut = 0; heatRateConvective = 0; heatRateImposed = 0;
        heatReaction.clear();
        strainEnergy = massKg = volumeM3 = 0;
        iterations = 0; initialResidual = finalResidual = solveTimeMs = 0;
        residualHistory.clear(); warnings.clear(); errorMessage.clear();
    }
};

// -----------------------------------------------------
// Диагностика постановки: одно сообщение на узел дерева
// -----------------------------------------------------
struct StudyIssue {
    StudyNodeState level;   // Warning или Error
    std::string     node;   // имя узла дерева, к которому относится
    std::string     text;   // сообщение
};

// -----------------------------------------------------
// Исследование целиком
// -----------------------------------------------------
struct StudyProject {
    std::string          name      = "Исследование 1";
    std::string          filePath;              // откуда загружена геометрия (STL)
    std::string          projectPath;           // куда сохранён файл проекта
    StudyAnalysis        analysis  = StudyAnalysis::LinearStatic;
    StudyUnitSystem      units     = StudyUnitSystem::SI;

    std::vector<StudyMaterial> materials;
    std::vector<StudyLoad>     loads;           // условия и нагрузки
    StudyMeshParams            mesh;
    StudySolverSettings        solver;
    StudyResults               results;

    // геометрия (заполняется из модели движка)
    bool   geometryLoaded = false;
    double geomMinX = 0, geomMinY = 0, geomMinZ = 0;
    double geomMaxX = 0, geomMaxY = 0, geomMaxZ = 0;
    int    triangles = 0;
    double volumeM3  = 0;      // оценка объёма по воксельной сетке
    double areaM2    = 0;      // площадь поверхности модели

    // состояние
    StudySolveStatus        status = StudySolveStatus::NotStarted;
    std::vector<StudyIssue> issues;
    double                  progress = 0.0;   // 0..1 для строки состояния
    std::string             progressText;

    StudyProject() {
        // по умолчанию в списке три материала из библиотеки; остальную библиотеку
        // можно добавить кнопкой «из библиотеки» в панели материалов
        const std::vector<StudyMaterial> lib = studyMaterialLibrary();
        materials.clear();
        for (size_t i = 0; i < lib.size() && i < 3; ++i) materials.push_back(lib[i]);
    }

    const StudyUnits unitsSI() const { return studyUnitsOf(units); }

    // материал, назначенный исследованию (по умолчанию первый)
    int materialIndex = 0;
    const StudyMaterial& material() const {
        static StudyMaterial dummy;
        if (materials.empty()) return dummy;
        size_t i = (materialIndex >= 0 && materialIndex < (int)materials.size())
                       ? (size_t)materialIndex : 0;
        return materials[i];
    }
};

// Единственный экземпляр исследования (header-only модуль: состояние живёт в
// локальном static, поэтому нарушений ODR при включении из нескольких файлов нет).
inline StudyProject& studyProject() {
    static StudyProject p;
    return p;
}

// -----------------------------------------------------
// Пресеты областей: грани габарита модели. Заполняют бокс тонким слоем (5% от
// размера стороны), чтобы условие легло ровно на нужный торец.
// -----------------------------------------------------
inline StudyRegion studyFaceRegion(const StudyProject& pr, int axis, bool positive, const char* label) {
    StudyRegion r;
    r.name = label;
    // Пресет грани сразу задаёт и фильтр по внешней нормали: условие на грани
    // обязано ложиться ровно на эту грань, а не на «боксы поблизости».
    r.normalAxis = axis;
    r.normalPositive = positive;
    const double spanX = pr.geomMaxX - pr.geomMinX;
    const double spanY = pr.geomMaxY - pr.geomMinY;
    const double spanZ = pr.geomMaxZ - pr.geomMinZ;
    // Толщина слоя — 5% габарита и НЕ зависит от разрешения сетки. Точность
    // попадания обеспечивает не толщина, а два механизма:
    //   * для узлов — выбор слоя у ПЛОСКОСТИ грани (см. regionOnFacePlane и
    //     femSetupSystem): узлы берутся в полосе ±допуск вокруг плоскости,
    //     поэтому второй слой узлов в область не попадает ни при каком разрешении;
    //   * для граней — фильтр по внешней нормали (normalAxis/normalPositive).
    // Раньше толщина слоя бралась от разрешения сетки на момент вызова, и если
    // пользователь менял разрешение ПОСЛЕ создания условия, закрепление молча
    // уезжало на второй слой узлов (эффективная длина L-h, ошибка до 12%).
    double tx = spanX * 0.05, ty = spanY * 0.05, tz = spanZ * 0.05;
    if (!(tx > 0)) tx = 1e-4;
    if (!(ty > 0)) ty = 1e-4;
    if (!(tz > 0)) tz = 1e-4;
    r.minX = pr.geomMinX; r.maxX = pr.geomMaxX;
    r.minY = pr.geomMinY; r.maxY = pr.geomMaxY;
    r.minZ = pr.geomMinZ; r.maxZ = pr.geomMaxZ;
    if (axis == 0) { if (positive) r.minX = pr.geomMaxX - tx; else r.maxX = pr.geomMinX + tx; }
    if (axis == 1) { if (positive) r.minY = pr.geomMaxY - ty; else r.maxY = pr.geomMinY + ty; }
    if (axis == 2) { if (positive) r.minZ = pr.geomMaxZ - tz; else r.maxZ = pr.geomMinZ + tz; }
    r.onlyBoundary = false;
    return r;
}

// -----------------------------------------------------
// Валидация постановки. Возвращает список замечаний: ошибки не дают считать,
// предупреждения — дают, но видны в дереве и в отчёте.
//
// Здесь же честность интерфейса: сообщение ведёт к конкретному узлу или
// параметру, а не «что-то не так».
// -----------------------------------------------------
inline std::vector<StudyIssue> studyValidate(const StudyProject& pr) {
    std::vector<StudyIssue> out;
    auto add = [&](StudyNodeState lv, const char* node, const std::string& text) {
        StudyIssue i; i.level = lv; i.node = node; i.text = text; out.push_back(i);
    };

    if (!pr.geometryLoaded)
        add(StudyNodeState::Error, "Геометрия", "модель не загружена: откройте STL-файл");

    if (pr.analysis == StudyAnalysis::LinearStatic || pr.analysis == StudyAnalysis::SteadyThermal) {
        // материал
        if (pr.materials.empty())
            add(StudyNodeState::Error, "Материалы", "нет ни одного материала");
        else {
            char why[256];
            const StudyMaterial& m = pr.material();
            if (!m.valid(why, sizeof(why)))
                add(StudyNodeState::Error, "Материалы", std::string("материал «") + m.name + "»: " + why);
            if (pr.analysis == StudyAnalysis::LinearStatic && m.poisson > 0.45)
                add(StudyNodeState::Warning, "Материалы",
                    "коэффициент Пуассона 0.45 и выше: Q1-гексаэдры при почти несжимаемом "
                    "материале дают завышенную жёсткость (locking). Результат qualitatively верен, "
                    "но точность ниже.");
        }

        // сетка
        if (pr.mesh.resolution < 4)
            add(StudyNodeState::Error, "Сетка", "разрешение меньше 4 ячеек: тело не представится объёмом");
        if (pr.mesh.resolution > pr.mesh.resolutionCap)
            add(StudyNodeState::Error, "Сетка", "разрешение выше предела — уменьшите его");
        if (pr.mesh.resolution > 64)
            add(StudyNodeState::Warning, "Сетка",
                "разрешение выше 64: время решения и память растут как N^3, сходимость "
                "диагонального предусловливания замедляется");
        if (pr.mesh.resolution < 12 && pr.analysis == StudyAnalysis::LinearStatic)
            add(StudyNodeState::Warning, "Сетка",
                "грубая сетка (меньше 12 ячеек): для изгибных задач Q1-элементы дают "
                "заниженные перемещения. Проверьте сходимость по сетке.");

        // условия
        int nFixed = 0, nDisp = 0, nTemp = 0, nFluxLike = 0, nForceLike = 0;
        for (const StudyLoad& l : pr.loads) {
            if (!l.enabled) continue;
            if (l.region.degenerate()) {
                add(StudyNodeState::Error, "Нагрузки", "условие «" + l.name + "»: область вырождена");
                continue;
            }
            if (studyBCIsStructural(l.kind)) {
                if (pr.analysis != StudyAnalysis::LinearStatic) {
                    add(StudyNodeState::Warning, "Нагрузки",
                        "условие «" + l.name + "» относится к прочностному анализу и не действует "
                        "в режиме «" + studyAnalysisName(pr.analysis) + "»");
                }
                if (l.kind == StudyBCKind::Fixed) nFixed++;
                if (l.kind == StudyBCKind::Displacement) nDisp++;
                if (l.kind == StudyBCKind::Force || l.kind == StudyBCKind::Pressure ||
                    l.kind == StudyBCKind::Gravity) nForceLike++;
            } else {
                if (pr.analysis != StudyAnalysis::SteadyThermal) {
                    add(StudyNodeState::Warning, "Нагрузки",
                        "условие «" + l.name + "» относится к тепловому анализу и не действует "
                        "в режиме «" + studyAnalysisName(pr.analysis) + "»");
                }
                if (l.kind == StudyBCKind::Temperature) nTemp++;
                else nFluxLike++;
            }
        }
        if (pr.analysis == StudyAnalysis::LinearStatic) {
            if (nFixed == 0 && nDisp == 0)
                add(StudyNodeState::Error, "Закрепления",
                    "тело не закреплено: матрица жёсткости сингулярна, решение не существует. "
                    "Добавьте защемление или заданное перемещение.");
            if (nForceLike == 0 && nDisp == 0)
                add(StudyNodeState::Warning, "Нагрузки",
                    "нет ни одной нагрузки: перемещения будут нулевыми");
            if (nFixed > 0 && nForceLike == 0 && nDisp == 0)
                add(StudyNodeState::Warning, "Нагрузки",
                    "закрепление есть, а нагрузки нет — проверьте, что условие включено");
        }
        if (pr.analysis == StudyAnalysis::SteadyThermal) {
            if (nTemp == 0)
                add(StudyNodeState::Error, "Граничные условия",
                    "нет ни одной заданной температуры: стационарная задача без условия "
                    "Дирихле не имеет единственного решения (температура определяется с "
                    "точностью до постоянной)");
            if (nTemp > 0 && nFluxLike == 0)
                add(StudyNodeState::Warning, "Граничные условия",
                    "задана только температура, потоков нет: поле будет постоянным");
        }
    }

    // решатель
    if (pr.solver.maxIterations < 10)
        add(StudyNodeState::Error, "Решатель", "предел итераций меньше 10 — расчёт не успеет сойтись");
    // Бессмысленный допуск (NaN, Inf, <= 0 или >= 1) — ошибка: при нём условие
    // сходимости выполняется сразу, и расчёт рапортует «сошёлся», не решив систему.
    // Свободный, но осмысленный допуск остаётся предупреждением.
    if (!std::isfinite(pr.solver.relTolerance) || !(pr.solver.relTolerance > 0.0) ||
        pr.solver.relTolerance >= 1.0)
        add(StudyNodeState::Error, "Решатель",
            "допуск невязки задан бессмысленно: нужно число в диапазоне 0 < tol < 1 "
            "(иначе сходимость будет заявлена без решения системы)");
    else if (pr.solver.relTolerance > 1e-2)
        add(StudyNodeState::Warning, "Решатель",
            "допуск невязки слишком свободный (ожидается 1e-12..1e-2): результат может быть неточным");

    return out;
}

// Состояние узла дерева по списку замечаний: ошибка > предупреждение > готово.
inline StudyNodeState studyNodeState(const std::vector<StudyIssue>& issues, const char* node,
                                     bool configured, bool solved) {
    StudyNodeState st = configured ? StudyNodeState::Ready : StudyNodeState::NotConfigured;
    if (solved) st = StudyNodeState::Solved;
    for (const StudyIssue& i : issues) {
        if (i.node != node) continue;
        if (i.level == StudyNodeState::Error) return StudyNodeState::Error;
        if (i.level == StudyNodeState::Warning) st = StudyNodeState::Warning;
    }
    return st;
}

// -----------------------------------------------------
// Файл проекта (.aerosproj) — текстовый, по строке на параметр, как
// aeros_settings.ini. Значения ВСЕГДА в СИ: файл не зависит от того, в какой
// системе единиц работал пользователь. Система единиц сохраняется отдельно и
// влияет только на отображение.
// -----------------------------------------------------
inline bool studySaveProject(StudyProject& pr, const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    auto b = [](bool v) { return v ? 1 : 0; };
    f << "# Aeros Engine CAE project (values are stored in SI)\n";
    f << "name=" << pr.name << "\n";
    f << "geometry=" << pr.filePath << "\n";
    f << "analysis=" << (int)pr.analysis << "\n";
    f << "units=" << (int)pr.units << "\n";
    f << "materialIndex=" << pr.materialIndex << "\n";
    f << "meshResolution=" << pr.mesh.resolution << "\n";
    f << "meshResolutionCap=" << pr.mesh.resolutionCap << "\n";
    f << "meshUseModelExtent=" << b(pr.mesh.useModelExtent) << "\n";
    f << "solverMaxIterations=" << pr.solver.maxIterations << "\n";
    f << "solverTolerance=" << pr.solver.relTolerance << "\n";
    f << "solverJacobi=" << b(pr.solver.jacobiPrecond) << "\n";
    f << "solverVonMises=" << b(pr.solver.vonMises) << "\n";
    f << "solverReactions=" << b(pr.solver.reactions) << "\n";
    f << "materials=" << pr.materials.size() << "\n";
    for (size_t i = 0; i < pr.materials.size(); ++i) {
        const StudyMaterial& m = pr.materials[i];
        f << "mat" << i << ".name=" << m.name << "\n";
        f << "mat" << i << ".E=" << m.youngE << "\n";
        f << "mat" << i << ".nu=" << m.poisson << "\n";
        f << "mat" << i << ".rho=" << m.density << "\n";
        f << "mat" << i << ".k=" << m.conductivity << "\n";
        f << "mat" << i << ".cp=" << m.specificHeat << "\n";
        f << "mat" << i << ".yield=" << m.yieldStrength << "\n";
    }
    f << "loads=" << pr.loads.size() << "\n";
    for (size_t i = 0; i < pr.loads.size(); ++i) {
        const StudyLoad& l = pr.loads[i];
        f << "load" << i << ".name=" << l.name << "\n";
        f << "load" << i << ".kind=" << (int)l.kind << "\n";
        f << "load" << i << ".enabled=" << b(l.enabled) << "\n";
        f << "load" << i << ".vx=" << l.vx << "\n";
        f << "load" << i << ".vy=" << l.vy << "\n";
        f << "load" << i << ".vz=" << l.vz << "\n";
        f << "load" << i << ".value=" << l.value << "\n";
        f << "load" << i << ".ambient=" << l.ambient << "\n";
        f << "load" << i << ".rname=" << l.region.name << "\n";
        f << "load" << i << ".minX=" << l.region.minX << "\n";
        f << "load" << i << ".minY=" << l.region.minY << "\n";
        f << "load" << i << ".minZ=" << l.region.minZ << "\n";
        f << "load" << i << ".maxX=" << l.region.maxX << "\n";
        f << "load" << i << ".maxY=" << l.region.maxY << "\n";
        f << "load" << i << ".maxZ=" << l.region.maxZ << "\n";
        f << "load" << i << ".boundary=" << b(l.region.onlyBoundary) << "\n";
        // выбор грани по внешней нормали: без этих ключей перезагруженный проект
        // снова ловил бы боковые грани у нагруженного торца (давление/поток легли
        // бы не туда, куда указал пользователь).
        f << "load" << i << ".normalAxis=" << l.region.normalAxis << "\n";
        f << "load" << i << ".normalSign=" << b(l.region.normalPositive) << "\n";
    }
    pr.projectPath = path;
    return f.good();
}

inline bool studyLoadProject(StudyProject& pr, const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::vector<std::pair<std::string,std::string>> kv;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv.emplace_back(line.substr(0, eq), line.substr(eq + 1));
    }
    auto getS = [&](const std::string& k, std::string def) -> std::string {
        for (auto& p : kv) if (p.first == k) return p.second;
        return def;
    };
    auto getD = [&](const std::string& k, double def) -> double {
        for (auto& p : kv) if (p.first == k) { try { return std::stod(p.second); } catch (...) {} }
        return def;
    };
    auto getI = [&](const std::string& k, int def) -> int {
        for (auto& p : kv) if (p.first == k) { try { return std::stoi(p.second); } catch (...) {} }
        return def;
    };

    pr.name     = getS("name", pr.name);
    pr.filePath = getS("geometry", pr.filePath);
    pr.analysis = (StudyAnalysis)getI("analysis", (int)pr.analysis);
    pr.units    = (StudyUnitSystem)getI("units", (int)pr.units);
    pr.materialIndex = getI("materialIndex", 0);
    pr.mesh.resolution     = getI("meshResolution", pr.mesh.resolution);
    pr.mesh.resolutionCap  = getI("meshResolutionCap", pr.mesh.resolutionCap);
    if (pr.mesh.resolutionCap < 8)   pr.mesh.resolutionCap = 8;
    if (pr.mesh.resolutionCap > 512) pr.mesh.resolutionCap = 512;
    pr.mesh.useModelExtent = getI("meshUseModelExtent", 1) != 0;
    pr.solver.maxIterations = getI("solverMaxIterations", pr.solver.maxIterations);
    pr.solver.relTolerance  = getD("solverTolerance", pr.solver.relTolerance);
    pr.solver.jacobiPrecond = getI("solverJacobi", 1) != 0;
    pr.solver.vonMises      = getI("solverVonMises", 1) != 0;
    pr.solver.reactions     = getI("solverReactions", 1) != 0;
    if (pr.mesh.resolution < 4) pr.mesh.resolution = 4;
    if (pr.mesh.resolution > pr.mesh.resolutionCap) pr.mesh.resolution = pr.mesh.resolutionCap;
    if (pr.solver.maxIterations < 10) pr.solver.maxIterations = 10;
    // Из файла может прийти что угодно, включая «nan», «inf», 0 или 1e9:
    // такой допуск нельзя передавать решателю (см. femSafeRelTolerance).
    if (!std::isfinite(pr.solver.relTolerance) || !(pr.solver.relTolerance > 0.0) ||
        pr.solver.relTolerance >= 1.0)
        pr.solver.relTolerance = 1e-10;

    int nm = getI("materials", 0);
    if (nm > 0 && nm <= 64) {
        pr.materials.clear();
        for (int i = 0; i < nm; ++i) {
            StudyMaterial m;
            std::string p = "mat" + std::to_string(i) + ".";
            m.name          = getS(p + "name", "Материал");
            m.youngE        = getD(p + "E", 2.1e11);
            m.poisson       = getD(p + "nu", 0.3);
            m.density       = getD(p + "rho", 7850.0);
            m.conductivity  = getD(p + "k", 45.0);
            m.specificHeat  = getD(p + "cp", 460.0);
            m.yieldStrength = getD(p + "yield", 3.55e8);
            pr.materials.push_back(m);
        }
    }
    int nl = getI("loads", 0);
    if (nl >= 0 && nl <= 512) {
        pr.loads.clear();
        for (int i = 0; i < nl; ++i) {
            StudyLoad l;
            std::string p = "load" + std::to_string(i) + ".";
            l.name     = getS(p + "name", "Условие");
            l.kind     = (StudyBCKind)getI(p + "kind", 0);
            l.enabled  = getI(p + "enabled", 1) != 0;
            l.vx = getD(p + "vx", 0); l.vy = getD(p + "vy", 0); l.vz = getD(p + "vz", 0);
            l.value    = getD(p + "value", 0);
            l.ambient  = getD(p + "ambient", 293.15);
            l.region.name = getS(p + "rname", l.region.name);
            l.region.minX = getD(p + "minX", 0); l.region.minY = getD(p + "minY", 0);
            l.region.minZ = getD(p + "minZ", 0);
            l.region.maxX = getD(p + "maxX", 0); l.region.maxY = getD(p + "maxY", 0);
            l.region.maxZ = getD(p + "maxZ", 0);
            l.region.onlyBoundary = getI(p + "boundary", 0) != 0;
            l.region.normalAxis     = getI(p + "normalAxis", -1);
            l.region.normalPositive = getI(p + "normalSign", 1) != 0;
            if (l.region.normalAxis < -1 || l.region.normalAxis > 2) l.region.normalAxis = -1;
            // тип условия обязан быть из известного набора: иначе switch в решателе
            // не отработает ни одну ветку, и условие молча не применится
            if ((int)l.kind < 0 || (int)l.kind > (int)StudyBCKind::Convection)
                l.kind = StudyBCKind::Fixed;
            pr.loads.push_back(l);
        }
    }
    if (pr.materialIndex < 0 || pr.materialIndex >= (int)pr.materials.size()) pr.materialIndex = 0;
    pr.projectPath = path;
    return true;
}

#endif // AEROS_STUDY_H
