#ifndef AEROS_RESULTS_VIEW_H
#define AEROS_RESULTS_VIEW_H
// =====================================================
// Aeros CAE — просмотр и оценка результатов
//
// Отдельный модуль, чтобы решатель не знал про интерфейсные вещи, а интерфейс не
// знал про сборку матриц. Здесь:
//   - описание поля результата (величина, единица, где лежит);
//   - легенда: диапазон авто или вручную, подписи с единицами;
//   - проба в точке (трилинейная интерполяция по узлам сетки);
//   - срез плоскостью (для картинки и для графиков);
//   - таблица ключевых величин;
//   - экспорт результатов и параметров исследования в CSV и в текстовый отчёт.
//
// Единицы: значения хранятся в СИ, а подпись и число для интерфейса получаются
// через StudyUnits (см. study.h). Никаких «магических» переводов в отображении.
//
// Точность не заявляется там, где программа её не проверяет: в отчёт попадают
// фактические невязка, число итераций, размерность сетки и предупреждения.
// =====================================================

#include "study.h"
#include "fem_mesh.h"

#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>

// -----------------------------------------------------
// Идентификатор поля результата. В интерфейсе показываются только те поля,
// которые действительно посчитаны (см. resultFieldAvailable).
// -----------------------------------------------------
enum class ResultFieldId {
    None = 0,
    DisplacementMag,   // |u|, м
    Ux, Uy, Uz,        // компоненты перемещений, м
    StressXX, StressYY, StressZZ,
    StressXY, StressXZ, StressYZ,
    VonMises,          // эквивалентное напряжение, Па
    Temperature,       // K
    HeatFluxMag,       // Вт/м2
    HeatFluxX, HeatFluxY, HeatFluxZ
};

struct ResultFieldInfo {
    ResultFieldId id;
    const char*   name;      // как подписывается в легенде
    int           unitKind;  // 0=length 1=stress 2=temp 3=heatflux 4=force 5=power 6=безразмерное
};

// Имя поля по идентификатору: для подписей и для честных сообщений об отказе.
inline const char* resultFieldName(ResultFieldId id);

inline const ResultFieldInfo* resultFieldTable(size_t* count) {
    static const ResultFieldInfo t[] = {
        { ResultFieldId::DisplacementMag, "Перемещение |u|",   0 },
        { ResultFieldId::Ux,              "Перемещение u_x",   0 },
        { ResultFieldId::Uy,              "Перемещение u_y",   0 },
        { ResultFieldId::Uz,              "Перемещение u_z",   0 },
        { ResultFieldId::VonMises,        "Напряжение фон Мизеса", 1 },
        { ResultFieldId::StressXX,        "Напряжение σ_xx",   1 },
        { ResultFieldId::StressYY,        "Напряжение σ_yy",   1 },
        { ResultFieldId::StressZZ,        "Напряжение σ_zz",   1 },
        { ResultFieldId::StressXY,        "Напряжение τ_xy",   1 },
        { ResultFieldId::StressXZ,        "Напряжение τ_xz",   1 },
        { ResultFieldId::StressYZ,        "Напряжение τ_yz",   1 },
        { ResultFieldId::Temperature,     "Температура",       2 },
        { ResultFieldId::HeatFluxMag,     "Тепловой поток |q|",3 },
        { ResultFieldId::HeatFluxX,       "Тепловой поток q_x",3 },
        { ResultFieldId::HeatFluxY,       "Тепловой поток q_y",3 },
        { ResultFieldId::HeatFluxZ,       "Тепловой поток q_z",3 },
    };
    if (count) *count = sizeof(t) / sizeof(t[0]);
    return t;
}

inline const char* resultFieldName(ResultFieldId id) {
    size_t cnt = 0;
    const ResultFieldInfo* tab = resultFieldTable(&cnt);
    for (size_t i = 0; i < cnt; ++i) if (tab[i].id == id) return tab[i].name;
    return (cnt > 0) ? tab[0].name : "?";
}

inline const char* resultUnitName(int unitKind, StudyUnitSystem s) {
    switch (unitKind) {
        case 0: return unitNameLength(s);
        case 1: return unitNameStress(s);
        case 2: return unitNameTemp(s);
        case 3: return unitNameHeatFlux(s);
        case 4: return unitNameForce(s);
        case 5: return unitNamePower(s);
        default: return "";
    }
}
inline double resultUnitSize(int unitKind, const StudyUnits& u) {
    switch (unitKind) {
        case 0: return u.length;
        case 1: return u.stress;
        case 2: return u.temp;
        case 3: return u.heatFlux;
        case 4: return u.force;
        case 5: return u.power;
        default: return 1.0;
    }
}

// Доступно ли поле для текущего результата (чтобы интерфейс не показывал пустое).
inline const std::vector<double>* resultFieldData(const StudyResults& res, ResultFieldId id) {
    switch (id) {
        case ResultFieldId::DisplacementMag: return nullptr;  // считается на лету
        case ResultFieldId::Ux:        return res.ux.empty() ? nullptr : &res.ux;
        case ResultFieldId::Uy:        return res.uy.empty() ? nullptr : &res.uy;
        case ResultFieldId::Uz:        return res.uz.empty() ? nullptr : &res.uz;
        case ResultFieldId::StressXX:  return res.sxx.empty() ? nullptr : &res.sxx;
        case ResultFieldId::StressYY:  return res.syy.empty() ? nullptr : &res.syy;
        case ResultFieldId::StressZZ:  return res.szz.empty() ? nullptr : &res.szz;
        case ResultFieldId::StressXY:  return res.sxy.empty() ? nullptr : &res.sxy;
        case ResultFieldId::StressXZ:  return res.sxz.empty() ? nullptr : &res.sxz;
        case ResultFieldId::StressYZ:  return res.syz.empty() ? nullptr : &res.syz;
        case ResultFieldId::VonMises:  return res.vonMises.empty() ? nullptr : &res.vonMises;
        case ResultFieldId::Temperature: return res.temperature.empty() ? nullptr : &res.temperature;
        case ResultFieldId::HeatFluxMag: return res.heatFluxMag.empty() ? nullptr : &res.heatFluxMag;
        case ResultFieldId::HeatFluxX: return res.qx.empty() ? nullptr : &res.qx;
        case ResultFieldId::HeatFluxY: return res.qy.empty() ? nullptr : &res.qy;
        case ResultFieldId::HeatFluxZ: return res.qz.empty() ? nullptr : &res.qz;
        default: return nullptr;
    }
}

inline bool resultFieldAvailable(const StudyResults& res, ResultFieldId id) {
    if (id == ResultFieldId::DisplacementMag) return !res.ux.empty();
    return resultFieldData(res, id) != nullptr;
}

inline double resultFieldAt(const StudyResults& res, ResultFieldId id, size_t node) {
    if (id == ResultFieldId::DisplacementMag) {
        if (node >= res.ux.size()) return 0.0;
        const double a = res.ux[node], b = res.uy[node], c = res.uz[node];
        return std::sqrt(a*a + b*b + c*c);
    }
    const std::vector<double>* d = resultFieldData(res, id);
    if (!d || node >= d->size()) return 0.0;
    return (*d)[node];
}

// -----------------------------------------------------
// Легенда: диапазон шкалы. Авто — по фактическим данным с симметрией для знаковых
// величин (напряжения/потоки), вручную — задаёт пользователь. Диапазон хранится
// в СИ, а отображается в единицах интерфейса.
// -----------------------------------------------------
struct ResultScale {
    ResultFieldId field = ResultFieldId::None;
    bool   autoRange = true;
    double manualLo = 0.0;   // СИ
    double manualHi = 1.0;   // СИ
    // вычисленное
    double lo = 0.0, hi = 1.0;
    double dataMin = 0.0, dataMax = 0.0;
    int    unitKind = 1;
};

inline void resultScaleUpdate(ResultScale& sc, const StudyResults& res, bool symmetricZero) {
    sc.dataMin = 0.0; sc.dataMax = 0.0;
    const size_t n = (size_t)res.nodes;
    bool first = true;
    for (size_t i = 0; i < n; ++i) {
        const double v = resultFieldAt(res, sc.field, i);
        if (!std::isfinite(v)) continue;
        if (first) { sc.dataMin = sc.dataMax = v; first = false; }
        else { if (v < sc.dataMin) sc.dataMin = v; if (v > sc.dataMax) sc.dataMax = v; }
    }
    if (first) { sc.dataMin = 0.0; sc.dataMax = 1.0; }
    if (sc.autoRange) {
        sc.lo = sc.dataMin; sc.hi = sc.dataMax;
        if (symmetricZero) {
            const double m = std::max(std::fabs(sc.lo), std::fabs(sc.hi));
            sc.lo = -m; sc.hi = m;
        }
        if (sc.hi - sc.lo < 1e-12) { sc.hi = sc.lo + 1.0; }
    } else {
        sc.lo = std::min(sc.manualLo, sc.manualHi);
        sc.hi = std::max(sc.manualLo, sc.manualHi);
        if (sc.hi - sc.lo < 1e-12) sc.hi = sc.lo + 1e-9;
    }
}

// Нормированное положение значения в шкале [0..1] — для цвета и для легенды.
inline double resultScaleT(const ResultScale& sc, double valueSI) {
    const double d = sc.hi - sc.lo;
    if (!(d > 0.0)) return 0.0;
    double t = (valueSI - sc.lo) / d;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return t;
}

// -----------------------------------------------------
// Проба в точке: трилинейная интерполяция по узлам сетки.
// Если хотя бы один из 8 окружающих узлов не принадлежит телу, возвращается
// значение ближайшего существующего узла — экстраполяция через пустоту была бы
// выдуманным числом.
// -----------------------------------------------------
inline bool femSampleField(const FemMesh& mesh, const std::vector<double>& values,
                           double x, double y, double z, double& out) {
    out = 0.0;
    if (values.empty() || !mesh.built) return false;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
    const double fx = (x - mesh.ox) / mesh.hx;
    const double fy = (y - mesh.oy) / mesh.hy;
    const double fz = (z - mesh.oz) / mesh.hz;
    if (fx < -0.5 || fy < -0.5 || fz < -0.5) return false;
    if (fx > mesh.ex + 0.5 || fy > mesh.ey + 0.5 || fz > mesh.ez + 0.5) return false;
    int i0 = (int)std::floor(fx), j0 = (int)std::floor(fy), k0 = (int)std::floor(fz);
    i0 = std::min(std::max(i0, 0), mesh.nx - 2);
    j0 = std::min(std::max(j0, 0), mesh.ny - 2);
    k0 = std::min(std::max(k0, 0), mesh.nz - 2);
    const double tx = std::min(std::max(fx - i0, 0.0), 1.0);
    const double ty = std::min(std::max(fy - j0, 0.0), 1.0);
    const double tz = std::min(std::max(fz - k0, 0.0), 1.0);

    int ids[8], present = 0;
    for (int dk = 0; dk < 2; ++dk)
    for (int dj = 0; dj < 2; ++dj)
    for (int di = 0; di < 2; ++di) {
        const int id = mesh.nodeIndex[(size_t)mesh.nodeId(i0 + di, j0 + dj, k0 + dk)];
        ids[(dk*2 + dj)*2 + di] = id;
        if (id >= 0) present++;
    }
    if (present == 0) return false;
    if (present < 8) {
        // ближайший существующий узел
        double best = 1e30;
        for (int q = 0; q < 8; ++q) {
            if (ids[q] < 0) continue;
            const double dx = mesh.nodeX(ids[q]) - x, dy = mesh.nodeY(ids[q]) - y, dz = mesh.nodeZ(ids[q]) - z;
            const double d = dx*dx + dy*dy + dz*dz;
            if (d < best) { best = d; out = values[(size_t)ids[q]]; }
        }
        return true;
    }
    const double w[8] = {
        (1-tx)*(1-ty)*(1-tz), tx*(1-ty)*(1-tz), tx*ty*(1-tz), (1-tx)*ty*(1-tz),
        (1-tx)*(1-ty)*tz,     tx*(1-ty)*tz,     tx*ty*tz,     (1-tx)*ty*tz
    };
    double s = 0.0;
    for (int q = 0; q < 8; ++q) s += w[q] * values[(size_t)ids[q]];
    out = s;
    return std::isfinite(out);
}

// -----------------------------------------------------
// Срез плоскостью: регулярная сетка проб вдоль двух осей. Используется и для
// картинки (тепловая карта), и для графика вдоль линии.
// -----------------------------------------------------
struct ResultSlice {
    int axis = 0;             // нормаль среза: 0=X, 1=Y, 2=Z
    double coord = 0.0;       // координата плоскости, м
    int nu = 0, nv = 0;       // размеры выборки
    double u0 = 0, v0 = 0, du = 1, dv = 1;   // параметры сетки выборки, м
    std::vector<double> values;              // nu*nv, СИ
    std::vector<char>   valid;               // 1 = точка попала в тело
    double vmin = 0, vmax = 0;
    bool ok = false;
};

inline bool resultBuildSlice(ResultSlice& sl, const FemMesh& mesh,
                             const std::vector<double>& values,
                             int axis, double coord, int samples) {
    sl = ResultSlice();
    sl.axis = axis;
    sl.coord = coord;
    if (!mesh.built || values.empty() || samples < 2) return false;
    const int n = std::min(std::max(samples, 2), 256);
    sl.nu = sl.nv = n;
    // оси в плоскости среза
    double a0 = mesh.ox, a1 = mesh.oy, b0 = mesh.oz;
    double aSpan = mesh.ex * mesh.hx, bSpan = mesh.ez * mesh.hz;
    if (axis == 0) { a0 = mesh.oy; aSpan = mesh.ey * mesh.hy; b0 = mesh.oz; bSpan = mesh.ez * mesh.hz; }
    else if (axis == 1) { a0 = mesh.ox; aSpan = mesh.ex * mesh.hx; b0 = mesh.oz; bSpan = mesh.ez * mesh.hz; }
    else { a0 = mesh.ox; aSpan = mesh.ex * mesh.hx; b0 = mesh.oy; bSpan = mesh.ey * mesh.hy; }
    (void)a1;
    sl.u0 = a0; sl.v0 = b0;
    sl.du = (n > 1) ? aSpan / (n - 1) : 0.0;
    sl.dv = (n > 1) ? bSpan / (n - 1) : 0.0;
    sl.values.assign((size_t)n * n, 0.0);
    sl.valid.assign((size_t)n * n, 0);
    bool first = true;
    for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) {
        double x, y, z;
        const double a = a0 + i * sl.du, b = b0 + j * sl.dv;
        if (axis == 0) { x = coord; y = a; z = b; }
        else if (axis == 1) { x = a; y = coord; z = b; }
        else { x = a; y = b; z = coord; }
        double v = 0.0;
        if (femSampleField(mesh, values, x, y, z, v)) {
            sl.values[(size_t)j * n + i] = v;
            sl.valid[(size_t)j * n + i] = 1;
            if (first) { sl.vmin = sl.vmax = v; first = false; }
            else { if (v < sl.vmin) sl.vmin = v; if (v > sl.vmax) sl.vmax = v; }
        }
    }
    sl.ok = !first;
    return sl.ok;
}

// График вдоль линии (для вкладки «Графики»): N проб от точки A до точки B.
struct ResultLinePlot {
    std::vector<double> s;      // координата вдоль линии, м
    std::vector<double> v;      // значение, СИ
    bool ok = false;
};

inline bool resultBuildLine(ResultLinePlot& lp, const FemMesh& mesh,
                            const std::vector<double>& values,
                            double ax, double ay, double az,
                            double bx, double by, double bz, int samples) {
    lp = ResultLinePlot();
    if (!mesh.built || values.empty() || samples < 2) return false;
    const int n = std::min(std::max(samples, 2), 512);
    const double dx = bx - ax, dy = by - ay, dz = bz - az;
    const double len = std::sqrt(dx*dx + dy*dy + dz*dz);
    lp.s.reserve((size_t)n); lp.v.reserve((size_t)n);
    for (int i = 0; i < n; ++i) {
        const double t = (double)i / (double)(n - 1);
        double out = 0.0;
        if (femSampleField(mesh, values, ax + dx*t, ay + dy*t, az + dz*t, out)) {
            lp.s.push_back(len * t);
            lp.v.push_back(out);
        }
    }
    lp.ok = !lp.v.empty();
    return lp.ok;
}

// -----------------------------------------------------
// Таблица ключевых величин: (название, значение в СИ, единица-вид)
// -----------------------------------------------------
struct ResultKeyValue {
    std::string name;
    double      valueSI;
    int         unitKind;
    std::string note;
};

inline std::vector<ResultKeyValue> resultKeyValues(const StudyProject& pr, const FemMesh& mesh) {
    std::vector<ResultKeyValue> out;
    const StudyResults& r = pr.results;
    const StudyUnits u = pr.unitsSI();
    (void)u;
    auto add = [&](const char* name, double v, int kind, const char* note) {
        ResultKeyValue kv; kv.name = name; kv.valueSI = v; kv.unitKind = kind; kv.note = note ? note : "";
        out.push_back(kv);
    };
    add("Тип анализа", (double)pr.analysis, 6, studyAnalysisName(pr.analysis));
    add("Статус", (double)r.status, 6, studyStatusName(r.status));
    add("Элементов сетки", (double)r.elements, 6, "");
    add("Узлов сетки", (double)r.nodes, 6, "");
    add("Степеней свободы", (double)r.dofs, 6, "");
    add("Объём тела", r.volumeM3, 6, "м3 (по сетке)");
    add("Масса тела", r.massKg, 6, "кг (по объёму сетки и плотности материала)");
    add("Итераций решателя", (double)r.iterations, 6, "");
    add("Относительная невязка", r.finalResidual, 6, "||r||/||f||");
    add("Время решения", r.solveTimeMs, 6, "мс");
    if (r.analysis == StudyAnalysis::LinearStatic) {
        add("Макс. перемещение", r.maxDisplacement, 0, "");
        add("Макс. напряжение фон Мизеса", r.maxVonMises, 1, "");
        add("Предел текучести материала", pr.material().yieldStrength, 1, "справочно");
        if (pr.material().yieldStrength > 0.0)
            add("Запас по текучести", pr.material().yieldStrength / (r.maxVonMises > 0 ? r.maxVonMises : 1.0), 6,
                "σ_тек / σ_vm  (линейная постановка: без перераспределения)");
        add("Сумма приложенных сил", r.appliedLoadSum, 4, "");
        add("Сумма реакций", r.reactionResultant, 4, "проверка равновесия");
        add("Энергия деформации", r.strainEnergy, 5, "");
    } else if (r.analysis == StudyAnalysis::SteadyThermal) {
        add("Мин. температура", r.minTemperature, 2, "");
        add("Макс. температура", r.maxTemperature, 2, "");
        add("Макс. тепловой поток", r.maxHeatFlux, 3, "");
        add("Суммарный тепловой поток", r.totalHeatRate, 5, "через границы с заданными условиями");
        if (std::fabs(r.heatRateImposed) > 0.0)
            add("Заданный подвод тепла", r.heatRateImposed, 5, "тепловыделение и заданные потоки");
        add("Подведено через границы T", r.heatRateIn, 5, "по реакциям узлов с заданной температурой");
        add("Отведено через границы T", r.heatRateOut, 5, "включая съём конвекцией");
        if (std::fabs(r.heatRateConvective) > 0.0)
            add("Съём тепла конвекцией", r.heatRateConvective, 5,
                r.heatRateConvective > 0.0 ? "тепло уходит в среду" : "среда греет тело");
    }
    add("Модуль Юнга материала", pr.material().youngE, 1, pr.material().name.c_str());
    add("Коэффициент Пуассона", pr.material().poisson, 6, pr.material().name.c_str());
    add("Плотность материала", pr.material().density, 6, "кг/м3");
    add("Теплопроводность", pr.material().conductivity, 6, "Вт/(м·K)");
    (void)mesh;
    return out;
}

// -----------------------------------------------------
// Экспорт результатов в CSV: координаты узлов и выбранные поля (в СИ — файл не
// зависит от системы единиц интерфейса; система записывается в заголовок).
// -----------------------------------------------------
inline bool resultExportCSV(const FemMesh& mesh, const StudyProject& pr,
                            const std::vector<ResultFieldId>& fields, const std::string& path,
                            std::string& err) {
    err.clear();
    if (!mesh.built) { err = "нет сетки"; return false; }
    std::ofstream f(path);
    if (!f) { err = "не удалось открыть файл для записи"; return false; }
    f << "# Aeros Engine CAE results\n";
    f << "# project=" << pr.name << "\n";
    f << "# geometry=" << pr.filePath << "\n";
    f << "# analysis=" << studyAnalysisName(pr.analysis) << "\n";
    f << "# status=" << studyStatusName(pr.results.status) << "\n";
    f << "# material=" << pr.material().name
      << " E=" << pr.material().youngE << " Pa nu=" << pr.material().poisson
      << " rho=" << pr.material().density << " kg/m3 k=" << pr.material().conductivity << " W/(m*K)\n";
    f << "# mesh=" << mesh.ex << "x" << mesh.ey << "x" << mesh.ez
      << " elements, " << mesh.nodeCount << " nodes\n";
    f << "# units=SI (m, Pa, K, W/m2, N); display units in UI: " << unitSystemName(pr.units) << "\n";
    f << "# iterations=" << pr.results.iterations
      << " residual=" << pr.results.finalResidual
      << " time_ms=" << pr.results.solveTimeMs << "\n";
    f << "node,x_m,y_m,z_m,boundary";
    for (ResultFieldId id : fields) {
        size_t cnt = 0;
        const ResultFieldInfo* tab = resultFieldTable(&cnt);
        for (size_t t = 0; t < cnt; ++t)
            if (tab[t].id == id) {
                f << "," << tab[t].name;
                break;
            }
    }
    f << "\n";
    for (int nd = 0; nd < mesh.nodeCount; ++nd) {
        f << nd << "," << mesh.nodeX(nd) << "," << mesh.nodeY(nd) << "," << mesh.nodeZ(nd)
          << "," << (int)mesh.nodeIsBoundary[(size_t)nd];
        for (ResultFieldId id : fields) {
            const double v = resultFieldAt(pr.results, id, (size_t)nd);
            f << "," << v;
        }
        f << "\n";
    }
    if (!f.good()) { err = "ошибка записи"; return false; }
    return true;
}

// -----------------------------------------------------
// Текстовый отчёт по исследованию: постановка, сетка, решение, результаты,
// предупреждения. Входит в отладочный отчёт F10 и сохраняется отдельным файлом.
// -----------------------------------------------------
inline std::string studyBuildReport(const StudyProject& pr, const FemMesh& mesh) {
    std::ostringstream o;
    const StudyResults& r = pr.results;
    const StudyUnits u = pr.unitsSI();
    o << "=== Исследование: " << pr.name << " ===\n";
    o << "Геометрия: " << (pr.filePath.empty() ? "(не загружена)" : pr.filePath) << "\n";
    o << "Тип анализа: " << studyAnalysisName(pr.analysis) << "\n";
    o << "Единицы интерфейса: " << unitSystemName(pr.units)
      << " (ядро и файл проекта — СИ)\n\n";

    o << "--- Материал ---\n";
    const StudyMaterial& m = pr.material();
    char b[512];
    std::snprintf(b, sizeof(b), "%s: E=%.6g %s, nu=%.4g, rho=%.6g %s, k=%.6g %s, cp=%.6g Дж/(кг·K), σ_тек=%.6g %s\n",
                  m.name.c_str(),
                  unitToDisplay(m.youngE, u.stress), unitNameStress(pr.units),
                  m.poisson,
                  unitToDisplay(m.density, u.density), unitNameDensity(pr.units),
                  unitToDisplay(m.conductivity, u.conductivity), unitNameConductivity(pr.units),
                  m.specificHeat,
                  unitToDisplay(m.yieldStrength, u.stress), unitNameStress(pr.units));
    o << b;

    o << "\n--- Сетка ---\n";
    if (mesh.built) {
        std::snprintf(b, sizeof(b),
                      "гексаэдры Q1: %d x %d x %d = %d элементов, %d узлов, ячейка %.6g x %.6g x %.6g %s\n"
                      "объём тела %.6g м3, площадь поверхности %.6g м2, масса %.6g кг, несвязных частей %d\n",
                      mesh.ex, mesh.ey, mesh.ez, mesh.elemCount, mesh.nodeCount,
                      unitToDisplay(mesh.hx, u.length), unitToDisplay(mesh.hy, u.length),
                      unitToDisplay(mesh.hz, u.length), unitNameLength(pr.units),
                      mesh.volume, mesh.surfaceArea, mesh.massKg, mesh.components);
        o << b;
        o << "Метод построения: воксельная occupancy модели -> гексаэдры; геометрия ступенчатая с шагом ячейки.\n";
    } else {
        o << "сетка не построена\n";
    }

    o << "\n--- Нагрузки и граничные условия ---\n";
    if (pr.loads.empty()) o << "(нет)\n";
    for (size_t i = 0; i < pr.loads.size(); ++i) {
        const StudyLoad& l = pr.loads[i];
        std::snprintf(b, sizeof(b), "%zu. %s [%s]%s\n", i + 1, l.name.c_str(),
                      studyBCKindName(l.kind), l.enabled ? "" : " — ОТКЛЮЧЕНО");
        o << b;
        std::snprintf(b, sizeof(b), "   область: x[%.6g..%.6g] y[%.6g..%.6g] z[%.6g..%.6g] м%s%s\n",
                      l.region.minX, l.region.maxX, l.region.minY, l.region.maxY,
                      l.region.minZ, l.region.maxZ,
                      l.region.onlyBoundary ? ", только поверхность" : "",
                      (studyBCIsThermal(l.kind) || l.kind == StudyBCKind::Pressure)
                          ? l.region.normalName() : "");
        o << b;
        switch (l.kind) {
            case StudyBCKind::Fixed: o << "   u = 0\n"; break;
            case StudyBCKind::Displacement:
                std::snprintf(b, sizeof(b), "   u = (%.6g, %.6g, %.6g) %s\n",
                              unitToDisplay(l.vx,u.length), unitToDisplay(l.vy,u.length),
                              unitToDisplay(l.vz,u.length), unitNameLength(pr.units));
                o << b; break;
            case StudyBCKind::Force:
                std::snprintf(b, sizeof(b), "   F = (%.6g, %.6g, %.6g) %s (суммарно, распределена по узлам области)\n",
                              unitToDisplay(l.vx,u.force), unitToDisplay(l.vy,u.force),
                              unitToDisplay(l.vz,u.force), unitNameForce(pr.units));
                o << b; break;
            case StudyBCKind::Pressure:
                std::snprintf(b, sizeof(b), "   p = %.6g %s\n",
                              unitToDisplay(l.value,u.stress), unitNameStress(pr.units));
                o << b; break;
            case StudyBCKind::Gravity:
                std::snprintf(b, sizeof(b), "   g = (%.6g, %.6g, %.6g) м/с2\n", l.vx, l.vy, l.vz);
                o << b; break;
            case StudyBCKind::Temperature:
                std::snprintf(b, sizeof(b), "   T = %.6g %s\n", unitToDisplay(l.value,u.temp), unitNameTemp(pr.units));
                o << b; break;
            case StudyBCKind::HeatFlux:
                std::snprintf(b, sizeof(b), "   q = %.6g %s (в тело)\n",
                              unitToDisplay(l.value,u.heatFlux), unitNameHeatFlux(pr.units));
                o << b; break;
            case StudyBCKind::HeatGeneration:
                std::snprintf(b, sizeof(b), "   Q = %.6g %s\n",
                              unitToDisplay(l.value,u.heatGen), unitNameHeatGen(pr.units));
                o << b; break;
            case StudyBCKind::Convection:
                std::snprintf(b, sizeof(b), "   h = %.6g %s, T_окр = %.6g %s\n",
                              unitToDisplay(l.value,u.hCoeff), unitNameHCoeff(pr.units),
                              unitToDisplay(l.ambient,u.temp), unitNameTemp(pr.units));
                o << b; break;
        }
    }

    o << "\n--- Решатель ---\n";
    std::snprintf(b, sizeof(b),
                  "МКЭ (Q1-гексаэдры, полное интегрирование 2x2x2), метод сопряжённых градиентов\n"
                  "с диагональным предусловливанием; матрица не хранится (умножение по элементам).\n"
                  "предел итераций %d, допуск невязки %.1e, Jacobi %s\n",
                  pr.solver.maxIterations, pr.solver.relTolerance, pr.solver.jacobiPrecond ? "вкл" : "выкл");
    o << b;
    std::snprintf(b, sizeof(b), "статус: %s; итераций %d; невязка %.3e; время %.1f мс\n",
                  studyStatusName(r.status), r.iterations, r.finalResidual, r.solveTimeMs);
    o << b;
    if (!r.errorMessage.empty()) o << "сообщение: " << r.errorMessage << "\n";

    o << "\n--- Результаты ---\n";
    if (!r.valid) {
        o << "результатов нет (расчёт не завершён успешно)\n";
    } else if (r.analysis == StudyAnalysis::LinearStatic) {
        std::snprintf(b, sizeof(b),
                      "макс. перемещение |u| = %.6g %s\n"
                      "макс. напряжение фон Мизеса = %.6g %s\n"
                      "предел текучести материала = %.6g %s, запас = %.3f\n"
                      "сумма приложенных сил = %.6g %s, сумма реакций = %.6g %s\n"
                      "энергия деформации = %.6g %s\n",
                      unitToDisplay(r.maxDisplacement,u.length), unitNameLength(pr.units),
                      unitToDisplay(r.maxVonMises,u.stress), unitNameStress(pr.units),
                      unitToDisplay(m.yieldStrength,u.stress), unitNameStress(pr.units),
                      m.yieldStrength > 0 ? m.yieldStrength / (r.maxVonMises > 0 ? r.maxVonMises : 1.0) : 0.0,
                      unitToDisplay(r.appliedLoadSum,u.force), unitNameForce(pr.units),
                      unitToDisplay(r.reactionResultant,u.force), unitNameForce(pr.units),
                      unitToDisplay(r.strainEnergy,u.energy), unitNameEnergy(pr.units));
        o << b;
        if (m.yieldStrength > 0.0 && r.maxVonMises > m.yieldStrength)
            o << "ВНИМАНИЕ: максимальное напряжение фон Мизеса превышает предел текучести —\n"
                 "линейная постановка в этой области недействительна (нужен нелинейный расчёт).\n";
    } else if (r.analysis == StudyAnalysis::SteadyThermal) {
        // Печатаем по строке за раз: один большой snprintf сюда не помещается,
        // а обрезанная строка означала бы потерянное предупреждение о балансе.
        std::snprintf(b, sizeof(b),
                      "температура: мин %.6g %s, макс %.6g %s\n"
                      "макс. тепловой поток = %.6g %s\n"
                      "суммарный поток через границы = %.6g %s\n",
                      unitToDisplay(r.minTemperature,u.temp), unitNameTemp(pr.units),
                      unitToDisplay(r.maxTemperature,u.temp), unitNameTemp(pr.units),
                      unitToDisplay(r.maxHeatFlux,u.heatFlux), unitNameHeatFlux(pr.units),
                      unitToDisplay(r.totalHeatRate,u.power), unitNamePower(pr.units));
        o << b;
        std::snprintf(b, sizeof(b),
                      "подведено %.6g %s (в том числе заданный подвод %.6g %s), отведено %.6g %s\n",
                      unitToDisplay(r.heatRateIn,u.power), unitNamePower(pr.units),
                      unitToDisplay(r.heatRateImposed,u.power), unitNamePower(pr.units),
                      unitToDisplay(r.heatRateOut,u.power), unitNamePower(pr.units));
        o << b;
        if (std::fabs(r.heatRateConvective) > 0.0) {
            std::snprintf(b, sizeof(b), "съём тепла конвекцией = %.6g %s (%s)\n",
                          unitToDisplay(r.heatRateConvective,u.power), unitNamePower(pr.units),
                          r.heatRateConvective > 0.0 ? "тепло уходит в среду" : "среда греет тело");
            o << b;
        }
        // баланс сходится — строку о нём не пишем; расходится — пишем честно
        const double supplied = r.heatRateIn + r.heatRateImposed;
        const double removed  = r.heatRateOut;
        const double scale = std::max(std::fabs(supplied), std::fabs(removed));
        if (scale > 0.0 && std::fabs(supplied - removed) / scale > 0.02) {
            std::snprintf(b, sizeof(b),
                          "ВНИМАНИЕ: тепловой баланс не сходится — подведено %.6g %s, отведено %.6g %s"
                          " (расхождение %.1f%%)\n",
                          unitToDisplay(supplied,u.power), unitNamePower(pr.units),
                          unitToDisplay(removed,u.power), unitNamePower(pr.units),
                          std::fabs(supplied - removed) / scale * 100.0);
            o << b;
        }
    }

    if (!r.warnings.empty()) {
        o << "\n--- Предупреждения ---\n";
        for (const std::string& w : r.warnings) o << " * " << w << "\n";
    }
    o << "\n--- Ограничения методики ---\n";
    o << " * геометрия приближена ступенькой размером с ячейку сетки;\n";
    o << " * элементы Q1 при изгибе завышают жёсткость (shear locking) — перемещения\n"
         "   при изгибе занижены, лечится измельчением сетки;\n";
    o << " * постановка линейная: малые перемещения, линейно-упругий материал,\n"
         "   без контакта и без нелинейностей;\n";
    o << " * точность не заявляется: программа показывает невязку, число итераций и\n";
    o << "   предупреждения, а оценку сходимости по сетке делает пользователь\n";
    o << "   (кнопка «Сравнить 3 разрешения»).\n";
    return o.str();
}


// -----------------------------------------------------
// Состояние просмотра результатов в интерфейсе: выбранное поле, шкала, проба,
// срез, график, исследование сходимости. Отдельная структура — чтобы решатель и
// модель исследования ничего не знали про ImGui.
// -----------------------------------------------------
struct CaeViewState {
    // выбор в деревьях
    int studySel  = 1;   // CaeStudySel (cae_ui.h)
    int resultSel = 1;   // CaeResultSel

    // текущее поле и его шкала
    ResultFieldId field = ResultFieldId::VonMises;
    ResultScale   scale;
    bool colorGeometry   = true;   // раскрашивать модель по полю
    bool legendSymmetric = false;  // симметричный диапазон вручную

    // проба в точке
    bool   probeEnabled = false;
    bool   probeValid   = false;
    double probeX = 0, probeY = 0, probeZ = 0, probeValue = 0;

    // срез плоскостью
    bool        sliceEnabled = false;
    int         sliceAxis    = 0;      // 0=X, 1=Y, 2=Z
    double      sliceCoord   = 0;      // м
    int         sliceSamples = 48;
    ResultSlice slice;

    // график вдоль линии
    bool           lineEnabled = false;
    ResultLinePlot line;
    // Концы линии — часть состояния просмотра, а не локальные static внутри
    // панели: иначе они не сохранялись вместе с остальными параметрами (проба,
    // срез, шкала) и не были доступны ни тестам, ни настройкам.
    double lineAx = 0, lineAy = 0, lineAz = 0;
    double lineBx = 1, lineBy = 0, lineBz = 0;

    // исследование сходимости по сетке
    struct MeshPoint {
        int    resolution = 0;
        int    elements = 0, nodes = 0, iterations = 0;
        double residual = 0, timeMs = 0, maxValue = 0;
    };
    std::vector<MeshPoint> convergence;

    bool        solving = false;
    std::string statusLine;
};

inline CaeViewState& caeView() { static CaeViewState s; return s; }

#endif // AEROS_RESULTS_VIEW_H
