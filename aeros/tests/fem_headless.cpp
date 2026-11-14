// Headless-проверки модулей CAD/CAE (v1.31.0): исследование, объёмная сетка МКЭ,
// решатели, просмотр результатов и геометрия компоновки оболочки.
// Собирается и выполняется без OpenGL/GLFW/ImGui.
//
// Зачем эти тесты: требование к среде — «численные проверки и опорные задачи для
// каждого реально реализованного решателя». Поэтому здесь не «дымовые» проверки,
// а сравнение с аналитическими решениями:
//
//   1) единицы: согласованность выведенных размеров и перевод туда-обратно;
//   2) валидация постановки: замечания ведут к конкретному узлу;
//   3) сохранение/загрузка проекта (.aerosproj);
//   4) объёмная сетка: объём, площадь, масса, грани, обход узлов, несвязные части;
//   5) ПАТЧ-ТЕСТ МКЭ: линейное поле перемещений воспроизводится точно;
//   6) опорная задача «растяжение стержня»: σ = P/A, u = P·L/(E·A), реакции = P;
//   7) опорная задача «теплопроводность через плоскую стенку»: T линейна,
//      q = k·ΔT/L — сравнение с аналитическим решением;
//   8) сходимость по сетке: ключевая величина ведёт себя предсказуемо;
//   9) смена системы единиц НЕ меняет решение (единицы — только отображение);
//  10) просмотр результатов: проба, срез, график, шкала, таблица, CSV, отчёт;
//  11) честность отказов: нет сетки / закреплений / температуры, предел итераций,
//      сингулярная система — статус и сообщение, а не «нулевой ответ»;
//  12) компоновка оболочки: прямоугольники панелей, сплиттеры, сохранение.
#include "study.h"
#include "fem_mesh.h"
#include "fem_solver.h"
#include "results_view.h"
#include "ui_layout.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <limits>
#include <fstream>
#include <sstream>
#include <algorithm>

// lang.h объявляет текущий язык; тест не линкует lang.cpp.
Language currentLanguage = Language::Russian;

static int g_failed = 0;
static int g_checks = 0;
static void check(bool cond, const char* what) {
    ++g_checks;
    std::printf("  %s  %s\n", cond ? "ok:  " : "FAIL:", what);
    if (!cond) ++g_failed;
}
static void section(const char* name) { std::printf("\n--- %s ---\n", name); }

// Относительная погрешность (знаменатель защищён от нуля).
static double rel(double got, double want) {
    const double d = std::fabs(want) > 1e-30 ? std::fabs(want) : 1.0;
    return std::fabs(got - want) / d;
}

// -----------------------------------------------------
// Синтетическая геометрия: стержень 1.0 x 0.1 x 0.1 м (воксельная occupancy, как
// её строит движок из SDF модели). Тело прямоугольное — аналитические решения точны.
// -----------------------------------------------------
static const int BAR_R = 20;
static std::vector<int> makeBarVoxels() {
    return std::vector<int>((size_t)BAR_R * BAR_R * BAR_R, 1);
}

static FemMesh buildBar(int targetRes, double density = 7850.0) {
    FemMesh m;
    const std::vector<int> occ = makeBarVoxels();
    femMeshBuildFromVoxels(m, occ, BAR_R, BAR_R, BAR_R,
                           0.0, 0.0, 0.0, 1.0, 0.1, 0.1,
                           targetRes, 0, density);
    return m;
}

// Постановка «растяжение стержня давлением на торце X+».
static void setupBarTension(StudyProject& pr, double E, double nu, double P, double A, int res = 16) {
    pr = StudyProject();
    pr.name = "Стержень (растяжение)";
    pr.analysis = StudyAnalysis::LinearStatic;
    pr.units = StudyUnitSystem::SI;
    pr.geometryLoaded = true;
    pr.geomMinX = 0; pr.geomMinY = 0; pr.geomMinZ = 0;
    pr.geomMaxX = 1.0; pr.geomMaxY = 0.1; pr.geomMaxZ = 0.1;
    pr.materials.clear();
    StudyMaterial st;
    st.name = "Сталь"; st.youngE = E; st.poisson = nu; st.density = 7850.0;
    st.conductivity = 45.0; st.yieldStrength = 3.55e8;
    pr.materials.push_back(st);
    pr.materialIndex = 0;
    pr.mesh.resolution = res;          // ДО создания пресетов граней
    pr.solver.maxIterations = 20000;
    pr.solver.relTolerance = 1e-11;

    StudyLoad fix;
    fix.name = "Защемление X-";
    fix.kind = StudyBCKind::Fixed;
    fix.region = studyFaceRegion(pr, 0, false, "грань X-");
    pr.loads.push_back(fix);

    StudyLoad press;
    press.name = "Давление на торец X+";
    press.kind = StudyBCKind::Pressure;
    press.value = P / A;                       // Па
    press.region = studyFaceRegion(pr, 0, true, "грань X+");
    pr.loads.push_back(press);
}

// Постановка «плоская стенка»: T=400 K на X-, T=300 K на X+, k Вт/(м·K).
// Аналитическое решение: T(x) = 400 - 100·x, q = k·ΔT/L = 45·100/1 = 4500 Вт/м2.
static void setupSlab(StudyProject& pr, double k, int res = 16) {
    pr = StudyProject();
    pr.name = "Плоская стенка";
    pr.analysis = StudyAnalysis::SteadyThermal;
    pr.geometryLoaded = true;
    pr.geomMinX = 0; pr.geomMinY = 0; pr.geomMinZ = 0;
    pr.geomMaxX = 1.0; pr.geomMaxY = 0.1; pr.geomMaxZ = 0.1;
    pr.materials.clear();
    StudyMaterial st;
    st.name = "Сталь"; st.youngE = 2.1e11; st.poisson = 0.3; st.density = 7850.0;
    st.conductivity = k;
    pr.materials.push_back(st);
    pr.materialIndex = 0;
    pr.mesh.resolution = res;
    pr.solver.maxIterations = 20000;
    pr.solver.relTolerance = 1e-12;

    StudyLoad hot;
    hot.name = "T=400";
    hot.kind = StudyBCKind::Temperature;
    hot.value = 400.0;
    hot.region = studyFaceRegion(pr, 0, false, "грань X-");
    pr.loads.push_back(hot);

    StudyLoad cold;
    cold.name = "T=300";
    cold.kind = StudyBCKind::Temperature;
    cold.value = 300.0;
    cold.region = studyFaceRegion(pr, 0, true, "грань X+");
    pr.loads.push_back(cold);
}

// Среднее u_x по нагруженному торцу и среднее σ_xx по серединному сечению.
static double faceAvgUx(const FemMesh& m, const StudyResults& r, int& count) {
    double s = 0; count = 0;
    const double xEnd = m.ox + m.ex * m.hx;
    for (int i = 0; i < m.nodeCount; ++i)
        if (std::fabs(m.nodeX(i) - xEnd) < 1e-9) { s += r.ux[(size_t)i]; count++; }
    return count ? s / count : 0.0;
}
static double midAvgSxx(const FemMesh& m, const StudyResults& r, int& count) {
    double s = 0; count = 0;
    const double xMid = m.ox + 0.5 * m.ex * m.hx;
    for (int i = 0; i < m.nodeCount; ++i)
        if (std::fabs(m.nodeX(i) - xMid) < 0.5 * m.hx + 1e-9) { s += r.sxx[(size_t)i]; count++; }
    return count ? s / count : 0.0;
}

int main() {
    std::printf("=== Aeros Engine CAE: headless-проверки (v1.31.0) ===\n");
    char b[384];

    // =========================================================
    section("1. Единицы измерения: согласованность и перевод");
    // =========================================================
    {
        const StudyUnits si = studyUnitsOf(StudyUnitSystem::SI);
        const StudyUnits mm = studyUnitsOf(StudyUnitSystem::mmMPa);
        std::snprintf(b, sizeof(b), "СИ: длина %g, напряжение %g, сила %g, плотность %g",
                      si.length, si.stress, si.force, si.density);
        check(si.length == 1.0 && si.stress == 1.0 && si.force == 1.0 && si.density == 1.0, b);
        std::snprintf(b, sizeof(b), "мм-МПа: длина %g м, напряжение %g Па, сила %g Н, масса %g кг",
                      mm.length, mm.stress, mm.force, mm.mass);
        check(mm.length == 1e-3 && mm.stress == 1e6 && mm.force == 1.0 && mm.mass == 1e3, b);

        // согласованность ВЫВЕДЕННЫХ размеров — иначе единицы «поедут» в формулах
        for (int sysIdx = 0; sysIdx < 2; ++sysIdx) {
            const StudyUnits& u = (sysIdx == 0) ? si : mm;
            const char* nm = (sysIdx == 0) ? "СИ" : "мм-МПа";
            std::snprintf(b, sizeof(b), "%s: сила = напряжение·длина²", nm);
            check(rel(u.force, u.stress * u.length * u.length) < 1e-12, b);
            std::snprintf(b, sizeof(b), "%s: масса = сила·время²/длина", nm);
            check(rel(u.mass, u.force * u.time * u.time / u.length) < 1e-12, b);
            std::snprintf(b, sizeof(b), "%s: плотность = масса/длина³", nm);
            check(rel(u.density, u.mass / (u.length * u.length * u.length)) < 1e-12, b);
            std::snprintf(b, sizeof(b), "%s: мощность = сила·длина/время", nm);
            check(rel(u.power, u.force * u.length / u.time) < 1e-12, b);
            std::snprintf(b, sizeof(b), "%s: теплопроводность = мощность/(длина·температура)", nm);
            check(rel(u.conductivity, u.power / (u.length * u.temp)) < 1e-12, b);
            std::snprintf(b, sizeof(b), "%s: тепловой поток = мощность/длина²", nm);
            check(rel(u.heatFlux, u.power / (u.length * u.length)) < 1e-12, b);
            std::snprintf(b, sizeof(b), "%s: тепловыделение = мощность/длина³", nm);
            check(rel(u.heatGen, u.power / (u.length * u.length * u.length)) < 1e-12, b);
            std::snprintf(b, sizeof(b), "%s: коэффициент теплоотдачи = мощность/(длина²·температура)", nm);
            check(rel(u.hCoeff, u.power / (u.length * u.length * u.temp)) < 1e-12, b);
        }

        std::snprintf(b, sizeof(b), "2.1e11 Па = %.6g МПа (ожидалось 210000)",
                      unitToDisplay(2.1e11, mm.stress));
        check(rel(unitToDisplay(2.1e11, mm.stress), 210000.0) < 1e-12, b);
        std::snprintf(b, sizeof(b), "7850 кг/м3 = %.6g т/мм3 (ожидалось 7.85e-9)",
                      unitToDisplay(7850.0, mm.density));
        check(rel(unitToDisplay(7850.0, mm.density), 7.85e-9) < 1e-9, b);
        std::snprintf(b, sizeof(b), "45 Вт/(м·K) = %.6g в мм-системе (ожидалось 45)",
                      unitToDisplay(45.0, mm.conductivity));
        check(rel(unitToDisplay(45.0, mm.conductivity), 45.0) < 1e-12, b);
        std::snprintf(b, sizeof(b), "1 м = %.6g мм", unitToDisplay(1.0, mm.length));
        check(rel(unitToDisplay(1.0, mm.length), 1000.0) < 1e-12, b);

        const double vals[] = { 1e-9, 1e-3, 1.0, 2.1e11, 4500.0 };
        bool rt = true;
        for (double v : vals) {
            const double back = unitFromDisplay(unitToDisplay(v, mm.stress), mm.stress);
            if (rel(back, v) > 1e-12) rt = false;
        }
        check(rt, "перевод СИ -> отображение -> СИ возвращает исходное значение");
        check(std::strcmp(unitNameStress(StudyUnitSystem::mmMPa), "МПа") == 0 &&
              std::strcmp(unitNameLength(StudyUnitSystem::mmMPa), "мм") == 0 &&
              std::strcmp(unitNameTemp(StudyUnitSystem::SI), "K") == 0,
              "подписи единиц соответствуют системе");
    }

    // =========================================================
    section("2. Валидация постановки: замечания ведут к узлу");
    // =========================================================
    {
        StudyProject empty;
        std::vector<StudyIssue> iss = studyValidate(empty);
        std::snprintf(b, sizeof(b), "пустой проект: замечаний %zu (ожидается >= 3)", iss.size());
        check(iss.size() >= 3, b);
        bool nodesNamed = true, hasError = false;
        for (const StudyIssue& i : iss) {
            if (i.node.empty() || i.text.empty()) nodesNamed = false;
            if (i.level == StudyNodeState::Error) hasError = true;
        }
        check(nodesNamed, "у каждого замечания указаны УЗЕЛ и текст (не «что-то не так»)");
        check(hasError, "пустой проект даёт хотя бы одну ошибку");
        bool geomError = false;
        for (const StudyIssue& i : iss) if (i.node == "Геометрия") geomError = true;
        check(geomError, "отсутствующая геометрия отнесена к узлу «Геометрия»");

        StudyProject pr;
        setupBarTension(pr, 2.1e11, 0.3, 10000.0, 0.01);
        iss = studyValidate(pr);
        int errors = 0;
        for (const StudyIssue& i : iss) if (i.level == StudyNodeState::Error) errors++;
        std::snprintf(b, sizeof(b), "корректная постановка растяжения: ошибок %d", errors);
        check(errors == 0, b);

        StudyProject nofix = pr;
        nofix.loads.erase(nofix.loads.begin());
        iss = studyValidate(nofix);
        bool singularity = false;
        for (const StudyIssue& i : iss)
            if (i.level == StudyNodeState::Error && i.node == "Закрепления") singularity = true;
        check(singularity, "тело без закреплений: ошибка в узле «Закрепления» (сингулярная матрица)");

        StudyProject th;
        setupSlab(th, 45.0);
        iss = studyValidate(th);
        errors = 0;
        for (const StudyIssue& i : iss) if (i.level == StudyNodeState::Error) errors++;
        std::snprintf(b, sizeof(b), "корректная тепловая постановка: ошибок %d", errors);
        check(errors == 0, b);
        StudyProject noT = th;
        noT.loads.clear();
        iss = studyValidate(noT);
        bool dirichlet = false;
        for (const StudyIssue& i : iss)
            if (i.level == StudyNodeState::Error && i.node == "Граничные условия") dirichlet = true;
        check(dirichlet, "тепловая задача без заданной температуры: ошибка в узле «Граничные условия»");

        StudyProject bad = pr;
        bad.mesh.resolution = 2;
        iss = studyValidate(bad);
        bool meshErr = false;
        for (const StudyIssue& i : iss) if (i.node == "Сетка" && i.level == StudyNodeState::Error) meshErr = true;
        check(meshErr, "разрешение сетки 2: ошибка в узле «Сетка»");
        bad.mesh.resolution = bad.mesh.resolutionCap + 1;
        iss = studyValidate(bad);
        meshErr = false;
        for (const StudyIssue& i : iss) if (i.node == "Сетка" && i.level == StudyNodeState::Error) meshErr = true;
        check(meshErr, "разрешение выше предела: ошибка в узле «Сетка»");

        StudyProject lock = pr;
        lock.materials[0].poisson = 0.49;
        iss = studyValidate(lock);
        bool lockWarn = false;
        for (const StudyIssue& i : iss) if (i.node == "Материалы" && i.level == StudyNodeState::Warning) lockWarn = true;
        check(lockWarn, "ν = 0.49: предупреждение о locking в узле «Материалы»");

        StudyProject degen = pr;
        degen.loads[1].region.maxX = degen.loads[1].region.minX - 1.0;
        iss = studyValidate(degen);
        bool degenErr = false;
        for (const StudyIssue& i : iss) if (i.node == "Нагрузки" && i.level == StudyNodeState::Error) degenErr = true;
        check(degenErr, "вырожденная область нагрузки: ошибка в узле «Нагрузки»");

        StudyProject mixed = pr;
        StudyLoad tLoad;
        tLoad.name = "T=300"; tLoad.kind = StudyBCKind::Temperature; tLoad.value = 300.0;
        tLoad.region = studyFaceRegion(pr, 1, true, "грань Y+");
        mixed.loads.push_back(tLoad);
        iss = studyValidate(mixed);
        bool mixedWarn = false;
        for (const StudyIssue& i : iss)
            if (i.node == "Нагрузки" && i.level == StudyNodeState::Warning &&
                i.text.find("не действует") != std::string::npos) mixedWarn = true;
        check(mixedWarn, "тепловое условие в прочностном анализе: предупреждение «не действует»");

        StudyProject solv = pr;
        solv.solver.maxIterations = 5;
        iss = studyValidate(solv);
        bool solverErr = false;
        for (const StudyIssue& i : iss) if (i.node == "Решатель" && i.level == StudyNodeState::Error) solverErr = true;
        check(solverErr, "предел итераций 5: ошибка в узле «Решатель»");

        check(std::strcmp(studyNodeStateName(StudyNodeState::Solved), "решено") == 0 &&
              std::strcmp(studyStatusName(StudySolveStatus::NotConverged), "не сошёлся") == 0 &&
              std::strcmp(studyStatusName(StudySolveStatus::Error), "ошибка") == 0 &&
              std::strcmp(studyStatusName(StudySolveStatus::Preparing), "подготовка") == 0 &&
              std::strcmp(studyStatusName(StudySolveStatus::Running), "выполняется") == 0,
              "статусы именованы: подготовка/выполняется/завершён/не сошёлся/ошибка");
        check(studyBCIsSurface(StudyBCKind::Pressure) && studyBCIsSurface(StudyBCKind::HeatFlux) &&
              studyBCIsSurface(StudyBCKind::Convection) && !studyBCIsSurface(StudyBCKind::Force),
              "условия на поверхности помечены (для них нужен выбор грани)");

        // пресет грани: фильтр нормали и плоскость задаются сразу
        StudyRegion fr = studyFaceRegion(pr, 0, true, "грань X+");
        check(fr.isFacePreset() && fr.normalAxis == 0 && fr.normalPositive,
              "пресет грани X+ сразу задаёт фильтр по нормали");
        check(std::fabs(fr.facePlane() - pr.geomMaxX) < 1e-12, "плоскость пресета X+ — это maxX габарита");
        StudyRegion fr2 = studyFaceRegion(pr, 2, false, "грань Z-");
        check(fr2.normalAxis == 2 && !fr2.normalPositive && std::fabs(fr2.facePlane() - pr.geomMinZ) < 1e-12,
              "пресет грани Z-: нормаль Z-, плоскость minZ");
        check(fr.regionOnFacePlane(pr.geomMaxX, 0.05, 0.05, 0.01) &&
              !fr.regionOnFacePlane(pr.geomMaxX - 0.5, 0.05, 0.05, 0.01),
              "отбор узлов полосой у плоскости грани работает");
        StudyRegion box;
        box.minX = 0; box.maxX = 1; box.minY = 0; box.maxY = 1; box.minZ = 0; box.maxZ = 1;
        check(!box.isFacePreset() && box.regionOnFacePlane(0.5, 0.5, 0.5, 0.01),
              "обычный бокс (без фильтра нормали) отбирает узлы по объёму");
        check(fr.matchesNormal(1, 0, 0) && !fr.matchesNormal(0, 1, 0) && !fr.matchesNormal(-1, 0, 0),
              "фильтр нормали пропускает только грани X+");
    }

    // =========================================================
    section("3. Сохранение и загрузка проекта (.aerosproj)");
    // =========================================================
    {
        StudyProject pr;
        setupBarTension(pr, 2.1e11, 0.3, 10000.0, 0.01);
        pr.units = StudyUnitSystem::mmMPa;
        pr.mesh.resolution = 24;
        pr.solver.maxIterations = 7777;
        StudyMaterial extra;
        extra.name = "Алюминий"; extra.youngE = 7.0e10; extra.conductivity = 205.0;
        pr.materials.push_back(extra);
        const std::string path = "/tmp/aeros-test-study.aerosproj";
        check(studySaveProject(pr, path), "проект сохранился в файл");

        StudyProject loaded;
        check(studyLoadProject(loaded, path), "проект загрузился из файла");
        std::snprintf(b, sizeof(b), "после загрузки: материалов %zu (было %zu)",
                      loaded.materials.size(), pr.materials.size());
        check(loaded.materials.size() == pr.materials.size(), b);
        std::snprintf(b, sizeof(b), "после загрузки: условий %zu (было %zu)",
                      loaded.loads.size(), pr.loads.size());
        check(loaded.loads.size() == pr.loads.size(), b);
        check(loaded.analysis == StudyAnalysis::LinearStatic, "тип анализа сохранён");
        check(loaded.units == StudyUnitSystem::mmMPa, "система единиц сохранена");
        check(loaded.mesh.resolution == 24, "разрешение сетки сохранено");
        check(loaded.solver.maxIterations == 7777, "предел итераций сохранён");
        check(!loaded.materials.empty() && rel(loaded.materials[0].youngE, 2.1e11) < 1e-12,
              "модуль Юнга сохранён в СИ (без двойного перевода единиц)");
        check(loaded.materials.size() > 1 && rel(loaded.materials[1].conductivity, 205.0) < 1e-12,
              "теплопроводность второго материала сохранена");
        bool loadsSame = true;
        for (size_t i = 0; i < loaded.loads.size() && i < pr.loads.size(); ++i) {
            if (loaded.loads[i].kind != pr.loads[i].kind) loadsSame = false;
            if (std::fabs(loaded.loads[i].value - pr.loads[i].value) > 1e-6) loadsSame = false;
        }
        check(loadsSame, "типы и значения условий сохранены");
        check(loaded.loads.size() > 1 && loaded.loads[1].region.normalAxis == 0 &&
              loaded.loads[1].region.normalPositive,
              "фильтр грани (нормаль X+) сохранён — иначе перезагрузка ловила бы боковые грани");

        std::ifstream f(path);
        std::stringstream ss; ss << f.rdbuf();
        const std::string txt = ss.str();
        check(txt.find("analysis=") != std::string::npos &&
              txt.find("units=") != std::string::npos &&
              txt.find("mat0.E=") != std::string::npos &&
              txt.find("normalAxis=") != std::string::npos,
              "файл проекта текстовый и содержит параметры (analysis/units/mat0.E/normalAxis)");
        std::remove(path.c_str());

        StudyProject junk;
        const std::string badPath = "/tmp/aeros-test-junk.aerosproj";
        { std::ofstream o(badPath); o << "solverMaxIterations=не-число\nmeshResolution=-5\nunits=99\n"; }
        check(studyLoadProject(junk, badPath), "файл с мусором не роняет загрузку");
        check(junk.solver.maxIterations >= 10, "предел итераций после мусора приведён к допустимому");
        check(junk.mesh.resolution >= 4, "разрешение сетки после мусора приведено к допустимому");
        std::remove(badPath.c_str());
        StudyProject missing;
        check(!studyLoadProject(missing, "/tmp/нет-такого-файла.aerosproj"),
              "загрузка несуществующего файла возвращает отказ");
    }

    // =========================================================
    section("4. Объёмная сетка МКЭ по вокселям");
    // =========================================================
    {
        FemMesh m = buildBar(24);
        std::snprintf(b, sizeof(b), "стержень: построено %d элементов и %d узлов", m.elemCount, m.nodeCount);
        check(m.built && m.elemCount > 0 && m.nodeCount > 0, b);
        std::snprintf(b, sizeof(b), "размеры сетки %d x %d x %d", m.ex, m.ey, m.ez);
        check(m.ex > m.ey && m.ex > m.ez && m.ey >= 2 && m.ez >= 2, b);
        std::snprintf(b, sizeof(b), "объём %.9f м3 (аналитически 0.01)", m.volume);
        check(rel(m.volume, 0.01) < 1e-9, b);
        std::snprintf(b, sizeof(b), "площадь поверхности %.9f м2 (аналитически 0.42)", m.surfaceArea);
        check(rel(m.surfaceArea, 0.42) < 1e-9, b);
        std::snprintf(b, sizeof(b), "масса %.6f кг (0.01 м3 * 7850)", m.massKg);
        check(rel(m.massKg, 78.5) < 1e-9, b);
        std::snprintf(b, sizeof(b), "несвязных частей %d (ожидается 1)", m.components);
        check(m.components == 1, b);
        check(!m.faces.empty(), "внешние грани посчитаны (нужны для давления и потоков)");

        double sum = 0;
        for (const FemMesh::Face& f : m.faces) sum += f.area;
        std::snprintf(b, sizeof(b), "сумма площадей граней %.9f против площади поверхности %.9f",
                      sum, m.surfaceArea);
        check(rel(sum, m.surfaceArea) < 1e-9, b);

        bool normalsOk = true, areasOk = true;
        for (const FemMesh::Face& f : m.faces) {
            const double len = std::sqrt((double)f.nx * f.nx + (double)f.ny * f.ny + (double)f.nz * f.nz);
            if (std::fabs(len - 1.0) > 1e-5) normalsOk = false;
            if (!(f.area > 0)) areasOk = false;
        }
        check(normalsOk, "внешние нормали всех граней единичные");
        check(areasOk, "площади всех граней положительны");

        // Инвариант обхода узлов грани: нормаль, построенная по самим узлам
        // (p1-p0)x(p2-p1), обязана совпадать с сохранённой, все 4 узла — в одной
        // плоскости, а площадь из обхода — равна сохранённой. Именно эта проверка
        // ловит ошибку в таблице локальных узлов: раньше грань -z перечисляла узлы
        // со слоем k=1 — нагрузка на неё легла бы не на те узлы, а пометка границы
        // захватила лишний внутренний слой.
        bool windingOk = true, coplanarOk = true, areaOk = true;
        int facesChecked = 0;
        int seen[6] = {0,0,0,0,0,0};
        for (const FemMesh::Face& f : m.faces) {
            bool missing = false;
            for (int q = 0; q < 4; ++q) if (f.nodes[q] < 0) missing = true;
            if (missing) continue;
            const int ax = (std::fabs(f.nx) > 0.5f) ? 0 : (std::fabs(f.ny) > 0.5f ? 1 : 2);
            const double ncomp = (ax == 0) ? f.nx : (ax == 1 ? f.ny : f.nz);
            seen[ax * 2 + (ncomp > 0 ? 1 : 0)]++;
            const double p[4][3] = {
                { m.nodeX(f.nodes[0]), m.nodeY(f.nodes[0]), m.nodeZ(f.nodes[0]) },
                { m.nodeX(f.nodes[1]), m.nodeY(f.nodes[1]), m.nodeZ(f.nodes[1]) },
                { m.nodeX(f.nodes[2]), m.nodeY(f.nodes[2]), m.nodeZ(f.nodes[2]) },
                { m.nodeX(f.nodes[3]), m.nodeY(f.nodes[3]), m.nodeZ(f.nodes[3]) }
            };
            const double a0 = p[1][0]-p[0][0], a1 = p[1][1]-p[0][1], a2 = p[1][2]-p[0][2];
            const double c0 = p[2][0]-p[1][0], c1 = p[2][1]-p[1][1], c2 = p[2][2]-p[1][2];
            const double nx = a1*c2 - a2*c1, ny = a2*c0 - a0*c2, nz = a0*c1 - a1*c0;
            const double len = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (len < 1e-15) { windingOk = false; continue; }
            if (std::fabs(nx/len - f.nx) > 1e-6 || std::fabs(ny/len - f.ny) > 1e-6 ||
                std::fabs(nz/len - f.nz) > 1e-6) windingOk = false;
            if (std::fabs(len - f.area) > 1e-9 * std::max(1.0, f.area)) areaOk = false;
            // все 4 узла в одной плоскости: координата вдоль оси нормали одинакова
            bool same = true;
            for (int q = 1; q < 4; ++q) if (std::fabs(p[q][ax] - p[0][ax]) > 1e-12) same = false;
            if (!same) { coplanarOk = false; windingOk = false; }
            facesChecked++;
        }
        std::snprintf(b, sizeof(b), "обход узлов проверен у %d граней: нормаль из узлов совпадает", facesChecked);
        check(facesChecked > 0 && windingOk, b);
        check(areaOk, "площадь грани из обхода узлов равна сохранённой");
        check(coplanarOk, "все 4 узла каждой грани лежат в одной плоскости");
        std::snprintf(b, sizeof(b), "грани по ориентациям: -x %d, +x %d, -y %d, +y %d, -z %d, +z %d",
                      seen[0], seen[1], seen[2], seen[3], seen[4], seen[5]);
        check(seen[0] > 0 && seen[1] > 0 && seen[2] > 0 && seen[3] > 0 && seen[4] > 0 && seen[5] > 0, b);
        const int wantPerFace = (m.ey + 1) * (m.ez + 1);
        std::snprintf(b, sizeof(b), "граней X+ %d против %d узловых слоёв (ey+1)*(ez+1)... элементов %d",
                      seen[1], wantPerFace - 1, m.ey * m.ez);
        check(seen[1] == m.ey * m.ez && seen[0] == m.ey * m.ez, "граней ±x ровно по ey*ez");
        check(seen[2] == m.ex * m.ez && seen[3] == m.ex * m.ez, "граней ±y ровно по ex*ez");
        check(seen[4] == m.ex * m.ey && seen[5] == m.ex * m.ey, "граней ±z ровно по ex*ey");

        // плотные массивы
        std::snprintf(b, sizeof(b), "nodeIsBoundary: %zu записей при %d узлах",
                      m.nodeIsBoundary.size(), m.nodeCount);
        check((int)m.nodeIsBoundary.size() == m.nodeCount, b);
        std::snprintf(b, sizeof(b), "nodeIndex: %zu записей при %d ячейках сетки",
                      m.nodeIndex.size(), m.nx * m.ny * m.nz);
        check((int)m.nodeIndex.size() == m.nx * m.ny * m.nz, b);

        // Пометка границы: внутренний узел — тот, что не лежит ни на одной внешней
        // грани, то есть (ex-1)*(ey-1)*(ez-1) узлов. Формула общая для любой сетки.
        int bnd = 0;
        for (int i = 0; i < m.nodeCount; ++i) if (m.nodeIsBoundary[(size_t)i]) bnd++;
        const int wantBnd = m.nodeCount - (m.ex - 1) * (m.ey - 1) * (m.ez - 1);
        std::snprintf(b, sizeof(b), "стержень %dx%dx%d: граничных узлов %d против %d = всего %d минус внутренних %d",
                      m.ex, m.ey, m.ez, bnd, wantBnd, m.nodeCount,
                      (m.ex - 1) * (m.ey - 1) * (m.ez - 1));
        check(bnd == wantBnd, b);

        // куб 1x1x1 м, 16 элементов по стороне: внутренних узлов 15^3
        {
            const int CR = 16;
            std::vector<int> cube((size_t)CR * CR * CR, 1);
            FemMesh cm;
            femMeshBuildFromVoxels(cm, cube, CR, CR, CR, 0, 0, 0, 1.0, 1.0, 1.0, 16, 0, 7850.0);
            std::snprintf(b, sizeof(b), "куб: сетка %d x %d x %d, узлов %d", cm.ex, cm.ey, cm.ez, cm.nodeCount);
            check(cm.built && cm.ex == 16 && cm.ey == 16 && cm.ez == 16 && cm.nodeCount == 17*17*17, b);
            int cb = 0;
            for (int i = 0; i < cm.nodeCount; ++i) if (cm.nodeIsBoundary[(size_t)i]) cb++;
            const int wantB = 17*17*17 - 15*15*15;
            std::snprintf(b, sizeof(b), "куб: граничных узлов %d против %d (17^3 - 15^3)", cb, wantB);
            check(cb == wantB, b);
            std::snprintf(b, sizeof(b), "куб: объём %.9f м3 (аналитически 1)", cm.volume);
            check(rel(cm.volume, 1.0) < 1e-12, b);
            std::snprintf(b, sizeof(b), "куб: площадь %.9f м2 (аналитически 6)", cm.surfaceArea);
            check(rel(cm.surfaceArea, 6.0) < 1e-12, b);
            check(std::fabs(cm.aspectWorst - 1.0) < 1e-9, "куб: ячейки равносторонние");
            std::snprintf(b, sizeof(b), "куб: граней %zu против 6*16*16 = %d", cm.faces.size(), 6*16*16);
            check(cm.faces.size() == (size_t)6 * 16 * 16, b);
        }

        bool inside = true;
        for (int i = 0; i < m.nodeCount; ++i) {
            if (m.nodeX(i) < m.ox - 1e-9 || m.nodeX(i) > m.ox + m.ex * m.hx + 1e-9) inside = false;
            if (m.nodeY(i) < m.oy - 1e-9 || m.nodeY(i) > m.oy + m.ey * m.hy + 1e-9) inside = false;
            if (m.nodeZ(i) < m.oz - 1e-9 || m.nodeZ(i) > m.oz + m.ez * m.hz + 1e-9) inside = false;
        }
        check(inside, "координаты всех узлов внутри домена");
        check(femMeshTolerance(m) > 0.0, "допуск попадания в область условия положительный");

        // сфера: ступенчатая аппроксимация даёт известную погрешность объёма
        const int SR = 24;
        std::vector<int> sph((size_t)SR * SR * SR, 0);
        const double Rc = 0.4, cs = 1.0 / SR;
        for (int k = 0; k < SR; ++k)
        for (int j = 0; j < SR; ++j)
        for (int i = 0; i < SR; ++i) {
            const double x = (i + 0.5) * cs - 0.5, y = (j + 0.5) * cs - 0.5, z = (k + 0.5) * cs - 0.5;
            if (std::sqrt(x*x + y*y + z*z) <= Rc) sph[(size_t)(k * SR + j) * SR + i] = 1;
        }
        FemMesh sm;
        femMeshBuildFromVoxels(sm, sph, SR, SR, SR, -0.5, -0.5, -0.5, 0.5, 0.5, 0.5, 32, 0, 7850.0);
        const double vAnalytic = 4.0 / 3.0 * 3.14159265358979323846 * Rc * Rc * Rc;
        std::snprintf(b, sizeof(b), "сфера R=0.4: объём сетки %.5f м3 против аналитического %.5f (%+.1f%%)",
                      sm.volume, vAnalytic, (sm.volume / vAnalytic - 1.0) * 100.0);
        check(sm.built && rel(sm.volume, vAnalytic) < 0.06, b);
        bool staircaseWarn = false;
        for (const std::string& w : sm.warnings)
            if (w.find("ступеньк") != std::string::npos) staircaseWarn = true;
        check(staircaseWarn, "сетка честно предупреждает о ступенчатой аппроксимации геометрии");
        int sb = 0;
        for (int i = 0; i < sm.nodeCount; ++i) if (sm.nodeIsBoundary[(size_t)i]) sb++;
        std::snprintf(b, sizeof(b), "сфера: граничных узлов %d из %d", sb, sm.nodeCount);
        check(sb > 0 && sb < sm.nodeCount, b);

        std::vector<int> none((size_t)8 * 8 * 8, 0);
        FemMesh nm;
        const bool built = femMeshBuildFromVoxels(nm, none, 8, 8, 8, 0, 0, 0, 1, 1, 1, 16, 0, 1000.0);
        check(!built || nm.elemCount == 0, "пустая occupancy не даёт сетки из элементов");
    }

    // =========================================================
    section("5. Патч-тест МКЭ: линейное поле воспроизводится точно");
    // =========================================================
    {
        StudyProject pr;
        setupBarTension(pr, 2.1e11, 0.0, 10000.0, 0.01);
        pr.loads.clear();
        const double delta = 1e-3;
        StudyLoad fix;
        fix.name = "X- защемление"; fix.kind = StudyBCKind::Fixed;
        fix.region = studyFaceRegion(pr, 0, false, "грань X-");
        pr.loads.push_back(fix);
        StudyLoad disp;
        disp.name = "X+ перемещение"; disp.kind = StudyBCKind::Displacement;
        disp.vx = delta; disp.vy = 0.0; disp.vz = 0.0;
        disp.region = studyFaceRegion(pr, 0, true, "грань X+");
        pr.loads.push_back(disp);

        FemMesh m = buildBar(16);
        StudyResults r;
        std::string err;
        const bool ok = femSolveLinearStatic(m, pr, r, err);
        std::snprintf(b, sizeof(b), "патч-тест: статус %s, итераций %d, невязка %.2e %s",
                      studyStatusName(r.status), r.iterations, r.finalResidual, ok ? "" : err.c_str());
        check(ok && r.status == StudySolveStatus::Converged, b);

        double maxDev = 0.0;
        for (int i = 0; i < m.nodeCount; ++i) {
            const double want = delta * (m.nodeX(i) - m.ox) / (m.ex * m.hx);
            const double d = std::fabs(r.ux[(size_t)i] - want) / delta;
            if (d > maxDev) maxDev = d;
        }
        std::snprintf(b, sizeof(b), "линейное поле u_x = δ·x/L: макс. отклонение %.2e (доля от δ)", maxDev);
        check(maxDev < 1e-9, b);

        double maxLat = 0.0;
        for (int i = 0; i < m.nodeCount; ++i) {
            maxLat = std::max(maxLat, std::fabs(r.uy[(size_t)i]));
            maxLat = std::max(maxLat, std::fabs(r.uz[(size_t)i]));
        }
        std::snprintf(b, sizeof(b), "боковые перемещения нулевые: макс %.2e м", maxLat);
        check(maxLat < 1e-15, b);

        const double eps = delta / (m.ex * m.hx);
        const double wantSxx = pr.materials[0].youngE * eps;
        double devSxx = 0.0, maxTrans = 0.0;
        for (int i = 0; i < m.nodeCount; ++i) {
            devSxx = std::max(devSxx, std::fabs(r.sxx[(size_t)i] - wantSxx) / std::fabs(wantSxx));
            maxTrans = std::max(maxTrans, std::fabs(r.syy[(size_t)i]));
            maxTrans = std::max(maxTrans, std::fabs(r.szz[(size_t)i]));
            maxTrans = std::max(maxTrans, std::fabs(r.sxy[(size_t)i]));
        }
        std::snprintf(b, sizeof(b), "σ_xx = E·ε = %.6e Па во всех узлах: отклонение %.2e", wantSxx, devSxx);
        check(devSxx < 1e-9, b);
        std::snprintf(b, sizeof(b), "поперечные и касательные напряжения нулевые: макс %.2e Па", maxTrans);
        check(maxTrans < 1.0, b);

        double devVm = 0.0;
        for (int i = 0; i < m.nodeCount; ++i)
            devVm = std::max(devVm, std::fabs(r.vonMises[(size_t)i] - wantSxx) / wantSxx);
        std::snprintf(b, sizeof(b), "фон Мизес = σ_xx при одноосном растяжении: отклонение %.2e", devVm);
        check(devVm < 1e-9, b);

        const double wantW = 0.5 * wantSxx * eps * m.volume;
        std::snprintf(b, sizeof(b), "энергия деформации %.6e Дж против аналитической %.6e Дж",
                      r.strainEnergy, wantW);
        check(rel(r.strainEnergy, wantW) < 1e-9, b);
        check(r.valid, "результат помечен действительным после сходимости");
    }

    // =========================================================
    section("6. Опорная задача: растяжение стержня (σ = P/A, u = PL/EA)");
    // =========================================================
    {
        const double E = 2.1e11, P = 10000.0, A = 0.01, L = 1.0;
        const double uExact = P * L / (E * A);
        const double sExact = P / A;

        {   // ν = 0: решение совпадает с одномерной формулой точно
            StudyProject pr;
            setupBarTension(pr, E, 0.0, P, A);
            FemMesh m = buildBar(16);
            StudyResults r;
            std::string err;
            const bool ok = femSolveLinearStatic(m, pr, r, err);
            check(ok, "растяжение (ν=0) сошлось");
            int cnt = 0;
            const double uAvg = faceAvgUx(m, r, cnt);
            std::snprintf(b, sizeof(b), "u среднее по торцу %.9e м против P·L/(E·A) %.9e (отклонение %.2e)",
                          uAvg, uExact, rel(uAvg, uExact));
            check(rel(uAvg, uExact) < 1e-9, b);
            std::snprintf(b, sizeof(b), "узлов на нагруженном торце %d", cnt);
            check(cnt >= 4, b);
            const double sAvg = midAvgSxx(m, r, cnt);
            std::snprintf(b, sizeof(b), "σ_xx среднее по сечению %.9e Па против P/A %.9e", sAvg, sExact);
            check(rel(sAvg, sExact) < 1e-9, b);
            std::snprintf(b, sizeof(b), "сумма реакций %.6f Н против приложенных %.6f Н",
                          r.reactionResultant, r.appliedLoadSum);
            check(rel(r.reactionResultant, P) < 1e-6 && rel(r.appliedLoadSum, P) < 1e-6, b);
            std::snprintf(b, sizeof(b), "макс. фон Мизес %.6e Па (ожидается %.6e)", r.maxVonMises, sExact);
            check(rel(r.maxVonMises, sExact) < 1e-6, b);
            std::snprintf(b, sizeof(b), "макс. перемещение %.9e м", r.maxDisplacement);
            check(rel(r.maxDisplacement, uExact) < 1e-9, b);
            bool eqWarn = false;
            for (const std::string& w : r.warnings) if (w.find("равновес") != std::string::npos) eqWarn = true;
            check(!eqWarn, "равновесие выполнено: предупреждения о дисбалансе сил нет");
            check(!r.residualHistory.empty(), "история невязки сохранена (для вкладки «Сходимость»)");
            check(r.finalResidual <= pr.solver.relTolerance, "итоговая невязка не больше допуска");
            check(r.elements == m.elemCount && r.nodes == m.nodeCount && r.dofs == 3 * m.nodeCount,
                  "размер задачи в результатах совпадает с сеткой");
            check(r.volumeM3 > 0 && r.massKg > 0, "объём и масса тела попали в результаты");
            check((int)r.rx.size() == m.nodeCount, "реакции посчитаны по всем узлам");
            double rxSum = 0;
            for (double v : r.rx) rxSum += v;
            std::snprintf(b, sizeof(b), "сумма реакций по X %.6f Н (приложено %.1f Н)", rxSum, P);
            check(rel(std::fabs(rxSum), P) < 1e-6, b);
        }

        {   // ν = 0.3: защемлённый торец мешает пуассоновскому сужению
            StudyProject pr;
            setupBarTension(pr, E, 0.3, P, A);
            FemMesh m = buildBar(16);
            StudyResults r;
            std::string err;
            const bool ok = femSolveLinearStatic(m, pr, r, err);
            check(ok, "растяжение (ν=0.3) сошлось");
            int cnt = 0;
            const double uAvg = faceAvgUx(m, r, cnt);
            const double dev = uAvg / uExact - 1.0;
            std::snprintf(b, sizeof(b), "u среднее %.9e м: %+0.3f%% к одномерной формуле", uAvg, dev * 100.0);
            check(dev < 0.0 && dev > -0.02, b);
            const double sAvg = midAvgSxx(m, r, cnt);
            std::snprintf(b, sizeof(b), "σ_xx среднее %.9e Па при ν=0.3 (P/A = %.9e)", sAvg, sExact);
            check(rel(sAvg, sExact) < 1e-6, b);
            std::snprintf(b, sizeof(b), "реакции %.6f Н (приложено %.6f Н)", r.reactionResultant, P);
            check(rel(r.reactionResultant, P) < 1e-6, b);
            double uyTop = 0.0; int nTop = 0;
            const double yTop = m.oy + m.ey * m.hy;
            for (int i = 0; i < m.nodeCount; ++i)
                if (std::fabs(m.nodeY(i) - yTop) < 1e-9 &&
                    std::fabs(m.nodeX(i) - (m.ox + 0.5 * m.ex * m.hx)) < 0.5 * m.hx + 1e-9) {
                    uyTop += r.uy[(size_t)i]; nTop++;
                }
            if (nTop > 0) uyTop /= nTop;
            std::snprintf(b, sizeof(b), "пуассоновское сужение: u_y на верхней грани %.3e м (%d узлов)", uyTop, nTop);
            check(nTop > 0 && uyTop < 0.0, b);
        }

        {   // нагрузка силой вместо давления
            StudyProject pr;
            setupBarTension(pr, E, 0.3, P, A);
            pr.loads[1].kind = StudyBCKind::Force;
            pr.loads[1].vx = P; pr.loads[1].vy = 0; pr.loads[1].vz = 0;
            pr.loads[1].value = 0.0;
            pr.loads[1].region.onlyBoundary = true;
            FemMesh m = buildBar(16);
            StudyResults r;
            std::string err;
            const bool ok = femSolveLinearStatic(m, pr, r, err);
            check(ok, "растяжение сосредоточенной силой сошлось");
            int cnt = 0;
            const double sAvg = midAvgSxx(m, r, cnt);
            std::snprintf(b, sizeof(b), "сила: σ_xx среднее %.6e Па против P/A %.6e", sAvg, sExact);
            check(rel(sAvg, sExact) < 1e-6, b);
            std::snprintf(b, sizeof(b), "сила: приложено %.6f Н (задано %.1f)", r.appliedLoadSum, P);
            check(rel(r.appliedLoadSum, P) < 1e-9, b);
            std::snprintf(b, sizeof(b), "сила: реакции %.6f Н", r.reactionResultant);
            check(rel(r.reactionResultant, P) < 1e-6, b);
        }

        {   // сила тяжести: вес тела = ρ·g·V
            StudyProject pr;
            setupBarTension(pr, E, 0.3, 0.0, A);
            pr.loads.clear();
            StudyLoad fix;
            fix.name = "защемление"; fix.kind = StudyBCKind::Fixed;
            fix.region.minX = -0.01; fix.region.maxX = 1.01;
            fix.region.minY = -0.01; fix.region.maxY = 0.0005;
            fix.region.minZ = -0.01; fix.region.maxZ = 0.11;
            pr.loads.push_back(fix);
            StudyLoad g;
            g.name = "тяжесть"; g.kind = StudyBCKind::Gravity;
            g.vx = 0; g.vy = -9.81; g.vz = 0;
            g.region.minX = -1; g.region.maxX = 2;
            g.region.minY = -1; g.region.maxY = 2;
            g.region.minZ = -1; g.region.maxZ = 2;
            pr.loads.push_back(g);
            FemMesh m = buildBar(16);
            StudyResults r;
            std::string err;
            const bool ok = femSolveLinearStatic(m, pr, r, err);
            check(ok, "задача с силой тяжести сошлась");
            const double wantW = 7850.0 * 9.81 * m.volume;
            std::snprintf(b, sizeof(b), "вес тела: приложено %.6f Н против ρ·g·V = %.6f Н",
                          r.appliedLoadSum, wantW);
            check(rel(r.appliedLoadSum, wantW) < 1e-6, b);
            std::snprintf(b, sizeof(b), "реакции от веса %.6f Н", r.reactionResultant);
            check(rel(r.reactionResultant, wantW) < 1e-6, b);
            check(r.maxDisplacement > 0.0, "под действием тяжести перемещения ненулевые");
        }

        {   // давление на грань Z- (проверка исправленного обхода узлов грани -z)
            StudyProject pr;
            setupBarTension(pr, E, 0.0, 0.0, A);
            pr.loads.pop_back();
            StudyLoad p;
            p.name = "давление на Z-"; p.kind = StudyBCKind::Pressure;
            p.value = 1.0e5;                                  // 100 кПа на нижнюю грань
            p.region = studyFaceRegion(pr, 2, false, "грань Z-");
            pr.loads.push_back(p);
            FemMesh m = buildBar(16);
            StudyResults r;
            std::string err;
            const bool ok = femSolveLinearStatic(m, pr, r, err);
            check(ok, "давление на грань Z- сошлось");
            // сила = p * площадь грани Z- = 1e5 * (1.0 * 0.1) = 1e4 Н вдоль -z
            const double wantF = 1.0e5 * (1.0 * 0.1);
            std::snprintf(b, sizeof(b), "давление на Z-: приложено %.6f Н против p·A = %.6f",
                          r.appliedLoadSum, wantF);
            check(rel(r.appliedLoadSum, wantF) < 1e-6, b);
            std::snprintf(b, sizeof(b), "реакции %.6f Н", r.reactionResultant);
            check(rel(r.reactionResultant, wantF) < 1e-6, b);
            // перемещения в основном поперечные (z), а не вдоль x
            double maxUz = 0.0, maxUx = 0.0;
            for (int i = 0; i < m.nodeCount; ++i) {
                maxUz = std::max(maxUz, std::fabs(r.uz[(size_t)i]));
                maxUx = std::max(maxUx, std::fabs(r.ux[(size_t)i]));
            }
            std::snprintf(b, sizeof(b), "давление на Z-: макс |u_z| %.3e м, макс |u_x| %.3e м", maxUz, maxUx);
            check(maxUz > 0.0 && maxUz > maxUx, b);
            // давление легло именно на узлы плоскости z = 0 (не на соседний слой)
            int loadedNodes = 0;
            for (int i = 0; i < m.nodeCount; ++i)
                if (std::fabs(m.nodeZ(i)) < 1e-9) loadedNodes++;
            std::snprintf(b, sizeof(b), "узлов в плоскости Z-: %d против (ex+1)*(ey+1) = %d",
                          loadedNodes, (m.ex + 1) * (m.ey + 1));
            check(loadedNodes == (m.ex + 1) * (m.ey + 1), b);
            // узел второго слоя (z = hz) не закреплен и смещается лишь за счёт деформации:
            // его u_z обязан быть того же знака и меньше, чем у нагруженной грани
            double uzTop = 0.0; int nTop = 0;
            for (int i = 0; i < m.nodeCount; ++i)
                if (std::fabs(m.nodeZ(i) - m.oz - m.ez * m.hz) < 1e-9) { uzTop += r.uz[(size_t)i]; nTop++; }
            if (nTop) uzTop /= nTop;
            double uzBot = 0.0; int nBot = 0;
            for (int i = 0; i < m.nodeCount; ++i)
                if (std::fabs(m.nodeZ(i) - m.oz) < 1e-9) { uzBot += r.uz[(size_t)i]; nBot++; }
            if (nBot) uzBot /= nBot;
            std::snprintf(b, sizeof(b), "u_z: нагруженная грань %.4e м, противоположная %.4e м", uzBot, uzTop);
            check(nTop > 0 && nBot > 0 && std::fabs(uzBot) > std::fabs(uzTop), b);
        }
    }

    // =========================================================
    section("7. Опорная задача: теплопроводность через плоскую стенку");
    // =========================================================
    {
        const double k = 45.0, Th = 400.0, Tc = 300.0, L = 1.0;
        const double qExact = k * (Th - Tc) / L;      // 4500 Вт/м2
        const int ress[] = { 8, 12, 16, 24, 32 };
        for (int res : ress) {
            StudyProject pr;
            setupSlab(pr, k, res);
            FemMesh m = buildBar(res);
            StudyResults r;
            std::string err;
            const bool ok = femSolveSteadyThermal(m, pr, r, err);
            std::snprintf(b, sizeof(b), "разрешение %d: статус %s, итераций %d, невязка %.2e",
                          res, studyStatusName(r.status), r.iterations, r.finalResidual);
            check(ok && r.status == StudySolveStatus::Converged, b);

            double maxDev = 0.0;
            for (int i = 0; i < m.nodeCount; ++i) {
                const double want = Th - (Th - Tc) * (m.nodeX(i) - m.ox) / (m.ex * m.hx);
                maxDev = std::max(maxDev, std::fabs(r.temperature[(size_t)i] - want));
            }
            std::snprintf(b, sizeof(b), "разрешение %d: макс |T - T_аналит| = %.2e K", res, maxDev);
            check(maxDev < 1e-9, b);

            std::snprintf(b, sizeof(b), "разрешение %d: T мин %.6f / макс %.6f K", res,
                          r.minTemperature, r.maxTemperature);
            check(std::fabs(r.minTemperature - Tc) < 1e-9 && std::fabs(r.maxTemperature - Th) < 1e-9, b);
            std::snprintf(b, sizeof(b), "разрешение %d: q макс %.6f Вт/м2 против k·ΔT/L = %.1f",
                          res, r.maxHeatFlux, qExact);
            check(rel(r.maxHeatFlux, qExact) < 1e-9, b);
            const double wantRate = qExact * 0.01;   // A = 0.1*0.1 м2 -> 45 Вт
            std::snprintf(b, sizeof(b), "разрешение %d: сквозной поток %.6f Вт против q·A = %.1f",
                          res, r.totalHeatRate, wantRate);
            check(rel(r.totalHeatRate, wantRate) < 1e-6, b);
            std::snprintf(b, sizeof(b), "разрешение %d: подведено %.6f Вт, отведено %.6f Вт",
                          res, r.heatRateIn, r.heatRateOut);
            check(rel(r.heatRateIn, wantRate) < 1e-6 && rel(r.heatRateOut, wantRate) < 1e-6, b);
            bool balanceWarn = false;
            for (const std::string& w : r.warnings) if (w.find("баланс") != std::string::npos) balanceWarn = true;
            check(!balanceWarn, "тепловой баланс сходится — предупреждения нет");
            check(r.finalResidual <= pr.solver.relTolerance, "невязка не больше заданного допуска");
            check((int)r.temperature.size() == m.nodeCount && (int)r.qx.size() == m.nodeCount &&
                  (int)r.heatReaction.size() == m.nodeCount,
                  "размеры тепловых массивов совпадают с числом узлов");
        }

        {   // тепловыделение в объёме: поле нелинейное, баланс «вошло = вышло»
            StudyProject pr;
            setupSlab(pr, k);
            StudyLoad gen;
            gen.name = "тепловыделение"; gen.kind = StudyBCKind::HeatGeneration;
            gen.value = 1.0e5;                          // Вт/м3
            gen.region.minX = -1; gen.region.maxX = 2;
            gen.region.minY = -1; gen.region.maxY = 2;
            gen.region.minZ = -1; gen.region.maxZ = 2;
            pr.loads.push_back(gen);
            FemMesh m = buildBar(16);
            StudyResults r;
            std::string err;
            const bool ok = femSolveSteadyThermal(m, pr, r, err);
            check(ok, "стенка с тепловыделением сошлась");
            std::snprintf(b, sizeof(b), "с тепловыделением: T макс %.4f K (выше %.1f K без источника)",
                          r.maxTemperature, Th);
            check(r.maxTemperature > Th, b);
            double tMid = 0.0; int nMid = 0;
            const double xMid = m.ox + 0.5 * m.ex * m.hx;
            for (int i = 0; i < m.nodeCount; ++i)
                if (std::fabs(m.nodeX(i) - xMid) < 0.5 * m.hx + 1e-9) { tMid += r.temperature[(size_t)i]; nMid++; }
            if (nMid) tMid /= nMid;
            std::snprintf(b, sizeof(b), "T в середине %.4f K против 350 K линейного профиля", tMid);
            check(nMid > 0 && tMid > 350.0, b);
            // что выделилось — вышло через границы: отвод > подвода
            const double qGen = gen.value * m.volume;
            std::snprintf(b, sizeof(b), "выделилось %.4f Вт; заданный подвод %.4f Вт, отведено %.4f Вт",
                          qGen, r.heatRateImposed, r.heatRateOut);
            check(rel(r.heatRateImposed, qGen) < 1e-6, b);
            std::snprintf(b, sizeof(b), "баланс: подведено %.6f Вт, отведено %.6f Вт",
                          r.heatRateIn + r.heatRateImposed, r.heatRateOut);
            check(std::fabs((r.heatRateIn + r.heatRateImposed) - r.heatRateOut) <
                  0.02 * (r.heatRateIn + r.heatRateImposed), b);
            bool balanceWarn = false;
            for (const std::string& w : r.warnings) if (w.find("баланс") != std::string::npos) balanceWarn = true;
            check(!balanceWarn, "с тепловыделением баланс сходится — предупреждения нет");
        }

        {   // тепловой поток на грани: T линейна, но с другим наклоном
            StudyProject pr;
            setupSlab(pr, k);
            pr.loads.pop_back();                       // убрать T=300 на X+
            StudyLoad flux;
            flux.name = "поток"; flux.kind = StudyBCKind::HeatFlux;
            flux.value = -4500.0;                      // Вт/м2, отводим столько же
            flux.region = studyFaceRegion(pr, 0, true, "грань X+");
            pr.loads.push_back(flux);
            FemMesh m = buildBar(16);
            StudyResults r;
            std::string err;
            const bool ok = femSolveSteadyThermal(m, pr, r, err);
            check(ok, "задача «температура + поток» сошлась");
            std::snprintf(b, sizeof(b), "поток -4500 Вт/м2: T макс %.4f, мин %.4f K", r.maxTemperature, r.minTemperature);
            check(std::fabs(r.minTemperature - 300.0) < 1e-6, b);
            std::snprintf(b, sizeof(b), "q макс %.4f Вт/м2 (задано 4500)", r.maxHeatFlux);
            check(rel(r.maxHeatFlux, 4500.0) < 1e-6, b);
        }

        {   // Конвекция (граничное условие третьего рода, Робин).
            // Аналитика для плоской стенки:  q = (Th - Tamb)/(L/k + 1/h),
            //                                 T(x=1) = Th - q·L/k.
            // Здесь же проверяется ДЕТЕРМИНИРОВАННОСТЬ: раньше Robin-члены
            // применялись по парам «грань-узел» в параллельном цикле, один узел
            // обновлялся из нескольких нитей OpenMP, и поток «гулял» от запуска к
            // запуску (до -69% от аналитики). Два одинаковых решения обязаны
            // совпадать побитово.
            const double hConv = 50.0, Tamb = 300.0;
            const double qAnalytic = (Th - Tamb) / (L / k + 1.0 / hConv);
            const double tSurfAnalytic = Th - qAnalytic * L / k;

            StudyProject pr;
            setupSlab(pr, k);
            pr.loads.pop_back();
            StudyLoad conv;
            conv.name = "конвекция"; conv.kind = StudyBCKind::Convection;
            conv.value = hConv; conv.ambient = Tamb;
            conv.region = studyFaceRegion(pr, 0, true, "грань X+");
            pr.loads.push_back(conv);

            for (int res : { 16, 32, 64 }) {
                FemMesh m = buildBar(res);
                StudyResults r;
                std::string err;
                const bool ok = femSolveSteadyThermal(m, pr, r, err);
                std::snprintf(b, sizeof(b), "конвекция, разрешение %d: сошлось (%s)", res,
                              ok ? "да" : err.c_str());
                check(ok && r.status == StudySolveStatus::Converged, b);

                int nSurf = 0;
                double tSurf = 0.0, qSurf = 0.0;
                const double xEnd = m.ox + m.ex * m.hx;
                for (int i = 0; i < m.nodeCount; ++i)
                    if (std::fabs(m.nodeX(i) - xEnd) < 1e-9) {
                        tSurf += r.temperature[(size_t)i]; qSurf += r.qx[(size_t)i]; nSurf++;
                    }
                if (nSurf) { tSurf /= nSurf; qSurf /= nSurf; }
                std::snprintf(b, sizeof(b), "конвекция, разрешение %d: q %.6f Вт/м2 против %.6f",
                              res, qSurf, qAnalytic);
                check(nSurf > 0 && rel(qSurf, qAnalytic) < 1e-9, b);
                std::snprintf(b, sizeof(b), "конвекция, разрешение %d: T стенки %.6f K против %.6f",
                              res, tSurf, tSurfAnalytic);
                check(rel(tSurf, tSurfAnalytic) < 1e-9, b);
                const double hEff = qSurf / (tSurf - Tamb);
                std::snprintf(b, sizeof(b), "конвекция, разрешение %d: эффективный h %.6f (задано %.1f)",
                              res, hEff, hConv);
                check(rel(hEff, hConv) < 1e-9, b);
                check(r.maxTemperature <= Th + 1e-6 && r.minTemperature >= Tamb - 1e-6,
                      "температура стенки между средой и заданной");
                const double wantRate = qAnalytic * 0.01;
                std::snprintf(b, sizeof(b), "конвекция, разрешение %d: отведено %.6f Вт против q·A = %.4f",
                          res, r.heatRateOut, wantRate);
                check(rel(r.heatRateOut, wantRate) < 1e-6, b);
                check(rel(r.heatRateIn, wantRate) < 1e-6, "подвод через горячую грань равен отводу");
                std::snprintf(b, sizeof(b), "конвекция, разрешение %d: съём конвекцией %.6f Вт", res,
                              r.heatRateConvective);
                check(rel(r.heatRateConvective, wantRate) < 1e-6, b);
                bool convBalance = false;
                for (const std::string& w : r.warnings) if (w.find("баланс") != std::string::npos) convBalance = true;
                check(!convBalance, "баланс с конвекцией сходится — предупреждения нет");

                // повторное решение той же задачи — побитово тот же ответ
                StudyResults r2;
                std::string err2;
                femSolveSteadyThermal(m, pr, r2, err2);
                bool identical = (r2.iterations == r.iterations) && (r2.finalResidual == r.finalResidual) &&
                                 (r2.temperature.size() == r.temperature.size());
                for (size_t i = 0; identical && i < r.temperature.size(); ++i)
                    if (r2.temperature[i] != r.temperature[i] || r2.qx[i] != r.qx[i]) identical = false;
                std::snprintf(b, sizeof(b), "конвекция, разрешение %d: повторный расчёт совпал побитово (%d итераций)",
                              res, r.iterations);
                check(identical, b);
            }

            // жёсткая конвекция (h = 500): стенка почти выходит на температуру среды
            pr.loads.back().value = 500.0;
            FemMesh m2 = buildBar(32);
            StudyResults r3;
            std::string err3;
            check(femSolveSteadyThermal(m2, pr, r3, err3), "жёсткая конвекция (h=500) сошлась");
            const double q500 = (Th - Tamb) / (L / k + 1.0 / 500.0);
            int nS = 0; double qS = 0.0;
            const double xE = m2.ox + m2.ex * m2.hx;
            for (int i = 0; i < m2.nodeCount; ++i)
                if (std::fabs(m2.nodeX(i) - xE) < 1e-9) { qS += r3.qx[(size_t)i]; nS++; }
            if (nS) qS /= nS;
            std::snprintf(b, sizeof(b), "h=500: q %.6f Вт/м2 против аналитических %.6f", qS, q500);
            check(nS > 0 && rel(qS, q500) < 1e-9, b);

            // отчёт обязан содержать строку о съёме тепла конвекцией и НЕ должен
            // жаловаться на баланс (проверка заодно ловит обрезку длинных строк)
            pr.loads.back().value = hConv;
            pr.results = r3;
            const std::string repConv = studyBuildReport(pr, m2);
            check(repConv.find("съём тепла конвекцией") != std::string::npos,
                  "отчёт содержит съём тепла конвекцией");
            check(repConv.find("баланс не сходится") == std::string::npos,
                  "отчёт не жалуется на баланс, когда он сходится");
            check(repConv.find("подведено") != std::string::npos &&
                  repConv.find("отведено") != std::string::npos,
                  "отчёт показывает подвод и отвод тепла в ваттах");
        }
    }

    // =========================================================
    section("8. Сходимость по сетке");
    // =========================================================
    {
        const int ress[] = { 8, 16, 32 };
        double prevT = 0.0;
        bool stable = true;
        for (int res : ress) {
            StudyProject pr;
            setupSlab(pr, 45.0, res);
            FemMesh m = buildBar(res);
            StudyResults r;
            std::string err;
            femSolveSteadyThermal(m, pr, r, err);
            if (prevT != 0.0 && rel(r.maxTemperature, prevT) > 1e-9) stable = false;
            prevT = r.maxTemperature;
        }
        check(stable, "тепловая задача: T макс одинакова на разрешениях 8/16/32");

        const double uExact = 10000.0 * 1.0 / (2.1e11 * 0.01);
        for (int res : ress) {
            StudyProject pr;
            setupBarTension(pr, 2.1e11, 0.0, 10000.0, 0.01, res);
            FemMesh m = buildBar(res);
            StudyResults r;
            std::string err;
            femSolveLinearStatic(m, pr, r, err);
            int cnt = 0;
            const double uAvg = faceAvgUx(m, r, cnt);
            const double dev = rel(uAvg, uExact);
            std::snprintf(b, sizeof(b), "ν=0, разрешение %d: u среднее %.9e (отклонение %.2e)", res, uAvg, dev);
            check(dev < 1e-9, b);
        }

        double prevDev = 1.0;
        bool monotone = true;
        for (int res : ress) {
            StudyProject pr;
            setupBarTension(pr, 2.1e11, 0.3, 10000.0, 0.01, res);
            FemMesh m = buildBar(res);
            StudyResults r;
            std::string err;
            femSolveLinearStatic(m, pr, r, err);
            int cnt = 0;
            const double uAvg = faceAvgUx(m, r, cnt);
            const double dev = std::fabs(uAvg / uExact - 1.0);
            std::snprintf(b, sizeof(b), "ν=0.3, разрешение %d: отклонение u среднего %+.3f%%",
                          res, (uAvg / uExact - 1.0) * 100.0);
            check(dev < 0.02, b);
            if (dev > prevDev + 1e-9) monotone = false;
            prevDev = dev;
        }
        check(monotone, "ν=0.3: отклонение уменьшается с измельчением сетки");

        double prevS = 0.0;
        bool sStable = true;
        for (int res : ress) {
            StudyProject pr;
            setupBarTension(pr, 2.1e11, 0.3, 10000.0, 0.01, res);
            FemMesh m = buildBar(res);
            StudyResults r;
            std::string err;
            femSolveLinearStatic(m, pr, r, err);
            int cnt = 0;
            const double sAvg = midAvgSxx(m, r, cnt);
            if (prevS != 0.0 && rel(sAvg, prevS) > 1e-6) sStable = false;
            prevS = sAvg;
        }
        check(sStable, "σ_xx среднее по сечению одинаково на разрешениях 8/16/32");
    }

    // =========================================================
    section("9. Смена системы единиц и разрешения не меняет физику");
    // =========================================================
    {
        StudyProject si;
        setupBarTension(si, 2.1e11, 0.3, 10000.0, 0.01);
        si.units = StudyUnitSystem::SI;
        StudyProject mm;
        setupBarTension(mm, 2.1e11, 0.3, 10000.0, 0.01);
        mm.units = StudyUnitSystem::mmMPa;

        FemMesh m = buildBar(16);
        StudyResults rsi, rmm;
        std::string e1, e2;
        const bool ok1 = femSolveLinearStatic(m, si, rsi, e1);
        const bool ok2 = femSolveLinearStatic(m, mm, rmm, e2);
        check(ok1 && ok2, "оба расчёта (СИ и мм-МПа) сошлись");
        std::snprintf(b, sizeof(b), "u макс: СИ %.9e м, мм-система %.9e м", rsi.maxDisplacement, rmm.maxDisplacement);
        check(rel(rsi.maxDisplacement, rmm.maxDisplacement) < 1e-15, b);
        std::snprintf(b, sizeof(b), "σ макс: СИ %.6e Па, мм-система %.6e Па", rsi.maxVonMises, rmm.maxVonMises);
        check(rel(rsi.maxVonMises, rmm.maxVonMises) < 1e-15, b);
        check(rsi.iterations == rmm.iterations, "число итераций одинаково (единицы не влияют на ядро)");

        const StudyUnits u = studyUnitsOf(StudyUnitSystem::mmMPa);
        std::snprintf(b, sizeof(b), "отображение: %.6g МПа против %.6g Па",
                      unitToDisplay(rsi.maxVonMises, u.stress), rsi.maxVonMises);
        check(rel(unitToDisplay(rsi.maxVonMises, u.stress), rsi.maxVonMises / 1e6) < 1e-12, b);

        FemMesh m2 = buildBar(24);
        StudyResults r24;
        std::string e3;
        femSolveLinearStatic(m2, mm, r24, e3);
        std::snprintf(b, sizeof(b), "мм-система: разрешение 16 -> %.9e, 24 -> %.9e м",
                      rmm.maxDisplacement, r24.maxDisplacement);
        check(rel(rmm.maxDisplacement, r24.maxDisplacement) < 0.02, b);
    }

    // =========================================================
    section("10. Просмотр результатов: проба, срез, график, шкала, CSV, отчёт");
    // =========================================================
    {
        StudyProject pr;
        setupSlab(pr, 45.0);
        FemMesh m = buildBar(16);
        StudyResults r;
        std::string err;
        femSolveSteadyThermal(m, pr, r, err);
        pr.results = r;

        for (double x : { 0.125, 0.25, 0.5, 0.75 }) {
            double v = 0.0;
            const bool got = femSampleField(m, r.temperature, x, 0.05, 0.05, v);
            std::snprintf(b, sizeof(b), "проба x=%.3f: T=%.6f K против аналитических %.6f K",
                          x, v, 400.0 - 100.0 * x);
            check(got && std::fabs(v - (400.0 - 100.0 * x)) < 1e-9, b);
        }
        {
            double v = 0.0;
            check(!femSampleField(m, r.temperature, 5.0, 5.0, 5.0, v),
                  "проба вне тела возвращает отказ, а не выдуманное число");
            check(!femSampleField(m, r.temperature, NAN, 0.05, 0.05, v),
                  "проба с NaN в координатах возвращает отказ");
            std::vector<double> empty;
            check(!femSampleField(m, empty, 0.5, 0.05, 0.05, v), "проба по пустому полю возвращает отказ");
        }

        ResultSlice sl;
        const bool sliceOk = resultBuildSlice(sl, m, r.temperature, 1, 0.05, 24);
        std::snprintf(b, sizeof(b), "срез Y=0.05: %d x %d, значения %.4f .. %.4f K", sl.nu, sl.nv, sl.vmin, sl.vmax);
        check(sliceOk && sl.ok, b);
        check(std::fabs(sl.vmin - 300.0) < 1e-9 && std::fabs(sl.vmax - 400.0) < 1e-9,
              "диапазон среза совпадает с диапазоном поля");
        int validCount = 0;
        for (char c : sl.valid) if (c) validCount++;
        std::snprintf(b, sizeof(b), "действительных точек среза %d из %d", validCount, sl.nu * sl.nv);
        check(validCount > 0, b);
        ResultSlice slOut;
        resultBuildSlice(slOut, m, r.temperature, 0, 5.0, 16);
        check(!slOut.ok, "срез вне тела не строится");

        ResultLinePlot lp;
        const bool lineOk = resultBuildLine(lp, m, r.temperature, 0.0, 0.05, 0.05, 1.0, 0.05, 0.05, 11);
        std::snprintf(b, sizeof(b), "график вдоль X: %zu точек, s от %.3f до %.3f м",
                      lp.v.size(), lp.s.front(), lp.s.back());
        check(lineOk && lp.ok && lp.v.size() == 11, b);
        check(std::fabs(lp.v.front() - 400.0) < 1e-9 && std::fabs(lp.v.back() - 300.0) < 1e-9,
              "график начинается с 400 K и заканчивается 300 K");
        bool decreasing = true;
        for (size_t i = 1; i < lp.v.size(); ++i) if (lp.v[i] > lp.v[i-1] + 1e-9) decreasing = false;
        check(decreasing, "температура монотонно убывает вдоль линии");

        ResultScale sc;
        sc.field = ResultFieldId::Temperature;
        resultScaleUpdate(sc, pr.results, false);
        std::snprintf(b, sizeof(b), "авто-шкала: %.4f .. %.4f K (данные %.4f .. %.4f)",
                      sc.lo, sc.hi, sc.dataMin, sc.dataMax);
        check(std::fabs(sc.lo - 300.0) < 1e-9 && std::fabs(sc.hi - 400.0) < 1e-9, b);
        check(std::fabs(resultScaleT(sc, 350.0) - 0.5) < 1e-12, "середина шкалы = 0.5");
        check(resultScaleT(sc, 100.0) == 0.0 && resultScaleT(sc, 900.0) == 1.0,
              "значения вне шкалы прижимаются к границам");
        sc.autoRange = false; sc.manualLo = 320.0; sc.manualHi = 380.0;
        resultScaleUpdate(sc, pr.results, false);
        check(std::fabs(sc.lo - 320.0) < 1e-12 && std::fabs(sc.hi - 380.0) < 1e-12, "ручной диапазон применяется");
        sc.manualLo = 400.0; sc.manualHi = 300.0;
        resultScaleUpdate(sc, pr.results, false);
        check(sc.lo <= sc.hi && sc.hi - sc.lo > 0.0, "перепутанные границы шкалы исправляются");

        StudyProject ps;
        setupBarTension(ps, 2.1e11, 0.3, 10000.0, 0.01);
        FemMesh ms = buildBar(16);
        StudyResults rs;
        std::string es;
        femSolveLinearStatic(ms, ps, rs, es);
        ps.results = rs;
        ResultScale ss;
        ss.field = ResultFieldId::Uy;
        resultScaleUpdate(ss, ps.results, true);
        std::snprintf(b, sizeof(b), "симметричная шкала u_y: %.3e .. %.3e", ss.lo, ss.hi);
        check(std::fabs(ss.lo + ss.hi) < 1e-18, b);

        std::vector<ResultKeyValue> kv = resultKeyValues(pr, m);
        std::snprintf(b, sizeof(b), "таблица величин: %zu строк", kv.size());
        check(kv.size() >= 10, b);
        bool hasStatus = false, hasIter = false, hasRes = false;
        for (const ResultKeyValue& e : kv) {
            if (e.name.find("Статус") != std::string::npos) hasStatus = true;
            if (e.name.find("Итераций") != std::string::npos) hasIter = true;
            if (e.name.find("невязка") != std::string::npos) hasRes = true;
        }
        check(hasStatus && hasIter && hasRes, "в таблице есть статус, число итераций и невязка");

        check(resultFieldAvailable(pr.results, ResultFieldId::Temperature) &&
              resultFieldAvailable(pr.results, ResultFieldId::HeatFluxMag) &&
              !resultFieldAvailable(pr.results, ResultFieldId::VonMises),
              "в тепловом результате доступны температура и поток, но не напряжения");
        check(resultFieldAvailable(ps.results, ResultFieldId::VonMises) &&
              resultFieldAvailable(ps.results, ResultFieldId::DisplacementMag) &&
              !resultFieldAvailable(ps.results, ResultFieldId::Temperature),
              "в прочностном результате доступны фон Мизес и перемещения, но не температура");
        {
            double vm = 0.0;
            for (int i = 0; i < ms.nodeCount; ++i)
                vm = std::max(vm, resultFieldAt(ps.results, ResultFieldId::DisplacementMag, (size_t)i));
            std::snprintf(b, sizeof(b), "|u| макс через resultFieldAt: %.6e м (в результатах %.6e)",
                          vm, ps.results.maxDisplacement);
            check(rel(vm, ps.results.maxDisplacement) < 1e-12, b);
        }

        const std::string csv = "/tmp/aeros-test-results.csv";
        std::vector<ResultFieldId> flds = { ResultFieldId::Temperature, ResultFieldId::HeatFluxX };
        std::string cerr;
        check(resultExportCSV(m, pr, flds, csv, cerr), "CSV с результатами записан");
        {
            std::ifstream f(csv);
            std::string line;
            int dataLines = 0;
            bool hasUnits = false, hasHeader = false, hasMaterial = false, hasStatus = false;
            while (std::getline(f, line)) {
                if (line.compare(0, 1, "#") == 0) {
                    if (line.find("units=SI") != std::string::npos) hasUnits = true;
                    if (line.find("material=") != std::string::npos) hasMaterial = true;
                    if (line.find("status=") != std::string::npos) hasStatus = true;
                    continue;
                }
                if (!hasHeader) { hasHeader = (line.find("node,x_m,y_m,z_m,boundary") == 0); continue; }
                dataLines++;
            }
            std::snprintf(b, sizeof(b), "CSV: строк данных %d при %d узлах", dataLines, m.nodeCount);
            check(dataLines == m.nodeCount, b);
            check(hasHeader, "CSV: заголовок начинается с node,x_m,y_m,z_m,boundary");
            check(hasUnits, "CSV: в заголовке указана система единиц (СИ)");
            check(hasMaterial, "CSV: в заголовке записаны свойства материала");
            check(hasStatus, "CSV: в заголовке записан статус расчёта");
        }
        std::remove(csv.c_str());
        std::string badErr;
        check(!resultExportCSV(m, pr, flds, "/нет/такого/каталога/x.csv", badErr) && !badErr.empty(),
              "запись CSV в недоступный каталог даёт отказ с сообщением");

        const std::string rep = studyBuildReport(pr, m);
        std::snprintf(b, sizeof(b), "отчёт: %zu байт", rep.size());
        check(rep.size() > 500, b);
        check(rep.find("Стационарная теплопередача") != std::string::npos, "отчёт содержит тип анализа");
        check(rep.find("невязка") != std::string::npos, "отчёт содержит невязку");
        check(rep.find("Ограничения методики") != std::string::npos, "отчёт содержит раздел об ограничениях");
        check(rep.find("ступеньк") != std::string::npos, "отчёт честно пишет о ступенчатой геометрии");
        check(rep.find("400") != std::string::npos && rep.find("300") != std::string::npos,
              "отчёт содержит фактические значения температур");
        check(rep.find("грань X+") != std::string::npos, "отчёт показывает выбор грани у условий");

        StudyProject pq = ps;
        pq.materials[0].yieldStrength = 1.0e3;
        const std::string repQ = studyBuildReport(pq, ms);
        check(repQ.find("ВНИМАНИЕ") != std::string::npos,
              "отчёт предупреждает, когда фон Мизес превышает предел текучести");
    }

    // =========================================================
    section("11. Честность отказов и статусов");
    // =========================================================
    {
        {   StudyProject pr;
            setupBarTension(pr, 2.1e11, 0.3, 10000.0, 0.01);
            FemMesh empty;
            StudyResults r;
            std::string err;
            const bool ok = femSolveLinearStatic(empty, pr, r, err);
            std::snprintf(b, sizeof(b), "без сетки: отказ=%d, статус=%s, сообщение «%s»",
                          (int)!ok, studyStatusName(r.status), err.c_str());
            check(!ok && r.status == StudySolveStatus::Error && !err.empty(), b);
            check(!r.valid, "результат без расчёта не помечен действительным");
        }
        {   StudyProject pr;
            setupBarTension(pr, 2.1e11, 0.3, 10000.0, 0.01);
            pr.loads.erase(pr.loads.begin());
            FemMesh m = buildBar(12);
            StudyResults r;
            std::string err;
            const bool ok = femSolveLinearStatic(m, pr, r, err);
            std::snprintf(b, sizeof(b), "без закреплений: отказ=%d, статус=%s, «%s»",
                          (int)!ok, studyStatusName(r.status), err.c_str());
            check(!ok && r.status == StudySolveStatus::Error, b);
            check(!r.valid, "сингулярная задача не выдаёт «действительный» результат");
            bool finite = true;
            for (double v : r.ux) if (!std::isfinite(v)) finite = false;
            check(finite, "в отказавшем расчёте нет NaN/Inf, выданных за ответ");
        }
        {   StudyProject pr;
            setupBarTension(pr, 2.1e11, 0.3, 10000.0, 0.01);
            pr.loads.pop_back();
            FemMesh m = buildBar(12);
            StudyResults r;
            std::string err;
            femSolveLinearStatic(m, pr, r, err);
            std::snprintf(b, sizeof(b), "без нагрузки: u макс %.3e м", r.maxDisplacement);
            check(r.maxDisplacement < 1e-18, b);
        }
        {   StudyProject pr;
            setupBarTension(pr, 2.1e11, 0.3, 10000.0, 0.01);
            pr.solver.maxIterations = 1;
            FemMesh m = buildBar(24);
            StudyResults r;
            std::string err;
            const bool ok = femSolveLinearStatic(m, pr, r, err);
            std::snprintf(b, sizeof(b), "предел 1 итерация: статус %s, невязка %.2e", studyStatusName(r.status),
                          r.finalResidual);
            check(!ok && r.status == StudySolveStatus::NotConverged, b);
            check(!r.valid, "несошедшийся расчёт не помечен действительным");
            check(r.finalResidual > pr.solver.relTolerance, "невязка больше допуска — статус честный");
            check(!r.errorMessage.empty(), "сообщение о несходимости заполнено");
        }
        {   StudyProject pr;
            setupSlab(pr, 45.0);
            pr.solver.maxIterations = 1;
            FemMesh m = buildBar(24);
            StudyResults r;
            std::string err;
            const bool ok = femSolveSteadyThermal(m, pr, r, err);
            std::snprintf(b, sizeof(b), "тепловой расчёт, 1 итерация: статус %s", studyStatusName(r.status));
            check(!ok && r.status == StudySolveStatus::NotConverged, b);
        }
        {   StudyProject pr;
            setupSlab(pr, 45.0);
            pr.loads.clear();
            StudyLoad flux;
            flux.name = "поток"; flux.kind = StudyBCKind::HeatFlux; flux.value = 1000.0;
            flux.region = studyFaceRegion(pr, 0, true, "грань X+");
            pr.loads.push_back(flux);
            FemMesh m = buildBar(12);
            StudyResults r;
            std::string err;
            const bool ok = femSolveSteadyThermal(m, pr, r, err);
            std::snprintf(b, sizeof(b), "только поток, без температуры: отказ=%d, статус=%s, «%s»",
                          (int)!ok, studyStatusName(r.status), err.c_str());
            check(!ok, b);
        }
        {   StudyProject pr;
            setupSlab(pr, 0.0);
            FemMesh m = buildBar(12);
            StudyResults r;
            std::string err;
            const bool ok = femSolveSteadyThermal(m, pr, r, err);
            std::snprintf(b, sizeof(b), "k = 0: отказ=%d, сообщение «%s»", (int)!ok, err.c_str());
            check(!ok, b);
        }
        {   StudyProject pr;
            setupBarTension(pr, -1.0, 0.3, 10000.0, 0.01);
            FemMesh m = buildBar(12);
            StudyResults r;
            std::string err;
            const bool ok = femSolveLinearStatic(m, pr, r, err);
            std::snprintf(b, sizeof(b), "E < 0: отказ=%d, сообщение «%s»", (int)!ok, err.c_str());
            check(!ok, b);
        }
        {   // условие, под которое не попал ни один узел/грань — явное предупреждение
            StudyProject pr;
            setupBarTension(pr, 2.1e11, 0.3, 10000.0, 0.01);
            pr.loads[1].region.minX = 50.0; pr.loads[1].region.maxX = 60.0;   // далеко от тела
            FemMesh m = buildBar(12);
            StudyResults r;
            std::string err;
            femSolveLinearStatic(m, pr, r, err);
            bool hitWarn = false;
            for (const std::string& w : r.warnings)
                if (w.find("не попал") != std::string::npos) hitWarn = true;
            check(hitWarn, "условие вне тела даёт предупреждение «не попала ни одна грань»");
        }
        {   // две несвязные части
            const int R = 16;
            std::vector<int> occ((size_t)R * R * R, 0);
            for (int k = 0; k < R; ++k)
            for (int j = 0; j < R; ++j)
            for (int i = 0; i < R; ++i)
                if (i < R / 3 || i >= 2 * R / 3) occ[(size_t)(k * R + j) * R + i] = 1;
            FemMesh m;
            femMeshBuildFromVoxels(m, occ, R, R, R, 0, 0, 0, 1.0, 0.1, 0.1, 16, 0, 7850.0);
            std::snprintf(b, sizeof(b), "две несвязные части: компонентов %d", m.components);
            check(m.components >= 2, b);
            bool compWarn = false;
            for (const std::string& w : m.warnings)
                if (w.find("несвяз") != std::string::npos) compWarn = true;
            check(compWarn, "сетка предупреждает о несвязных частях");
            StudyProject pr;
            setupBarTension(pr, 2.1e11, 0.3, 10000.0, 0.01);
            StudyResults r;
            std::string err;
            const bool ok = femSolveLinearStatic(m, pr, r, err);
            std::snprintf(b, sizeof(b), "две части с одним защемлением: отказ=%d, статус=%s, «%s»",
                          (int)!ok, studyStatusName(r.status), err.c_str());
            check(!ok, b);
        }
        {   StudyProject pr;
            setupBarTension(pr, 2.1e11, 0.3, 10000.0, 0.01);
            pr.analysis = StudyAnalysis::AeroCFD;
            check(std::strcmp(studyAnalysisName(pr.analysis), "Аэродинамика (LBM)") == 0,
                  "аэродинамика — отдельный режим (LBM), FEM для неё не применяется");
            std::vector<StudyIssue> iss = studyValidate(pr);
            bool noFemChecks = true;
            for (const StudyIssue& i : iss) if (i.node == "Сетка") noFemChecks = false;
            check(noFemChecks, "для режима аэродинамики валидация не требует сетку МКЭ");
        }
        {   // Допуск сходимости: бессмысленное значение не должно давать фиктивную
            // «сходимость». Раньше tol >= 1 (или NaN/Inf) делал условие сходимости
            // истинным на первой итерации: решатель рапортовал «сошёлся», не решив
            // систему, и такой ответ принимали за результат.
            StudyProject pr;
            setupBarTension(pr, 2.1e11, 0.0, 10000.0, 0.01);
            pr.mesh.resolution = 12;
            const int R = 12;
            std::vector<int> occ((size_t)R * R * R, 1);
            FemMesh m;
            femMeshBuildFromVoxels(m, occ, R, R, R, 0.0f, 0.0f, 0.0f, 1.0f, 0.1f, 0.1f,
                                   pr.mesh.resolution, 0, pr.material().density);
            check(m.built && m.elemCount > 0, "сетка для проверки допуска построена");

            pr.solver.relTolerance = 1e-10;
            StudyResults ref; std::string errRef;
            femSolveLinearStatic(m, pr, ref, errRef);
            check(ref.status == StudySolveStatus::Converged, "эталонный расчёт (допуск 1e-10) сошёлся");

            const double badTol[] = { 1e9, -1.0, 0.0,
                                      std::numeric_limits<double>::quiet_NaN(),
                                      std::numeric_limits<double>::infinity() };
            const char* badName[] = { "1e9", "-1", "0", "NaN", "Inf" };
            for (int i = 0; i < 5; ++i) {
                pr.solver.relTolerance = badTol[i];
                StudyResults r; std::string err;
                femSolveLinearStatic(m, pr, r, err);
                std::snprintf(b, sizeof(b),
                              "допуск %s: статус %s, итераций %d, невязка %.3e, u_max %.6e (эталон %.6e)",
                              badName[i], studyStatusName(r.status), r.iterations, r.finalResidual,
                              r.maxDisplacement, ref.maxDisplacement);
                check(r.status == StudySolveStatus::Converged, b);
                check(r.iterations > 1, "бессмысленный допуск не дал «сходимость» за одну итерацию");
                check(r.finalResidual < 1e-6, "невязка действительно мала, а не принята на веру");
                check(std::fabs(r.maxDisplacement - ref.maxDisplacement) <=
                          1e-6 * (std::fabs(ref.maxDisplacement) + 1e-30),
                      "результат совпал с эталонным: допуск подменён безопасным, а не проглочен");
                bool tolWarn = false;
                for (const std::string& w : r.warnings)
                    if (w.find("допуск сходимости") != std::string::npos) tolWarn = true;
                check(tolWarn, "в результатах записано предупреждение о замене допуска");
            }

            pr.solver.relTolerance = 1e-10;
            StudyResults r2; std::string e2;
            femSolveLinearStatic(m, pr, r2, e2);
            bool noTolWarn = true;
            for (const std::string& w : r2.warnings)
                if (w.find("допуск сходимости") != std::string::npos) noTolWarn = false;
            check(noTolWarn, "при корректном допуске предупреждение о замене не появляется");

            // История невязки хранится в double. Раньше это был float: невязка
            // глубоко сошедшейся системы (1e-45…1e-160) обнулялась, и интерфейс
            // показывал «начальная невязка 0» и плоский график сходимости.
            std::snprintf(b, sizeof(b), "история невязки: %zu точек, первая %.3e, последняя %.3e",
                          r2.residualHistory.size(),
                          r2.residualHistory.empty() ? 0.0 : r2.residualHistory.front(),
                          r2.residualHistory.empty() ? 0.0 : r2.residualHistory.back());
            check(!r2.residualHistory.empty(), b);
            // история пишется не каждую итерацию, поэтому последняя записанная
            // невязка обязана быть не меньше конечной — и не обнулённой
            check(!r2.residualHistory.empty() && r2.residualHistory.back() >= r2.finalResidual &&
                  r2.residualHistory.back() > 0.0,
                  "последняя записанная невязка истории не меньше конечной и не обнулена");
            check(!r2.residualHistory.empty() && r2.initialResidual == r2.residualHistory.front(),
                  "начальная невязка взята из истории без потери точности");
            check(r2.initialResidual > r2.finalResidual && r2.finalResidual > 0.0,
                  "невязка убывает и не обнуляется");

            // недостижимый допуск: честно «не сошёлся», но невязка обязана остаться числом
            pr.solver.relTolerance = 1e-300;
            pr.solver.maxIterations = 5000;
            StudyResults r3; std::string e3;
            femSolveLinearStatic(m, pr, r3, e3);
            std::snprintf(b, sizeof(b), "глубокая невязка: статус %s, итераций %d, невязка %.3e",
                          studyStatusName(r3.status), r3.iterations, r3.finalResidual);
            check(r3.status == StudySolveStatus::NotConverged, b);
            check(r3.finalResidual > 0.0, "невязка не обнулилась при глубокой сходимости");
            check(!r3.residualHistory.empty() && r3.residualHistory.back() > 0.0,
                  "в истории невязки нет обнуления там, где float дал бы 0");
            // прямое доказательство: это значение float представить не может
            check(!r3.residualHistory.empty() && (float)r3.residualHistory.back() == 0.0f,
                  "значение невязки ниже порога float: double его сохранил, float обнулил бы");
            check(r3.finalResidual < 1e-30,
                  "невязка действительно ниже порога представимости float (1.2e-38)");
            pr.solver.relTolerance = 1e-10;
            pr.solver.maxIterations = 2000;

            // валидация постановки: бессмысленный допуск — ошибка, свободный — предупреждение
            pr.solver.relTolerance = 5.0;
            bool isError = false;
            for (const StudyIssue& is : studyValidate(pr))
                if (is.node == "Решатель" && is.level == StudyNodeState::Error) isError = true;
            check(isError, "валидация считает бессмысленный допуск ошибкой, а не предупреждением");
            pr.solver.relTolerance = 2e-2;
            bool isWarn = false; isError = false;
            for (const StudyIssue& is : studyValidate(pr))
                if (is.node == "Решатель") {
                    if (is.level == StudyNodeState::Warning) isWarn = true;
                    if (is.level == StudyNodeState::Error) isError = true;
                }
            check(isWarn && !isError, "свободный, но осмысленный допуск — предупреждение, не ошибка");

            // загрузка из файла: мусорный допуск заменяется безопасным
            const char* tmp = "/tmp/aeros-fem-tolerance.aerosproj";
            {   std::ofstream f(tmp);
                f << "# проверка загрузки допуска\nsolverTolerance=1e9\nmeshResolution=12\n"; }
            StudyProject loaded;
            check(studyLoadProject(loaded, tmp), "проект с мусорным допуском загружен");
            std::snprintf(b, sizeof(b), "загруженный допуск %.3g (ожидался безопасный 1e-10)",
                          loaded.solver.relTolerance);
            check(std::fabs(loaded.solver.relTolerance - 1e-10) <= 1e-13, b);
            {   std::ofstream f(tmp);
                f << "solverTolerance=nan\n"; }
            check(studyLoadProject(loaded, tmp), "проект с «nan» в допуске загружен");
            check(std::isfinite(loaded.solver.relTolerance) && loaded.solver.relTolerance > 0.0,
                  "«nan» из файла не попал в настройки решателя");
            std::remove(tmp);
        }
        {   // Предел размера сетки: честный отказ ДО выделения гигабайтов.
            // Расчёт держит примерно 250 байт на элемент, поэтому сетка больше
            // 12 млн элементов на обычном ПК не решается — её надо отклонить сразу.
            const int R = 8;
            std::vector<int> occ((size_t)R * R * R, 1);
            FemMesh big;
            const bool okBig = femMeshBuildFromVoxels(big, occ, R, R, R,
                                                      0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                                                      300, 0, 7850.0);
            const long long elems = (long long)300 * 300 * 300;
            std::snprintf(b, sizeof(b), "сетка на %lld элементов отклонена (построение вернуло %d)",
                          elems, (int)okBig);
            check(!okBig && !big.built && big.elemCount == 0, b);
            bool saidTooBig = false;
            for (const std::string& w : big.warnings)
                if (w.find("слишком велика") != std::string::npos) saidTooBig = true;
            check(saidTooBig, "в предупреждениях названа причина: сетка слишком велика");
            bool mentionsMemory = false;
            for (const std::string& w : big.warnings)
                if (w.find("байт") != std::string::npos || w.find("памят") != std::string::npos)
                    mentionsMemory = true;
            check(mentionsMemory, "предупреждение объясняет ограничение памятью, а не просто «нельзя»");

            // сразу под пределом — сетка строится (проверка, что предел не занижен зря)
            FemMesh ok12;
            const bool okMid = femMeshBuildFromVoxels(ok12, occ, R, R, R,
                                                      0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                                                      96, 0, 7850.0);
            std::snprintf(b, sizeof(b), "сетка 96x96x96 = %d элементов построена", ok12.elemCount);
            check(okMid && ok12.built && ok12.elemCount == 96 * 96 * 96, b);
        }
    }

    // =========================================================
    section("12. Компоновка оболочки (без ImGui — чистая геометрия)");
    // =========================================================
    {
        const float W = 1920.0f, H = 1080.0f;
        UiLayoutState st;
        UiLayoutRects L = uiLayoutCompute(st, W, H);
        check(L.menu.valid() && L.menu.w == W && L.menu.y == 0.0f, "главное меню — полоса во всю ширину сверху");
        std::snprintf(b, sizeof(b), "строка состояния: y=%.0f h=%.0f (высота окна %.0f)", L.status.y, L.status.h, H);
        check(std::fabs(L.status.y + L.status.h - H) < 0.01f && L.status.w == W, b);
        check(L.tree.x == 0.0f && L.tree.y == st.menuH, "дерево проекта закреплено у левого края под меню");
        check(std::fabs(L.props.right() - W) < 0.01f, "панель свойств закреплена у правого края");
        check(std::fabs(L.bottom.y + L.bottom.h - L.status.y) < 0.01f && L.bottom.w == W,
              "нижние вкладки лежат прямо над строкой состояния");
        check(std::fabs(L.viewport.x - L.tree.right()) < 0.01f &&
              std::fabs(L.viewport.right() - L.props.x) < 0.01f,
              "3D-вид занимает центр между деревом и свойствами");
        check(std::fabs(L.viewport.y + L.viewport.h - L.bottom.y) < 0.01f, "3D-вид не заходит под нижние вкладки");
        check(L.spTree.valid() && L.spProps.valid() && L.spBottom.valid(),
              "разделители панелей построены (их можно тянуть)");

        st.maximized = true;
        L = uiLayoutCompute(st, W, H);
        check(!L.treeVisible && !L.propsVisible && !L.bottomVisible, "при максимизации доки скрыты");
        check(L.viewport.x == 0.0f && L.viewport.w == W, "вид занимает всю ширину окна");
        check(L.menu.h > 0 && L.status.h > 0, "меню и строка состояния остаются видны при максимизации");
        check(!L.spTree.valid() && !L.spProps.valid() && !L.spBottom.valid(),
              "ручки разделителей скрыты вместе с панелями");
        st.maximized = false;

        st.panelsVisible = false;
        L = uiLayoutCompute(st, W, H);
        check(L.viewport.w == W && L.viewport.x == 0.0f, "F12: вид на всю ширину");
        check(L.menu.h > 0 && L.status.h > 0, "F12 не убирает меню и строку состояния");
        st.panelsVisible = true;

        st.treePanel = false; st.propsPanel = false;
        L = uiLayoutCompute(st, W, H);
        check(L.viewport.w == W && L.bottom.w == W, "вид и вкладки растянулись на место скрытых панелей");
        st.treePanel = true; st.propsPanel = true;

        st.menuBar = false;
        L = uiLayoutCompute(st, W, H);
        check(L.tree.y == 0.0f && L.viewport.y == 0.0f, "без меню панели поднимаются к верху окна");
        st.menuBar = true;

        for (float w : { 320.0f, 640.0f, 800.0f, 1280.0f, 3840.0f }) {
            UiLayoutState s2;
            UiLayoutRects L2 = uiLayoutCompute(s2, w, 720.0f);
            std::snprintf(b, sizeof(b), "окно %.0fx720: ширина вида %.0f px", w, L2.viewport.w);
            check(L2.viewport.w > 1.0f && L2.viewport.h > 1.0f, b);
            check(L2.status.h > 0.0f, "строка состояния есть при любой ширине окна");
            check(L2.tree.w + L2.props.w <= w, "панели не шире окна");
        }

        check(uiLayoutDragSize(264.0f, 20.0f, false, 150.0f, 600.0f) == 284.0f, "разделитель дерева тянется вправо");
        check(uiLayoutDragSize(264.0f, 20.0f, true, 240.0f, 1200.0f) == 244.0f,
              "разделитель свойств: движение вправо уменьшает ширину");
        check(uiLayoutDragSize(160.0f, -50.0f, false, 150.0f, 600.0f) == 150.0f, "минимум ширины выдержан");
        check(uiLayoutDragSize(590.0f, 50.0f, false, 150.0f, 600.0f) == 600.0f, "максимум ширины выдержан");
        check(uiLayoutDragSize(264.0f, (float)NAN, false, 150.0f, 600.0f) == 264.0f,
              "NaN от мыши не портит размер панели");

        {   UiLayoutState a;
            a.workspace = 2; a.bottomTab = 3; a.treePanel = false;
            a.treeW = 333.0f; a.propsW = 511.0f; a.bottomH = 277.0f; a.fontScale = 1.25f;
            a.maximized = true;
            std::ostringstream os;
            uiLayoutWrite(os, a);
            const std::string txt = os.str();
            UiLayoutState loaded;
            std::istringstream is(txt);
            std::string line;
            int applied = 0;
            while (std::getline(is, line)) {
                const size_t p = line.find('=');
                if (p == std::string::npos) continue;
                if (uiLayoutApplyValue(loaded, line.substr(0, p), line.substr(p + 1))) applied++;
            }
            std::snprintf(b, sizeof(b), "компоновка: сохранено и применено %d ключей", applied);
            check(applied >= 10, b);
            check(loaded.workspace == 2 && loaded.bottomTab == 3 && !loaded.treePanel,
                  "рабочее пространство, вкладка и видимость панелей восстановлены");
            check(std::fabs(loaded.treeW - 333.0f) < 0.5f && std::fabs(loaded.propsW - 511.0f) < 0.5f &&
                  std::fabs(loaded.bottomH - 277.0f) < 0.5f, "размеры панелей восстановлены");
            check(std::fabs(loaded.fontScale - 1.25f) < 0.01f, "масштаб шрифта восстановлен");
            check(!loaded.maximized, "временная максимизация вида не сохраняется между запусками");
            check(txt.find("maximized") == std::string::npos, "в файле настроек нет ключа максимизации");
        }
        {   UiLayoutState s3;
            check(uiLayoutApplyValue(s3, "uiShowTree", "0") && !s3.treePanel, "старый ключ uiShowTree принят");
            check(uiLayoutApplyValue(s3, "uiTreeW", "300") && std::fabs(s3.treeW - 300.0f) < 0.5f,
                  "старый ключ uiTreeW принят");
            check(uiLayoutApplyValue(s3, "uiScale", "1.3") && std::fabs(s3.fontScale - 1.3f) < 1e-4f,
                  "старый ключ uiScale принят");
            check(uiLayoutApplyValue(s3, "uiDocked", "0") && !s3.panelsVisible, "старый ключ uiDocked принят");
            check(uiLayoutApplyValue(s3, "uiBottomH", "250") && std::fabs(s3.bottomH - 250.0f) < 0.5f,
                  "старый ключ uiBottomH принят");
            check(!uiLayoutApplyValue(s3, "flowSpeed", "20"), "чужие ключи модуль компоновки не перехватывает");
            s3.treeW = -1e9f; s3.propsW = 1e9f; s3.bottomH = -5.0f; s3.workspace = 99; s3.bottomTab = -2;
            s3.clampToScreen(1920.0f, 1080.0f);
            check(s3.treeW >= UiLayoutState::minTreeW() && s3.treeW <= UiLayoutState::maxTreeW(),
                  "мусор в размерах дерева приведён к диапазону");
            check(s3.propsW >= UiLayoutState::minPropsW() && s3.propsW <= UiLayoutState::maxPropsW(),
                  "мусор в размерах свойств приведён к диапазону");
            check(s3.bottomH >= UiLayoutState::minBottomH(), "мусор в высоте вкладок приведён к минимуму");
            check(s3.workspace == 0 && s3.bottomTab == 0, "недопустимые индексы сброшены");
        }
        {   bool names = true;
            for (int i = 0; i < UI_WORKSPACE_COUNT; ++i) {
                if (!uiWorkspaceName(i) || !uiWorkspaceName(i)[0]) names = false;
                if (!uiWorkspaceHint(i) || !uiWorkspaceHint(i)[0]) names = false;
            }
            for (int t = 0; t < UI_BOTTOM_TAB_COUNT; ++t)
                if (!uiBottomTabName(t) || !uiBottomTabName(t)[0]) names = false;
            check(names, "у всех рабочих пространств и вкладок есть непустые имена");
            check(UI_WORKSPACE_COUNT == 3 && UI_BOTTOM_TAB_COUNT == 5,
                  "три рабочих пространства и пять нижних вкладок");
            check(std::strcmp(uiWorkspaceName(0), "Модель") == 0 &&
                  std::strcmp(uiWorkspaceName(1), "Исследование") == 0 &&
                  std::strcmp(uiWorkspaceName(2), "Результаты") == 0,
                  "пространства названы: Модель / Исследование / Результаты");
            check(std::strcmp(uiBottomTabName(0), "Сообщения") == 0 &&
                  std::strcmp(uiBottomTabName(1), "Сходимость") == 0 &&
                  std::strcmp(uiBottomTabName(4), "Журнал") == 0,
                  "вкладки названы: сообщения / сходимость / графики / результаты / журнал");
        }
    }

    std::printf("\n=== CAE: проверок %d, провалов %d — %s ===\n",
                g_checks, g_failed, g_failed == 0 ? "ВСЁ В ПОРЯДКЕ" : "ЕСТЬ ПРОВАЛЫ");
    return g_failed == 0 ? 0 : 1;
}
