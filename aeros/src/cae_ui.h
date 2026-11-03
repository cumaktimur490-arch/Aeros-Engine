#ifndef AEROS_CAE_UI_H
#define AEROS_CAE_UI_H
// =====================================================
// Aeros Engine — редактор инженерных исследований (CAE-часть интерфейса)
//
// Модуль отделён от ui.cpp намеренно: ui.cpp отвечает за аэро/CFD-панели и
// оболочку окна, а здесь — только работа с исследованием (study.h), сеткой
// (fem_mesh.h), решателями (fem_solver.h) и просмотром результатов
// (results_view.h). Оболочка (меню, доки, сплиттеры) — ui_layout.h / ui_dock.h.
//
// Что здесь реально реализовано (без заглушек):
//   * перенос геометрии модели в исследование (по воксельному представлению,
//     которое движок уже строит для LBM);
//   * построение объёмной сетки гексаэдров Q1 по вокселям;
//   * линейный статический прочностной расчёт и стационарная теплопередача;
//   * отображение поля на геометрии (цвета вершин + тот же рамп, что у карты Cp);
//   * легенда с единицами, авто/ручной диапазон, проба в точке, срезы,
//     график вдоль линии, таблица ключевых величин;
//   * экспорт результатов в CSV и отчёта в текстовый файл;
//   * исследование сходимости по трём разрешениям сетки.
//
// Честность интерфейса: пункт меню или кнопка, которые ничего не считают,
// здесь не появляются. Если расчёт невозможен — сообщается ПРИЧИНА и узел
// дерева, к которому она относится (см. studyValidate).
// =====================================================

#include <imgui.h>
#include <glm/glm.hpp>

#include <cstdio>
#include <cstdarg>
#include <cfloat>
#include <new>        // std::bad_alloc
#include <stdexcept>  // std::exception
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

#include "globals.h"
#include "version.h"
#include "lang.h"
#include "debug_log.h"
#include "timelapse.h"      // самописец (пункты меню «Отладка»)
#include "model.h"          // loadModel
#include "stl_loader.h"     // openFileDialog
#include "vulkan_renderer.h"// saveFileDialogNative
#include "forces.h"         // updateVertexColors / uploadVertexColorsToGPU
#include "lbm.h"            // pressureColorInRange — единый цветовой рамп
#include "study.h"
#include "fem_mesh.h"
#include "fem_solver.h"
#include "results_view.h"
#include "ui_layout.h"
#include "ui_dock.h"

// -----------------------------------------------------
// Состояние CAE-интерфейса: сетка, выбранное поле, шкала, проба, срез, график.
// Живёт в локальном static (header-only модуль, как studyProject()).
// -----------------------------------------------------
inline FemMesh& caeMesh() { static FemMesh m; return m; }

// определёны ниже по файлу
inline bool caeApplyFieldToGeometry();
inline void caeDrawResultsSummary(const StudyProject& pr, const StudyUnits& u);
inline void caeRunMeshConvergence();
inline bool caeBuildMesh();
inline bool caeSyncGeometry();

// Идентификаторы узлов дерева исследования.
enum CaeStudySel {
    CAE_STUDY_ANALYSIS  = 1,
    CAE_STUDY_GEOMETRY  = 2,
    CAE_STUDY_MATERIALS = 3,
    CAE_STUDY_LOADS     = 4,
    CAE_STUDY_MESH      = 5,
    CAE_STUDY_SOLVER    = 6,
    CAE_STUDY_UNITS     = 7,
    CAE_STUDY_RESULTS   = 8,
    CAE_STUDY_MAT_BASE  = 100,   // + индекс материала
    CAE_STUDY_LOAD_BASE = 1000   // + индекс условия
};

// Идентификаторы узлов дерева результатов.
enum CaeResultSel {
    CAE_RES_LEGEND = 1,
    CAE_RES_PROBE  = 2,
    CAE_RES_SLICE  = 3,
    CAE_RES_PLOT   = 4,
    CAE_RES_TABLE  = 5,
    CAE_RES_CONV   = 6,
    CAE_RES_EXPORT = 7,
    CAE_RES_FIELD_BASE = 100   // + индекс поля из resultFieldTable
};

// -----------------------------------------------------
// Сообщения вкладки «Сообщения»: разделены по уровням, потому что требования к
// оболочке — отдельные предупреждения для сетки, граничных условий и сходимости.
// -----------------------------------------------------
struct CaeMessage {
    int level = 0;          // 0 = инфо, 1 = предупреждение, 2 = ошибка
    std::string category;   // «сетка», «граничные условия», «сходимость», ...
    std::string text;
};

inline std::vector<CaeMessage>& caeMessages() { static std::vector<CaeMessage> v; return v; }

inline void caeLog(int level, const char* category, const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    CaeMessage m;
    m.level = level;
    m.category = category ? category : "";
    m.text = buf;
    std::vector<CaeMessage>& v = caeMessages();
    v.push_back(m);
    if (v.size() > 800) v.erase(v.begin(), v.begin() + (long)(v.size() - 800));
    // дублируем в общий журнал: отчёт F10 должен содержать и историю расчёта
    if (level >= 2)      debugLogWrite(2, "[CAE:%s] %s", m.category.c_str(), buf);
    else if (level == 1) debugLogWrite(1, "[CAE:%s] %s", m.category.c_str(), buf);
    else                 debugLogAction("cae", "%s", buf);
}

inline void caeClearMessages() { caeMessages().clear(); }

// -----------------------------------------------------
// Геометрия модели -> исследование. Используется ТО ЖЕ воксельное представление,
// которое движок строит для LBM: отдельного SDF-мешера не выдумываем.
// -----------------------------------------------------
inline bool caeSyncGeometry() {
    StudyProject& pr = studyProject();
    if (g_voxelData.empty() || g_voxNx < 2 || g_voxNy < 2 || g_voxNz < 2) {
        pr.geometryLoaded = false;
        caeLog(2, "геометрия", "воксельное представление модели не построено — загрузите STL и дождитесь построения сетки модели");
        return false;
    }
    long long solid = 0;
    for (size_t i = 0; i < g_voxelData.size(); ++i) if (g_voxelData[i]) solid++;
    if (solid <= 0) {
        pr.geometryLoaded = false;
        caeLog(2, "геометрия", "в воксельной сетке нет ни одной заполненной ячейки");
        return false;
    }
    const double dx = (double)(g_voxMaxX - g_voxMinX) / (double)g_voxNx;
    const double dy = (double)(g_voxMaxY - g_voxMinY) / (double)g_voxNy;
    const double dz = (double)(g_voxMaxZ - g_voxMinZ) / (double)g_voxNz;
    pr.geomMinX = g_voxMinX; pr.geomMinY = g_voxMinY; pr.geomMinZ = g_voxMinZ;
    pr.geomMaxX = g_voxMaxX; pr.geomMaxY = g_voxMaxY; pr.geomMaxZ = g_voxMaxZ;
    pr.triangles = modelVertexCount / 3;
    pr.volumeM3  = (double)solid * dx * dy * dz;
    pr.areaM2    = 0.0;   // площадь оценим по построенной сетке МКЭ (там она точная по граням)
    pr.geometryLoaded = true;
    if (!aeroModelPath.empty()) pr.filePath = aeroModelPath;
    caeLog(0, "геометрия", "геометрия перенесена в исследование: %lld заполненных ячеек из %zu, объём %.6g м3, габарит %.4g x %.4g x %.4g м",
           solid, g_voxelData.size(), pr.volumeM3,
           pr.geomMaxX - pr.geomMinX, pr.geomMaxY - pr.geomMinY, pr.geomMaxZ - pr.geomMinZ);
    return true;
}

// -----------------------------------------------------
// Объёмная сетка МКЭ. STL — поверхность, а не объёмная сетка, поэтому элементы
// строятся по заполненным вокселям: это законный воксельный МКЭ (Q1-гексаэдры),
// а не «расчёт по поверхности».
// -----------------------------------------------------
inline bool caeBuildMesh() {
    StudyProject& pr = studyProject();
    if (!pr.geometryLoaded && !caeSyncGeometry()) return false;
    const int res = pr.mesh.resolution;
    if (res < 4 || res > pr.mesh.resolutionCap) {
        caeLog(2, "сетка", "разрешение %d вне допустимого диапазона 4..%d", res, pr.mesh.resolutionCap);
        return false;
    }
    FemMesh& m = caeMesh();
    bool ok = false;
    try {
        ok = femMeshBuildFromVoxels(m, g_voxelData, g_voxNx, g_voxNy, g_voxNz,
                                    g_voxMinX, g_voxMinY, g_voxMinZ,
                                    g_voxMaxX, g_voxMaxY, g_voxMaxZ,
                                    res, pr.materialIndex, pr.material().density);
    } catch (const std::bad_alloc&) {
        // так же, как lbm.cpp / voxel_grid.cpp / particles.cpp: честный отказ,
        // а не падение процесса из-за нехватки памяти
        m = FemMesh();
        caeLog(2, "сетка", "не хватило памяти на сетку такого размера — уменьшите разрешение");
        return false;
    } catch (const std::exception& e) {
        m = FemMesh();
        caeLog(2, "сетка", "построение сетки прервано: %s", e.what());
        return false;
    }
    for (const std::string& w : m.warnings) caeLog(1, "сетка", "%s", w.c_str());
    if (!ok || !m.built) {
        caeLog(2, "сетка", "не удалось построить объёмную сетку (элементов %d)", m.elemCount);
        return false;
    }
    pr.areaM2 = m.surfaceArea;
    pr.volumeM3 = m.volume;
    caeLog(0, "сетка", "построена сетка: %d x %d x %d = %d гексаэдров Q1, %d узлов, %zu внешних граней, объём %.6g м3, масса %.6g кг",
           m.ex, m.ey, m.ez, m.elemCount, m.nodeCount, m.faces.size(), m.volume, m.massKg);
    return true;
}

// -----------------------------------------------------
// Решение. Статус, невязка, число итераций и время — в StudyResults; сюда же
// складываются предупреждения решателя (сетка / граничные условия / сходимость).
// -----------------------------------------------------
inline bool caeSolve(bool rebuildMesh) {
    StudyProject& pr = studyProject();
    pr.status = StudySolveStatus::Preparing;
    pr.progressText = "проверка постановки";
    pr.issues = studyValidate(pr);
    int errors = 0;
    for (const StudyIssue& i : pr.issues) {
        if (i.level == StudyNodeState::Error) errors++;
        caeLog(i.level == StudyNodeState::Error ? 2 : 1, i.node.c_str(), "%s", i.text.c_str());
    }
    if (errors > 0) {
        pr.status = StudySolveStatus::Error;
        pr.results.status = StudySolveStatus::Error;
        pr.results.errorMessage = "в постановке есть ошибки — см. вкладку «Сообщения»";
        caeLog(2, "постановка", "расчёт не начат: ошибок %d", errors);
        return false;
    }
    if (!pr.geometryLoaded && !caeSyncGeometry()) {
        pr.status = StudySolveStatus::Error;
        return false;
    }
    if (rebuildMesh || !caeMesh().built) {
        pr.progressText = "построение сетки";
        if (!caeBuildMesh()) { pr.status = StudySolveStatus::Error; return false; }
    }
    pr.progress = 0.25;
    pr.status = StudySolveStatus::Running;
    pr.progressText = (pr.analysis == StudyAnalysis::LinearStatic)
                          ? "решение статической задачи" : "решение тепловой задачи";

    std::string err;
    bool ok = false;
    try {
    if (pr.analysis == StudyAnalysis::LinearStatic)
        ok = femSolveLinearStatic(caeMesh(), pr, pr.results, err);
    else if (pr.analysis == StudyAnalysis::SteadyThermal)
        ok = femSolveSteadyThermal(caeMesh(), pr, pr.results, err);
    else {
        // Модальный и нестационарный анализы НЕ реализованы: пункт в интерфейсе
        // не появляется, а прямой вызов получает явный отказ вместо «нулевого»
        // результата, который можно было бы принять за ответ.
        err = "этот тип анализа не реализован (см. документацию: следующие этапы)";
        caeLog(2, "анализ", "%s", err.c_str());
        // Иначе pr.status остался бы от предыдущего успешного расчёта: интерфейс
        // показывал бы «решено» и старые числа при фактически невыполненном расчёте.
        pr.results.status = StudySolveStatus::Error;
        pr.results.valid = false;
        pr.results.errorMessage = err;
    }
    } catch (const std::bad_alloc&) {
        // Нехватка памяти на систему такого размера. Результаты чистим: оставлять
        // частично заполненные поля нельзя — их приняли бы за ответ.
        ok = false;
        err = "не хватило памяти на расчёт такой сетки — уменьшите разрешение или предел сетки";
        pr.results.clear();
        pr.results.status = StudySolveStatus::Error;
        pr.results.errorMessage = err;
        caeLog(2, "решатель", "%s", err.c_str());
    } catch (const std::exception& e) {
        ok = false;
        err = std::string("расчёт прерван исключением: ") + e.what();
        pr.results.clear();
        pr.results.status = StudySolveStatus::Error;
        pr.results.errorMessage = err;
        caeLog(2, "решатель", "%s", err.c_str());
    }
    pr.progress = 1.0;
    pr.status = pr.results.status;
    pr.progressText = studyStatusName(pr.results.status);
    if (!err.empty()) caeLog(2, "решатель", "%s", err.c_str());
    for (const std::string& w : pr.results.warnings) {
        const char* cat = "решатель";
        if (w.find("сетк") != std::string::npos) cat = "сетка";
        else if (w.find("закреп") != std::string::npos || w.find("услов") != std::string::npos ||
                 w.find("гранич") != std::string::npos) cat = "граничные условия";
        else if (w.find("невязк") != std::string::npos || w.find("сходим") != std::string::npos ||
                 w.find("равновес") != std::string::npos) cat = "сходимость";
        caeLog(1, cat, "%s", w.c_str());
    }
    if (ok) {
        caeLog(0, "решатель", "%s: %s, итераций %d, невязка %.3e, время %.1f мс, элементов %d, узлов %d",
               studyAnalysisName(pr.analysis), studyStatusName(pr.results.status),
               pr.results.iterations, pr.results.finalResidual, pr.results.solveTimeMs,
               pr.results.elements, pr.results.nodes);
        if (pr.analysis == StudyAnalysis::LinearStatic)
            caeLog(0, "результаты", "макс. перемещение %.6g м, макс. фон Мизес %.6g Па, сумма сил %.6g Н, сумма реакций %.6g Н, энергия деформации %.6g Дж",
                   pr.results.maxDisplacement, pr.results.maxVonMises,
                   pr.results.appliedLoadSum, pr.results.reactionResultant, pr.results.strainEnergy);
        else
            caeLog(0, "результаты", "температура %.6g..%.6g K, макс. тепловой поток %.6g Вт/м2, суммарный поток %.6g Вт",
                   pr.results.minTemperature, pr.results.maxTemperature,
                   pr.results.maxHeatFlux, pr.results.totalHeatRate);
        // поле по умолчанию — то, что имеет смысл для выбранного анализа
        CaeViewState& v = caeView();
        if (pr.analysis == StudyAnalysis::LinearStatic) {
            if (v.field == ResultFieldId::Temperature || v.field == ResultFieldId::HeatFluxMag ||
                v.field == ResultFieldId::HeatFluxX || v.field == ResultFieldId::HeatFluxY ||
                v.field == ResultFieldId::HeatFluxZ)
                v.field = ResultFieldId::VonMises;
        } else {
            if (v.field != ResultFieldId::Temperature && v.field != ResultFieldId::HeatFluxMag &&
                v.field != ResultFieldId::HeatFluxX && v.field != ResultFieldId::HeatFluxY &&
                v.field != ResultFieldId::HeatFluxZ)
                v.field = ResultFieldId::Temperature;
        }
        caeApplyFieldToGeometry();
        return true;
    }
    caeLog(2, "решатель", "расчёт не завершён: %s", studyStatusName(pr.results.status));
    return false;
}

// -----------------------------------------------------
// Отображение поля на геометрии. Цвет каждой вершины модели — значение поля в её
// точке (трилинейная интерполяция по узлам сетки МКЭ), рамп — тот же, что у карты
// давления, поэтому внешний вид результатов и CFD согласован.
// Вершины вне тела красятся нейтрально: экстраполировать поле в пустоту нельзя.
// -----------------------------------------------------
inline bool caeApplyFieldToGeometry() {
    CaeViewState& v = caeView();
    const FemMesh& m = caeMesh();
    const StudyProject& pr = studyProject();
    caeColorOverride = false;
    if (!v.colorGeometry) { updateVertexColors(); return false; }
    // Каждый отказ обязан называть причину. Раньше функция просто возвращала false
    // (или, что хуже, красила всю модель в цвет «вне тела» и возвращала true, если
    // выбранное поле в этих результатах не считается), а интерфейс молчал.
    if (!m.built || !pr.results.valid) {
        caeLog(1, "отображение", "раскраска по полю не применена: %s",
               !m.built ? "нет объёмной сетки" : "нет результатов расчёта");
        return false;
    }
    const std::vector<double>* data = resultFieldData(pr.results, v.field);
    if (!data) {
        caeLog(2, "отображение",
               "поле «%s» в этих результатах не считается — раскраска не применена; "
               "выберите другое поле или включите его в настройках решателя",
               resultFieldName(v.field));
        return false;
    }
    const size_t nVert = g_vertices.size() / 3;
    if (nVert == 0) {
        caeLog(1, "отображение", "раскраска по полю не применена: у модели нет вершин");
        return false;
    }
    if (g_vertexColors.size() != nVert * 3) {
        try { g_vertexColors.assign(nVert * 3, 0.6f); }
        catch (...) {
            caeLog(2, "отображение", "не удалось выделить буфер цветов вершин на %zu значений",
                   nVert * 3);
            return false;
        }
    }
    v.scale.field = v.field;
    const bool symmetric = (v.field == ResultFieldId::StressXX || v.field == ResultFieldId::StressYY ||
                            v.field == ResultFieldId::StressZZ || v.field == ResultFieldId::StressXY ||
                            v.field == ResultFieldId::StressXZ || v.field == ResultFieldId::StressYZ ||
                            v.field == ResultFieldId::Ux || v.field == ResultFieldId::Uy ||
                            v.field == ResultFieldId::Uz || v.field == ResultFieldId::HeatFluxX ||
                            v.field == ResultFieldId::HeatFluxY || v.field == ResultFieldId::HeatFluxZ);
    resultScaleUpdate(v.scale, pr.results, symmetric || v.legendSymmetric);

    // значения для вершин, у которых поле не определено (вне тела)
    int outside = 0;
#ifdef _OPENMP
    #pragma omp parallel for reduction(+:outside)
#endif
    for (long long i = 0; i < (long long)nVert; ++i) {
        double val = 0.0;
        const bool got = data ? femSampleField(m, *data,
                                               (double)g_vertices[3*i], (double)g_vertices[3*i+1],
                                               (double)g_vertices[3*i+2], val) : false;
        glm::vec3 col(0.62f, 0.62f, 0.66f);
        if (got) {
            col = pressureColorInRange((float)val, (float)v.scale.lo, (float)v.scale.hi);
        } else {
            outside++;
        }
        if (!std::isfinite(col.x) || !std::isfinite(col.y) || !std::isfinite(col.z))
            col = glm::vec3(0.5f);
        g_vertexColors[3*i]   = col.x;
        g_vertexColors[3*i+1] = col.y;
        g_vertexColors[3*i+2] = col.z;
    }
    if (outside > 0 && (long long)outside == (long long)nVert) {
        caeLog(1, "отображение", "ни одна вершина модели не попала в расчётную область — проверьте, что геометрия синхронизирована");
    }
    // цвета готовы: загружаем их в VBO, минуя пересчёт карты Cp
    caeColorOverride = true;
    uploadVertexColorsToGPU();
    showPressure = false;   // карта Cp и поле МКЭ — разные величины, не смешиваем
    return true;
}

// -----------------------------------------------------
// Исследование сходимости по сетке: одно и то же на трёх разрешениях. Программа
// не заявляет точность, которой не проверяла, — она показывает, как меняется
// ключевая величина, и пользователь видит, сошёлся ли результат.
// -----------------------------------------------------
inline void caeRunMeshConvergence() {
    StudyProject& pr = studyProject();
    CaeViewState& v = caeView();
    v.convergence.clear();
    if (!pr.geometryLoaded && !caeSyncGeometry()) return;
    const int base = pr.mesh.resolution;
    const int tries[3] = {
        std::max(4, base / 2),
        base,
        std::min(pr.mesh.resolutionCap, base + base / 2)
    };
    caeLog(0, "сходимость", "исследование сходимости: разрешения %d / %d / %d", tries[0], tries[1], tries[2]);
    FemMesh backup = caeMesh();
    StudyResults backupRes = pr.results;
    const StudySolveStatus backupStatus = pr.status;
    for (int k = 0; k < 3; ++k) {
        pr.mesh.resolution = tries[k];
        if (!caeBuildMesh()) { caeLog(2, "сходимость", "разрешение %d: сетка не построилась", tries[k]); continue; }
        std::string err;
        StudyResults res;
        bool ok = false;
        try {
            ok = (pr.analysis == StudyAnalysis::LinearStatic)
                     ? femSolveLinearStatic(caeMesh(), pr, res, err)
                     : femSolveSteadyThermal(caeMesh(), pr, res, err);
        } catch (const std::bad_alloc&) {
            err = "не хватило памяти на это разрешение";
            caeLog(2, "сходимость", "разрешение %d: %s", tries[k], err.c_str());
        } catch (const std::exception& e) {
            err = std::string("расчёт прерван исключением: ") + e.what();
            caeLog(2, "сходимость", "разрешение %d: %s", tries[k], err.c_str());
        }
        CaeViewState::MeshPoint mp;
        mp.resolution = tries[k];
        mp.elements = res.elements;
        mp.nodes = res.nodes;
        mp.iterations = res.iterations;
        mp.residual = res.finalResidual;
        mp.timeMs = res.solveTimeMs;
        mp.maxValue = ok ? ((pr.analysis == StudyAnalysis::LinearStatic) ? res.maxDisplacement : res.maxTemperature)
                         : 0.0;
        v.convergence.push_back(mp);
        caeLog(ok ? 0 : 2, "сходимость", "разрешение %d: элементов %d, узлов %d, итераций %d, невязка %.2e, время %.1f мс, %s %.6g %s",
               tries[k], mp.elements, mp.nodes, mp.iterations, mp.residual, mp.timeMs,
               (pr.analysis == StudyAnalysis::LinearStatic) ? "макс. перемещение" : "макс. температура",
               mp.maxValue,
               (pr.analysis == StudyAnalysis::LinearStatic) ? unitNameLength(pr.units) : unitNameTemp(pr.units));
        if (!ok && !err.empty()) caeLog(2, "сходимость", "разрешение %d: %s", tries[k], err.c_str());
    }
    // возвращаем рабочее состояние, чтобы пользователь не потерял основной расчёт
    pr.mesh.resolution = base;
    caeMesh() = backup;
    pr.results = backupRes;
    pr.status = backupStatus;
    v.statusLine = "исследование сходимости завершено";
}

// -----------------------------------------------------
// Экспорт: CSV с результатами и текстовый отчёт. Пути задаёт пользователь через
// нативный диалог сохранения (или вводом пути в модальном окне, если диалог
// недоступен в этой сборке).
// -----------------------------------------------------
inline bool caeExportCSV() {
    StudyProject& pr = studyProject();
    if (!pr.results.valid) { caeLog(2, "экспорт", "нет результатов для экспорта — сначала выполните расчёт"); return false; }
    std::vector<ResultFieldId> fields;
    if (pr.analysis == StudyAnalysis::LinearStatic)
        fields = { ResultFieldId::DisplacementMag, ResultFieldId::Ux, ResultFieldId::Uy, ResultFieldId::Uz,
                   ResultFieldId::VonMises, ResultFieldId::StressXX, ResultFieldId::StressYY,
                   ResultFieldId::StressZZ, ResultFieldId::StressXY };
    else
        fields = { ResultFieldId::Temperature, ResultFieldId::HeatFluxMag,
                   ResultFieldId::HeatFluxX, ResultFieldId::HeatFluxY, ResultFieldId::HeatFluxZ };
    std::string path = saveFileDialogNative("Aeros-CAE-results.csv");
    if (path.empty()) { caeLog(1, "экспорт", "файл CSV не выбран — экспорт отменён"); return false; }
    std::string err;
    if (!resultExportCSV(caeMesh(), pr, fields, path, err)) {
        caeLog(2, "экспорт", "не удалось записать CSV: %s", err.c_str());
        return false;
    }
    caeLog(0, "экспорт", "результаты сохранены в CSV: %s (полей %zu, узлов %d, единицы СИ)",
           path.c_str(), fields.size(), caeMesh().nodeCount);
    return true;
}

inline bool caeExportReport() {
    StudyProject& pr = studyProject();
    std::string path = saveFileDialogNative("Aeros-CAE-report.txt");
    if (path.empty()) { caeLog(1, "экспорт", "файл отчёта не выбран — экспорт отменён"); return false; }
    const std::string text = studyBuildReport(pr, caeMesh());
    std::ofstream f(path);
    if (!f) { caeLog(2, "экспорт", "не удалось открыть файл отчёта для записи"); return false; }
    f << text;
    if (!f.good()) { caeLog(2, "экспорт", "ошибка записи отчёта"); return false; }
    caeLog(0, "экспорт", "отчёт сохранён: %s (%zu байт)", path.c_str(), text.size());
    return true;
}

inline bool caeSaveStudy() {
    StudyProject& pr = studyProject();
    std::string path = saveFileDialogNative("Aeros-Study.aerosproj");
    if (path.empty()) { caeLog(1, "проект", "файл не выбран — сохранение отменено"); return false; }
    std::string err;
    if (!studySaveProject(pr, path)) { caeLog(2, "проект", "не удалось сохранить исследование: %s", path.c_str()); return false; }
    pr.projectPath = path;
    caeLog(0, "проект", "исследование сохранено: %s", path.c_str());
    return true;
}

inline bool caeLoadStudy() {
    std::string path = openFileDialog();
    if (path.empty()) { caeLog(1, "проект", "файл не выбран — загрузка отменена"); return false; }
    StudyProject& pr = studyProject();
    if (!studyLoadProject(pr, path)) { caeLog(2, "проект", "не удалось загрузить исследование: %s", path.c_str()); return false; }
    pr.projectPath = path;
    pr.results.clear();
    pr.status = StudySolveStatus::NotStarted;
    caeMesh() = FemMesh();
    caeLog(0, "проект", "исследование загружено: %s (материалов %zu, условий %zu, анализ: %s)",
           path.c_str(), pr.materials.size(), pr.loads.size(), studyAnalysisName(pr.analysis));
    pr.issues = studyValidate(pr);
    return true;
}

// -----------------------------------------------------
// Состояние узла дерева: 0 не настроено, 1 готово, 2 предупреждение, 3 ошибка,
// 4 решено. Берётся из фактической валидации, а не «нарисовано».
// -----------------------------------------------------
inline int caeNodeState(const StudyProject& pr, const char* nodeName) {
    int worst = 0;
    for (const StudyIssue& i : pr.issues) {
        if (i.node != nodeName) continue;
        const int lv = (i.level == StudyNodeState::Error) ? 3 : 2;
        if (lv > worst) worst = lv;
    }
    if (worst > 0) return worst;
    // узел настроен, если по нему нет замечаний
    return 1;
}

inline int caeRootState(const StudyProject& pr) {
    if (pr.results.valid && pr.results.status == StudySolveStatus::Converged) return 4;
    int worst = 0;
    for (const StudyIssue& i : pr.issues) {
        const int lv = (i.level == StudyNodeState::Error) ? 3 : 2;
        if (lv > worst) worst = lv;
    }
    if (worst == 0 && pr.geometryLoaded) return 1;
    if (!pr.geometryLoaded && worst == 0) return 0;
    return worst;
}

// -----------------------------------------------------
// Ввод величины с переводом между СИ и единицами интерфейса. Подпись всегда
// содержит единицу, чтобы значения разных систем не смешивались на экране.
// -----------------------------------------------------
inline bool caeInputSI(const char* label, double* si, const StudyUnits& u, double unitSize,
                       const char* unitName, double step = 0.0) {
    if (!si) return false;
    double disp = unitToDisplay(*si, unitSize);
    if (!std::isfinite(disp)) disp = 0.0;
    char lbl[224];
    std::snprintf(lbl, sizeof(lbl), "%s, %s", label, unitName ? unitName : "");
    const bool changed = ImGui::InputDouble(lbl, &disp, step, step > 0 ? step * 10.0 : 0.0, "%.6g");
    if (changed && std::isfinite(disp)) *si = unitFromDisplay(disp, unitSize);
    (void)u;
    return changed;
}

// Текст «значение + единица» для вывода (без редактирования).
inline void caeTextSI(const char* label, double si, double unitSize, const char* unitName) {
    char b[256];
    std::snprintf(b, sizeof(b), "%s: %.6g %s", label, unitToDisplay(si, unitSize), unitName ? unitName : "");
    ImGui::TextUnformatted(b);
}

// -----------------------------------------------------
// Дерево исследования (левая панель, рабочее пространство «Исследование»).
// -----------------------------------------------------
inline void caeDrawStudyTree() {
    StudyProject& pr = studyProject();
    CaeViewState& v = caeView();
    pr.issues = studyValidate(pr);

    const ImGuiTreeNodeFlags gf = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;

    if (ImGui::TreeNodeEx(_TR("Study", "Исследование"), gf)) {
        uiDockStateDot(caeRootState(pr));
        if (ImGui::Selectable(studyAnalysisName(pr.analysis), v.studySel == CAE_STUDY_ANALYSIS,
                              ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f)))
            v.studySel = CAE_STUDY_ANALYSIS;
        uiDockStateDot(caeNodeState(pr, "Геометрия"));
        if (ImGui::Selectable(_TR("Geometry", "Геометрия"), v.studySel == CAE_STUDY_GEOMETRY,
                              ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f)))
            v.studySel = CAE_STUDY_GEOMETRY;
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx(_TR("Units", "Единицы"), gf)) {
        uiDockStateDot(1);
        if (ImGui::Selectable(_TR("Unit system", "Система единиц"), v.studySel == CAE_STUDY_UNITS,
                              ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f)))
            v.studySel = CAE_STUDY_UNITS;
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx(_TR("Materials", "Материалы"), gf)) {
        uiDockStateDot(caeNodeState(pr, "Материалы"));
        if (ImGui::Selectable(_TR("Material list", "Список материалов"), v.studySel == CAE_STUDY_MATERIALS,
                              ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f)))
            v.studySel = CAE_STUDY_MATERIALS;
        for (size_t i = 0; i < pr.materials.size(); ++i) {
            const int sel = CAE_STUDY_MAT_BASE + (int)i;
            char label[256];
            std::snprintf(label, sizeof(label), "%s%s", pr.materials[i].name.c_str(),
                          (int)i == pr.materialIndex ? "  *" : "");
            uiDockStateDot((int)i == pr.materialIndex ? 1 : 0);
            ImGui::PushID((int)i);
            if (ImGui::Selectable(label, v.studySel == sel, ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f)))
                v.studySel = sel;
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx(_TR("Loads and constraints", "Нагрузки и закрепления"), gf)) {
        uiDockStateDot(caeNodeState(pr, "Нагрузки"));
        if (ImGui::Selectable(_TR("Load list", "Список условий"), v.studySel == CAE_STUDY_LOADS,
                              ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f)))
            v.studySel = CAE_STUDY_LOADS;
        for (size_t i = 0; i < pr.loads.size(); ++i) {
            const int sel = CAE_STUDY_LOAD_BASE + (int)i;
            char label[288];
            std::snprintf(label, sizeof(label), "%s [%s]%s", pr.loads[i].name.c_str(),
                          studyBCKindName(pr.loads[i].kind), pr.loads[i].enabled ? "" : "  (выкл)");
            uiDockStateDot(pr.loads[i].enabled ? 1 : 0);
            ImGui::PushID(1000 + (int)i);
            if (ImGui::Selectable(label, v.studySel == sel, ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f)))
                v.studySel = sel;
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx(_TR("Mesh and solver", "Сетка и решатель"), gf)) {
        const int meshState = caeMesh().built ? 1 : 0;
        uiDockStateDot(meshState);
        if (ImGui::Selectable(_TR("Volume mesh", "Объёмная сетка"), v.studySel == CAE_STUDY_MESH,
                              ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f)))
            v.studySel = CAE_STUDY_MESH;
        uiDockStateDot(1);
        if (ImGui::Selectable(_TR("Solver settings", "Настройки решателя"), v.studySel == CAE_STUDY_SOLVER,
                              ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f)))
            v.studySel = CAE_STUDY_SOLVER;
        ImGui::TreePop();
    }

    const int resState = !pr.results.valid ? 0
                       : (pr.results.status == StudySolveStatus::Converged ? 4
                       : (pr.results.status == StudySolveStatus::NotConverged ? 2 : 3));
    uiDockStateDot(resState);
    if (ImGui::Selectable(_TR("Results", "Результаты"), v.studySel == CAE_STUDY_RESULTS,
                          ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f)))
        v.studySel = CAE_STUDY_RESULTS;

    ImGui::Separator();
    ImGui::TextDisabled("%s", _TR("* — material assigned to the study", "* — материал, назначенный исследованию"));
}

// Подпись выбранного узла дерева исследования — для заголовка панели свойств:
// пользователь всегда видит, ЧЬИ именно свойства перед ним.
inline const char* caeStudySelName(int sel) {
    if (sel == CAE_STUDY_ANALYSIS)  return _TR("Analysis", "Анализ");
    if (sel == CAE_STUDY_GEOMETRY)  return _TR("Geometry", "Геометрия");
    if (sel == CAE_STUDY_UNITS)     return _TR("Unit system", "Система единиц");
    if (sel == CAE_STUDY_MATERIALS) return _TR("Materials", "Материалы");
    if (sel == CAE_STUDY_LOADS)     return _TR("Loads and constraints", "Нагрузки и закрепления");
    if (sel == CAE_STUDY_MESH)      return _TR("Volume mesh", "Объёмная сетка");
    if (sel == CAE_STUDY_SOLVER)    return _TR("Solver settings", "Настройки решателя");
    if (sel == CAE_STUDY_RESULTS)   return _TR("Results", "Результаты");
    StudyProject& pr = studyProject();
    if (sel >= CAE_STUDY_MAT_BASE && sel < CAE_STUDY_LOAD_BASE) {
        const int i = sel - CAE_STUDY_MAT_BASE;
        if (i >= 0 && i < (int)pr.materials.size()) return pr.materials[(size_t)i].name.c_str();
    }
    if (sel >= CAE_STUDY_LOAD_BASE) {
        const int i = sel - CAE_STUDY_LOAD_BASE;
        if (i >= 0 && i < (int)pr.loads.size()) return pr.loads[(size_t)i].name.c_str();
    }
    return _TR("not selected", "не выбрано");
}

inline const char* caeResultSelName(int sel) {
    if (sel == CAE_RES_PROBE)  return _TR("Point probe", "Проба в точке");
    if (sel == CAE_RES_SLICE)  return _TR("Slices", "Срезы");
    if (sel == CAE_RES_PLOT)   return _TR("Plots", "Графики");
    if (sel == CAE_RES_TABLE)  return _TR("Key values table", "Таблица величин");
    if (sel == CAE_RES_CONV)   return _TR("Mesh convergence", "Сходимость по сетке");
    if (sel == CAE_RES_EXPORT) return _TR("Export", "Экспорт");
    CaeViewState& v = caeView();
    size_t cnt = 0;
    const ResultFieldInfo* tab = resultFieldTable(&cnt);
    for (size_t i = 0; i < cnt; ++i) if (tab[i].id == v.field) return tab[i].name;
    return _TR("Legend and scale", "Легенда и шкала");
}

// -----------------------------------------------------
// Панель свойств исследования: показывает ровно то, что выбрано в дереве.
// -----------------------------------------------------
inline void caeDrawStudyProps() {
    StudyProject& pr = studyProject();
    CaeViewState& v = caeView();
    const StudyUnits u = pr.unitsSI();

    // ---- шапка: команда «Рассчитать» всегда на виду ----
    if (ImGui::Button(_TR("Solve", "Рассчитать"), ImVec2(-1, 0))) {
        caeSolve(true);
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("Build mesh", "Построить сетку"))) caeBuildMesh();
    ImGui::SameLine();
    if (ImGui::Button(_TR("Sync geometry", "Синхронизировать геометрию"))) caeSyncGeometry();
    ImGui::Text("%s: %s", _TR("Status", "Статус"), studyStatusName(pr.status));
    ImGui::Separator();

    // ---- анализ ----
    if (v.studySel == CAE_STUDY_ANALYSIS) {
        ImGui::TextUnformatted(_TR("Analysis type", "Тип анализа"));
        int a = (int)pr.analysis;
        // Только реально реализованные типы. Модальный и нестационарный появятся,
        // когда будет готова общая база — в списке их нет, чтобы нельзя было
        // выбрать неработающий режим.
        const char* items[] = {
            studyAnalysisName(StudyAnalysis::LinearStatic),
            studyAnalysisName(StudyAnalysis::SteadyThermal)
        };
        if (ImGui::Combo(_TR("Analysis", "Анализ"), &a, items, 2)) {
            pr.analysis = (a == 1) ? StudyAnalysis::SteadyThermal : StudyAnalysis::LinearStatic;
            pr.results.clear();
            pr.status = StudySolveStatus::NotStarted;
            caeLog(0, "анализ", "тип анализа изменён на «%s»; прежние результаты сброшены",
                   studyAnalysisName(pr.analysis));
        }
        ImGui::TextWrapped("%s", _TR("Linear static: small displacements, linear-elastic isotropic material, "
                                     "constraints and loads, results — displacements, stresses, von Mises.",
                                     "Линейный статический расчёт: малые перемещения, линейно-упругий изотропный "
                                     "материал, закрепления и нагрузки; результаты — перемещения, напряжения, фон Мизес."));
        ImGui::TextWrapped("%s", _TR("Steady thermal: conduction with prescribed temperatures, heat flux, "
                                     "heat generation and convection; results — temperature field and heat flux.",
                                     "Стационарная теплопередача: теплопроводность с заданными температурами, "
                                     "потоком, тепловыделением и конвекцией; результаты — поле температур и тепловой поток."));
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s",
            _TR("Modal and transient analyses are NOT implemented yet — planned for the next stage.",
                "Модальный и нестационарный анализы НЕ реализованы — это следующий этап."));
        ImGui::Separator();
        ImGui::TextUnformatted(_TR("Issues of the setup", "Замечания к постановке"));
        if (pr.issues.empty()) ImGui::TextDisabled("%s", _TR("no issues", "замечаний нет"));
        for (const StudyIssue& i : pr.issues) {
            ImVec4 col = (i.level == StudyNodeState::Error) ? ImVec4(1.0f, 0.4f, 0.35f, 1.0f)
                                                            : ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
            ImGui::TextColored(col, "[%s] %s: %s", studyNodeStateName(i.level), i.node.c_str(), i.text.c_str());
        }
    }
    // ---- геометрия ----
    else if (v.studySel == CAE_STUDY_GEOMETRY) {
        ImGui::TextUnformatted(_TR("Geometry", "Геометрия"));
        ImGui::TextWrapped("%s: %s", _TR("Model file", "Файл модели"),
                           pr.filePath.empty() ? _TR("(not loaded)", "(не загружен)") : pr.filePath.c_str());
        ImGui::Text("%s: %d | %s: %d", _TR("Triangles", "Треугольников"), pr.triangles,
                    _TR("Vertices", "Вершин"), modelVertexCount);
        caeTextSI(_TR("Volume", "Объём"), pr.volumeM3, 1.0, "м3");
        caeTextSI(_TR("Surface area", "Площадь поверхности"), pr.areaM2, 1.0, "м2");
        char b[256];
        std::snprintf(b, sizeof(b), "%s: [%.6g .. %.6g] x [%.6g .. %.6g] x [%.6g .. %.6g] м",
                      _TR("Bounding box", "Габарит"), pr.geomMinX, pr.geomMaxX,
                      pr.geomMinY, pr.geomMaxY, pr.geomMinZ, pr.geomMaxZ);
        ImGui::TextUnformatted(b);
        std::snprintf(b, sizeof(b), "%s: %d x %d x %d (ячеек), домен [%.6g..%.6g] x [%.6g..%.6g] x [%.6g..%.6g] м",
                      _TR("Voxel grid", "Воксельная сетка"), g_voxNx, g_voxNy, g_voxNz,
                      g_voxMinX, g_voxMaxX, g_voxMinY, g_voxMaxY, g_voxMinZ, g_voxMaxZ);
        ImGui::TextWrapped("%s", b);
        ImGui::Separator();
        ImGui::TextWrapped("%s", _TR("The volume mesh is built from the voxel representation of the model "
                                     "(the same one the LBM solver uses). An STL surface alone is NOT a volume "
                                     "FEM mesh, so the geometry is staircase-approximated with the cell size.",
                                     "Объёмная сетка строится по воксельному представлению модели (тому же, что "
                                     "использует решатель LBM). Поверхность STL сама по себе НЕ является объёмной "
                                     "сеткой МКЭ, поэтому геометрия приближена ступенькой размером с ячейку."));
        if (ImGui::Button(_TR("Re-sync geometry from model", "Перенести геометрию из модели заново")))
            caeSyncGeometry();
    }
    // ---- единицы ----
    else if (v.studySel == CAE_STUDY_UNITS) {
        ImGui::TextUnformatted(_TR("Unit system", "Система единиц"));
        int sys = (int)pr.units;
        const char* items[] = { unitSystemName(StudyUnitSystem::SI), unitSystemName(StudyUnitSystem::mmMPa) };
        if (ImGui::Combo(_TR("Display units", "Единицы отображения"), &sys, items, 2)) {
            pr.units = (sys == 1) ? StudyUnitSystem::mmMPa : StudyUnitSystem::SI;
            caeLog(0, "единицы", "система единиц интерфейса: %s (ядро и файл проекта всегда в СИ)",
                   unitSystemName(pr.units));
        }
        ImGui::Separator();
        ImGui::TextUnformatted(_TR("Coherent unit set derived from the base units", "Согласованный набор единиц, выведенный из базовых"));
        char b[256];
        std::snprintf(b, sizeof(b), "длина: %g м = 1 %s", u.length, unitNameLength(pr.units));            ImGui::TextUnformatted(b);
        std::snprintf(b, sizeof(b), "напряжение: %g Па = 1 %s", u.stress, unitNameStress(pr.units));      ImGui::TextUnformatted(b);
        std::snprintf(b, sizeof(b), "сила: %g Н = 1 %s", u.force, unitNameForce(pr.units));               ImGui::TextUnformatted(b);
        std::snprintf(b, sizeof(b), "масса: %g кг = 1 %s", u.mass, unitNameDensity(pr.units));            ImGui::TextUnformatted(b);
        std::snprintf(b, sizeof(b), "плотность: %g кг/м3 = 1 %s", u.density, unitNameDensity(pr.units));  ImGui::TextUnformatted(b);
        std::snprintf(b, sizeof(b), "мощность: %g Вт = 1 %s", u.power, unitNamePower(pr.units));          ImGui::TextUnformatted(b);
        std::snprintf(b, sizeof(b), "энергия: %g Дж = 1 %s", u.energy, unitNameEnergy(pr.units));         ImGui::TextUnformatted(b);
        std::snprintf(b, sizeof(b), "теплопроводность: %g Вт/(м·K) = 1 %s", u.conductivity, unitNameConductivity(pr.units)); ImGui::TextUnformatted(b);
        std::snprintf(b, sizeof(b), "тепловой поток: %g Вт/м2 = 1 %s", u.heatFlux, unitNameHeatFlux(pr.units)); ImGui::TextUnformatted(b);
        std::snprintf(b, sizeof(b), "тепловыделение: %g Вт/м3 = 1 %s", u.heatGen, unitNameHeatGen(pr.units));   ImGui::TextUnformatted(b);
        std::snprintf(b, sizeof(b), "коэф. теплоотдачи: %g Вт/(м2·K) = 1 %s", u.hCoeff, unitNameHCoeff(pr.units)); ImGui::TextUnformatted(b);
        std::snprintf(b, sizeof(b), "температура: %s (всегда абсолютная)", unitNameTemp(pr.units));       ImGui::TextUnformatted(b);
        ImGui::Separator();
        ImGui::TextWrapped("%s", _TR("The solver core and the project file always store SI. Units affect display "
                                     "and export labels only — values are never converted twice.",
                                     "Ядро решателя и файл проекта всегда хранят СИ. Единицы влияют только на "
                                     "отображение и подписи экспорта — значения никогда не переводятся дважды."));
    }
    // ---- материалы ----
    else if (v.studySel == CAE_STUDY_MATERIALS) {
        ImGui::TextUnformatted(_TR("Materials", "Материалы"));
        int idx = pr.materialIndex;
        std::vector<const char*> names;
        for (const StudyMaterial& m : pr.materials) names.push_back(m.name.c_str());
        if (!names.empty() && ImGui::Combo(_TR("Assigned material", "Назначенный материал"), &idx,
                                           names.data(), (int)names.size())) {
            pr.materialIndex = idx;
            caeLog(0, "материалы", "назначен материал «%s»", pr.material().name.c_str());
        }
        if (ImGui::Button(_TR("Add from library", "Добавить из библиотеки"))) {
            const std::vector<StudyMaterial> lib = studyMaterialLibrary();
            ImGui::OpenPopup(_TR("Material library", "Библиотека материалов"));
            (void)lib;
        }
        if (ImGui::BeginPopupModal(_TR("Material library", "Библиотека материалов"), nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            const std::vector<StudyMaterial> lib = studyMaterialLibrary();
            for (size_t i = 0; i < lib.size(); ++i) {
                char b[256];
                std::snprintf(b, sizeof(b), "%s  (E=%.4g %s, k=%.4g)", lib[i].name.c_str(),
                              unitToDisplay(lib[i].youngE, u.stress), unitNameStress(pr.units),
                              unitToDisplay(lib[i].conductivity, u.conductivity));
                if (ImGui::Button(b, ImVec2(-1, 0))) {
                    bool exists = false;
                    for (const StudyMaterial& m : pr.materials) if (m.name == lib[i].name) exists = true;
                    if (exists) caeLog(1, "материалы", "материал «%s» уже есть в списке", lib[i].name.c_str());
                    else { pr.materials.push_back(lib[i]); caeLog(0, "материалы", "добавлен материал «%s»", lib[i].name.c_str()); }
                    ImGui::CloseCurrentPopup();
                }
            }
            if (ImGui::Button(_TR("Close", "Закрыть"), ImVec2(-1, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(_TR("Remove selected", "Удалить выбранный"))) {
            if (pr.materials.size() <= 1) caeLog(2, "материалы", "нельзя удалить единственный материал");
            else {
                caeLog(0, "материалы", "удалён материал «%s»", pr.materials[(size_t)pr.materialIndex].name.c_str());
                pr.materials.erase(pr.materials.begin() + pr.materialIndex);
                pr.materialIndex = 0;
            }
        }
    }
    else if (v.studySel >= CAE_STUDY_MAT_BASE && v.studySel < CAE_STUDY_LOAD_BASE) {
        const int mi = v.studySel - CAE_STUDY_MAT_BASE;
        if (mi >= 0 && mi < (int)pr.materials.size()) {
            StudyMaterial& m = pr.materials[(size_t)mi];
            ImGui::TextUnformatted(_TR("Material properties", "Свойства материала"));
            char nm[192];
            std::strncpy(nm, m.name.c_str(), sizeof(nm) - 1); nm[sizeof(nm) - 1] = 0;
            if (ImGui::InputText(_TR("Name", "Название"), nm, sizeof(nm))) m.name = nm;
            if (ImGui::RadioButton(_TR("Assign to study", "Назначить исследованию"), pr.materialIndex == mi))
                pr.materialIndex = mi;
            ImGui::Separator();
            bool usedStructural = (pr.analysis == StudyAnalysis::LinearStatic);
            if (usedStructural) {
                if (caeInputSI(_TR("Young modulus E", "Модуль Юнга E"), &m.youngE, u, u.stress, unitNameStress(pr.units)))
                    caeLog(0, "материалы", "«%s»: E = %.6g Па", m.name.c_str(), m.youngE);
                ImGui::SetNextItemWidth(140);
                ImGui::InputDouble(_TR("Poisson ratio nu", "Коэффициент Пуассона nu"), &m.poisson, 0.01, 0.1, "%.4f");
                if (caeInputSI(_TR("Yield strength", "Предел текучести"), &m.yieldStrength, u, u.stress, unitNameStress(pr.units)))
                    caeLog(0, "материалы", "«%s»: предел текучести = %.6g Па", m.name.c_str(), m.yieldStrength);
            } else {
                ImGui::BeginDisabled(true);
                caeInputSI(_TR("Young modulus E", "Модуль Юнга E"), &m.youngE, u, u.stress, unitNameStress(pr.units));
                ImGui::EndDisabled();
                ImGui::TextDisabled("%s", _TR("not used by the thermal analysis", "не используется в тепловом анализе"));
            }
            if (caeInputSI(_TR("Density", "Плотность"), &m.density, u, u.density, unitNameDensity(pr.units)))
                caeLog(0, "материалы", "«%s»: плотность = %.6g кг/м3", m.name.c_str(), m.density);
            if (pr.analysis == StudyAnalysis::SteadyThermal) {
                if (caeInputSI(_TR("Conductivity k", "Теплопроводность k"), &m.conductivity, u, u.conductivity,
                               unitNameConductivity(pr.units)))
                    caeLog(0, "материалы", "«%s»: k = %.6g Вт/(м·K)", m.name.c_str(), m.conductivity);
            } else {
                ImGui::BeginDisabled(true);
                caeInputSI(_TR("Conductivity k", "Теплопроводность k"), &m.conductivity, u, u.conductivity,
                           unitNameConductivity(pr.units));
                ImGui::EndDisabled();
                ImGui::TextDisabled("%s", _TR("not used by the structural analysis", "не используется в прочностном анализе"));
            }
            ImGui::Separator();
            char why[288];
            if (!m.valid(why, sizeof(why)))
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s", why);
            else
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s", _TR("properties are valid", "свойства корректны"));
            ImGui::TextWrapped("%s", _TR("Values are stored in SI; shown in the selected display unit system.",
                                         "Значения хранятся в СИ; показаны в выбранной системе единиц."));
        }
    }
    // ---- список условий ----
    else if (v.studySel == CAE_STUDY_LOADS) {
        ImGui::TextUnformatted(_TR("Loads and constraints", "Нагрузки и закрепления"));
        if (ImGui::Button(_TR("Add condition", "Добавить условие"))) {
            StudyLoad l;
            char nm[64];
            std::snprintf(nm, sizeof(nm), "%s %zu", _TR("Condition", "Условие"), pr.loads.size() + 1);
            l.name = nm;
            l.kind = (pr.analysis == StudyAnalysis::LinearStatic) ? StudyBCKind::Fixed : StudyBCKind::Temperature;
            if (pr.geometryLoaded) l.region = studyFaceRegion(pr, 0, false, _TR("face X-", "грань X-"));
            pr.loads.push_back(l);
            v.studySel = CAE_STUDY_LOAD_BASE + (int)pr.loads.size() - 1;
            caeLog(0, "граничные условия", "добавлено условие «%s» [%s]", l.name.c_str(), studyBCKindName(l.kind));
        }
        ImGui::TextWrapped("%s", _TR("Quick presets by the faces of the model bounding box:",
                                     "Быстрые заготовки по граням габарита модели:"));
        for (int axis = 0; axis < 3; ++axis)
            for (int sign = 0; sign < 2; ++sign) {
                if (axis > 0 || sign > 0) ImGui::SameLine();
                char b[64];
                std::snprintf(b, sizeof(b), "%c%c", "XYZ"[axis], sign ? '+' : '-');
                if (!pr.geometryLoaded) { ImGui::BeginDisabled(true); ImGui::Button(b); ImGui::EndDisabled(); continue; }
                if (ImGui::Button(b)) {
                    StudyLoad l;
                    char nm[64];
                    std::snprintf(nm, sizeof(nm), "%s %zu", _TR("Condition", "Условие"), pr.loads.size() + 1);
                    l.name = nm;
                    l.kind = (pr.analysis == StudyAnalysis::LinearStatic) ? StudyBCKind::Fixed : StudyBCKind::Temperature;
                    l.region = studyFaceRegion(pr, axis, sign != 0, b);
                    pr.loads.push_back(l);
                    v.studySel = CAE_STUDY_LOAD_BASE + (int)pr.loads.size() - 1;
                    caeLog(0, "граничные условия", "добавлено условие «%s» на грани %s [%s]",
                           l.name.c_str(), b, studyBCKindName(l.kind));
                }
            }
        ImGui::Separator();
        for (size_t i = 0; i < pr.loads.size(); ++i) {
            char b[288];
            std::snprintf(b, sizeof(b), "%zu. %s [%s]%s", i + 1, pr.loads[i].name.c_str(),
                          studyBCKindName(pr.loads[i].kind), pr.loads[i].enabled ? "" : " (выкл)");
            if (ImGui::Selectable(b, v.studySel == CAE_STUDY_LOAD_BASE + (int)i,
                                  ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f)))
                v.studySel = CAE_STUDY_LOAD_BASE + (int)i;
        }
        if (pr.loads.empty())
            ImGui::TextDisabled("%s", _TR("no conditions — the solver cannot start",
                                          "условий нет — решатель не сможет стартовать"));
    }
    else if (v.studySel >= CAE_STUDY_LOAD_BASE) {
        const int li = v.studySel - CAE_STUDY_LOAD_BASE;
        if (li >= 0 && li < (int)pr.loads.size()) {
            StudyLoad& l = pr.loads[(size_t)li];
            ImGui::TextUnformatted(_TR("Condition properties", "Свойства условия"));
            char nm[192];
            std::strncpy(nm, l.name.c_str(), sizeof(nm) - 1); nm[sizeof(nm) - 1] = 0;
            if (ImGui::InputText(_TR("Name", "Название"), nm, sizeof(nm))) l.name = nm;
            ImGui::Checkbox(_TR("Enabled", "Включено"), &l.enabled);

            // тип условия: только допустимые для текущего анализа
            std::vector<StudyBCKind> kinds;
            std::vector<const char*> knames;
            if (pr.analysis == StudyAnalysis::LinearStatic) {
                kinds = { StudyBCKind::Fixed, StudyBCKind::Displacement, StudyBCKind::Force,
                          StudyBCKind::Pressure, StudyBCKind::Gravity };
            } else {
                kinds = { StudyBCKind::Temperature, StudyBCKind::HeatFlux, StudyBCKind::HeatGeneration,
                          StudyBCKind::Convection };
            }
            for (StudyBCKind k : kinds) knames.push_back(studyBCKindName(k));
            int cur = 0;
            for (size_t i = 0; i < kinds.size(); ++i) if (kinds[i] == l.kind) cur = (int)i;
            if (ImGui::Combo(_TR("Type", "Тип"), &cur, knames.data(), (int)knames.size())) {
                l.kind = kinds[(size_t)cur];
                caeLog(0, "граничные условия", "«%s»: тип изменён на [%s]", l.name.c_str(), studyBCKindName(l.kind));
            }

            ImGui::Separator();
            ImGui::TextUnformatted(_TR("Region (metres, model coordinates)", "Область (метры, координаты модели)"));
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.42f);
            ImGui::InputDouble(_TR("X min", "X мин"), &l.region.minX, 0, 0, "%.6g"); ImGui::SameLine();
            ImGui::InputDouble(_TR("X max", "X макс"), &l.region.maxX, 0, 0, "%.6g");
            ImGui::InputDouble(_TR("Y min", "Y мин"), &l.region.minY, 0, 0, "%.6g"); ImGui::SameLine();
            ImGui::InputDouble(_TR("Y max", "Y макс"), &l.region.maxY, 0, 0, "%.6g");
            ImGui::InputDouble(_TR("Z min", "Z мин"), &l.region.minZ, 0, 0, "%.6g"); ImGui::SameLine();
            ImGui::InputDouble(_TR("Z max", "Z макс"), &l.region.maxZ, 0, 0, "%.6g");
            ImGui::PopItemWidth();
            ImGui::Checkbox(_TR("Only nodes on the body surface", "Только узлы на поверхности тела"),
                            &l.region.onlyBoundary);
            if (pr.geometryLoaded) {
                ImGui::TextDisabled("%s", _TR("Set region to a face of the bounding box:", "Задать область гранью габарита:"));
                for (int axis = 0; axis < 3; ++axis)
                    for (int sign = 0; sign < 2; ++sign) {
                        if (axis > 0 || sign > 0) ImGui::SameLine();
                        char b[64];
                        std::snprintf(b, sizeof(b), "%c%c", "XYZ"[axis], sign ? '+' : '-');
                        if (ImGui::Button(b)) {
                            l.region = studyFaceRegion(pr, axis, sign != 0, b);
                            caeLog(0, "граничные условия", "«%s»: область = грань %s", l.name.c_str(), b);
                        }
                    }
            }
            // фильтр по нормали — для условий, заданных на поверхности
            if (studyBCIsSurface(l.kind)) {
                ImGui::Separator();
                ImGui::TextUnformatted(_TR("Face selection (outward normal)", "Выбор грани (внешняя нормаль)"));
                int ax = l.region.normalAxis + 1;
                const char* axNames[] = { _TR("any face", "любая грань"), "X", "Y", "Z" };
                if (ImGui::Combo(_TR("Normal axis", "Ось нормали"), &ax, axNames, 4))
                    l.region.normalAxis = ax - 1;
                if (l.region.normalAxis >= 0)
                    ImGui::Checkbox(_TR("Positive direction", "Положительное направление"), &l.region.normalPositive);
                ImGui::TextDisabled("%s: %s", _TR("Selected", "Выбрано"), l.region.normalName());
                ImGui::TextWrapped("%s", _TR("Without the normal filter a box region would also catch the side faces "
                                             "near the loaded end. Pick a face, not a volume.",
                                             "Без фильтра по нормали область-бокс захватывает и боковые грани у "
                                             "нагруженного торца. Выбирайте грань, а не объём."));
            }

            ImGui::Separator();
            switch (l.kind) {
                case StudyBCKind::Fixed:
                    ImGui::TextWrapped("%s", _TR("All displacement components are zero in the region "
                                                 "(u = v = w = 0).",
                                                 "В области обнуляются все компоненты перемещений (u = v = w = 0)."));
                    ImGui::TextWrapped("%s", _TR("Warning: constraining all three components on a flat end also "
                                                 "suppresses Poisson contraction, so the model is stiffer than a real "
                                                 "clamp. Refine the mesh or use a shorter constrained zone.",
                                                 "Внимание: закрепление всех трёх компонент на плоском торце подавляет "
                                                 "пуассоновское сужение, поэтому модель жёстче реального защемления. "
                                                 "Измельчайте сетку или уменьшайте зону закрепления."));
                    break;
                case StudyBCKind::Displacement:
                    caeInputSI(_TR("u_x", "u_x"), &l.vx, u, u.length, unitNameLength(pr.units));
                    caeInputSI(_TR("u_y", "u_y"), &l.vy, u, u.length, unitNameLength(pr.units));
                    caeInputSI(_TR("u_z", "u_z"), &l.vz, u, u.length, unitNameLength(pr.units));
                    break;
                case StudyBCKind::Force:
                    caeInputSI(_TR("F_x", "F_x"), &l.vx, u, u.force, unitNameForce(pr.units));
                    caeInputSI(_TR("F_y", "F_y"), &l.vy, u, u.force, unitNameForce(pr.units));
                    caeInputSI(_TR("F_z", "F_z"), &l.vz, u, u.force, unitNameForce(pr.units));
                    ImGui::TextWrapped("%s", _TR("The total force is split equally between the nodes of the region. "
                                                 "For a uniform traction use Pressure instead.",
                                                 "Суммарная сила делится поровну между узлами области. Для равномерно "
                                                 "распределённой нагрузки используйте «Давление»."));
                    break;
                case StudyBCKind::Pressure:
                    caeInputSI(_TR("Pressure p", "Давление p"), &l.value, u, u.stress, unitNameStress(pr.units));
                    ImGui::TextWrapped("%s", _TR("Consistent nodal forces p·n·A/4 on the selected faces — "
                                                 "uniform traction, not corner-concentrated loads.",
                                                 "Согласованные узловые силы p·n·A/4 на выбранных гранях — равномерная "
                                                 "нагрузка, а не сосредоточенные силы в углах."));
                    break;
                case StudyBCKind::Gravity:
                    ImGui::InputDouble(_TR("g_x, m/s2", "g_x, м/с2"), &l.vx, 0, 0, "%.6g");
                    ImGui::InputDouble(_TR("g_y, m/s2", "g_y, м/с2"), &l.vy, 0, 0, "%.6g");
                    ImGui::InputDouble(_TR("g_z, m/s2", "g_z, м/с2"), &l.vz, 0, 0, "%.6g");
                    if (ImGui::Button(_TR("Set -9.81 along Y", "Задать -9.81 по Y"))) { l.vx = 0; l.vy = -9.81; l.vz = 0; }
                    ImGui::SameLine();
                    if (ImGui::Button(_TR("Set -9.81 along Z", "Задать -9.81 по Z"))) { l.vx = 0; l.vy = 0; l.vz = -9.81; }
                    break;
                case StudyBCKind::Temperature:
                    caeInputSI(_TR("Temperature T", "Температура T"), &l.value, u, u.temp, unitNameTemp(pr.units));
                    break;
                case StudyBCKind::HeatFlux:
                    caeInputSI(_TR("Heat flux q (into the body)", "Тепловой поток q (в тело)"),
                               &l.value, u, u.heatFlux, unitNameHeatFlux(pr.units));
                    break;
                case StudyBCKind::HeatGeneration:
                    caeInputSI(_TR("Volumetric heat generation Q", "Объёмное тепловыделение Q"),
                               &l.value, u, u.heatGen, unitNameHeatGen(pr.units));
                    break;
                case StudyBCKind::Convection:
                    caeInputSI(_TR("Film coefficient h", "Коэффициент теплоотдачи h"),
                               &l.value, u, u.hCoeff, unitNameHCoeff(pr.units));
                    caeInputSI(_TR("Ambient temperature", "Температура среды"),
                               &l.ambient, u, u.temp, unitNameTemp(pr.units));
                    ImGui::TextWrapped("%s", _TR("Lumped Robin condition: f += h·T_amb·A/4, K += h·A/4 on the faces.",
                                                 "Сосредоточенное условие Робена: f += h·T_окр·A/4, K += h·A/4 на гранях."));
                    break;
            }

            ImGui::Separator();
            if (ImGui::Button(_TR("Duplicate", "Дублировать"))) {
                StudyLoad copy = l;
                char nm[96];
                std::snprintf(nm, sizeof(nm), "%s (копия)", l.name.c_str());
                copy.name = nm;
                pr.loads.push_back(copy);
                v.studySel = CAE_STUDY_LOAD_BASE + (int)pr.loads.size() - 1;
                caeLog(0, "граничные условия", "создана копия условия «%s»", l.name.c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button(_TR("Delete", "Удалить"))) {
                caeLog(0, "граничные условия", "удалено условие «%s»", l.name.c_str());
                pr.loads.erase(pr.loads.begin() + li);
                v.studySel = CAE_STUDY_LOADS;
            }
        }
    }
    // ---- сетка ----
    else if (v.studySel == CAE_STUDY_MESH) {
        ImGui::TextUnformatted(_TR("Volume mesh", "Объёмная сетка"));
        if (ImGui::SliderInt(_TR("Resolution (cells along the largest side)", "Разрешение (ячеек вдоль наибольшей стороны)"),
                             &pr.mesh.resolution, 4, pr.mesh.resolutionCap))
            caeLog(0, "сетка", "разрешение сетки = %d", pr.mesh.resolution);
        ImGui::SameLine();
        if (ImGui::Button(_TR("Build", "Построить"))) caeBuildMesh();
        const FemMesh& m = caeMesh();
        if (m.built) {
            char b[288];
            std::snprintf(b, sizeof(b), "%d x %d x %d = %d гексаэдров Q1, %d узлов, %d внешних граней",
                          m.ex, m.ey, m.ez, m.elemCount, m.nodeCount, (int)m.faces.size());
            ImGui::TextUnformatted(b);
            std::snprintf(b, sizeof(b), "ячейка %.6g x %.6g x %.6g %s, объём тела %.6g м3, поверхность %.6g м2, масса %.6g кг",
                          unitToDisplay(m.hx, u.length), unitToDisplay(m.hy, u.length), unitToDisplay(m.hz, u.length),
                          unitNameLength(pr.units), m.volume, m.surfaceArea, m.massKg);
            ImGui::TextWrapped("%s", b);
            std::snprintf(b, sizeof(b), "несвязных частей: %d, худшее соотношение сторон ячейки: %.2f",
                          m.components, m.aspectWorst);
            ImGui::TextWrapped("%s", b);
            std::snprintf(b, sizeof(b), "допуск попадания узла/грани в область условия: %.4g м (половина наибольшей стороны ячейки)",
                          femMeshTolerance(m));
            ImGui::TextWrapped("%s", b);
            if (m.components > 1)
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f),
                                   "%s", _TR("the body has several disconnected parts — without constraints on each "
                                             "of them the system is singular",
                                             "тело состоит из нескольких несвязных частей — без закреплений на каждой "
                                             "из них система сингулярна"));
        } else {
            ImGui::TextDisabled("%s", _TR("mesh is not built yet", "сетка ещё не построена"));
        }
        ImGui::Separator();
        ImGui::TextWrapped("%s", _TR("Method: hexahedral Q1 elements over the filled voxels of the model "
                                     "(structured voxel FEM). The geometry is staircase-approximated with the cell "
                                     "size, so the volume and the stress concentrators are approximate. Q1 elements "
                                     "are stiff in bending (shear locking) — refine the mesh for bending-dominated "
                                     "cases. Conjugate gradients with Jacobi preconditioning; the matrix is not "
                                     "stored (matrix-free).",
                                     "Метод: гексаэдры Q1 по заполненным вокселям модели (структурный воксельный МКЭ). "
                                     "Геометрия приближена ступенькой размером с ячейку, поэтому объём и концентраторы "
                                     "напряжений приближённые. Элементы Q1 завышают жёсткость при изгибе (shear "
                                     "locking) — измельчайте сетку в изгибных задачах. Метод сопряжённых градиентов с "
                                     "диагональным предусловливанием; матрица не хранится (matrix-free)."));
    }
    // ---- решатель ----
    else if (v.studySel == CAE_STUDY_SOLVER) {
        ImGui::TextUnformatted(_TR("Solver settings", "Настройки решателя"));
        if (ImGui::InputInt(_TR("Iteration limit", "Предел итераций"), &pr.solver.maxIterations)) {
            if (pr.solver.maxIterations < 10) pr.solver.maxIterations = 10;
            if (pr.solver.maxIterations > 200000) pr.solver.maxIterations = 200000;
        }
        ImGui::InputDouble(_TR("Relative residual tolerance ||r||/||f||", "Допуск относительной невязки ||r||/||f||"),
                           &pr.solver.relTolerance, 0, 0, "%.2e");
        if (!(pr.solver.relTolerance > 0) || pr.solver.relTolerance > 1e-2) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s",
                               _TR("tolerance must be in (0 .. 1e-2]", "допуск должен быть в (0 .. 1e-2]"));
        }
        ImGui::Checkbox(_TR("Jacobi preconditioning", "Диагональное предусловливание (Jacobi)"), &pr.solver.jacobiPrecond);
        ImGui::Separator();
        ImGui::TextWrapped("%s", _TR("The solver is synchronous: the interface does not update while it runs. "
                                     "Iteration count grows quickly with mesh size — the method is CG without "
                                     "multigrid.",
                                     "Решатель работает синхронно: интерфейс не обновляется, пока идёт расчёт. "
                                     "Число итераций быстро растёт с размером сетки — метод CG, без многосеточного "
                                     "ускорения."));
    }
    // ---- результаты (в дереве исследования — сводка) ----
    else if (v.studySel == CAE_STUDY_RESULTS) {
        caeDrawResultsSummary(pr, u);
    }
    (void)v;
}

// Сводка результатов — используется и в дереве исследования, и во вкладке «Результаты».
inline void caeDrawResultsSummary(const StudyProject& pr, const StudyUnits& u) {
    const StudyResults& r = pr.results;
    ImGui::TextUnformatted(_TR("Results", "Результаты"));
    ImGui::Text("%s: %s", _TR("Status", "Статус"), studyStatusName(r.status));
    if (!r.valid) {
        ImGui::TextDisabled("%s", _TR("no results — run the solver", "результатов нет — выполните расчёт"));
        return;
    }
    char b[288];
    std::snprintf(b, sizeof(b), "%s: %d, %s: %d, %s: %d", _TR("elements", "элементов"), r.elements,
                  _TR("nodes", "узлов"), r.nodes, _TR("DOFs", "степеней свободы"), r.dofs);
    ImGui::TextUnformatted(b);
    std::snprintf(b, sizeof(b), "%s: %d, %s: %.3e, %s: %.1f %s",
                  _TR("iterations", "итераций"), r.iterations,
                  _TR("residual", "невязка"), r.finalResidual,
                  _TR("time", "время"), r.solveTimeMs, _TR("ms", "мс"));
    ImGui::TextUnformatted(b);
    ImGui::Separator();
    if (r.analysis == StudyAnalysis::LinearStatic) {
        caeTextSI(_TR("Max displacement |u|", "Макс. перемещение |u|"), r.maxDisplacement, u.length, unitNameLength(pr.units));
        caeTextSI(_TR("Max von Mises stress", "Макс. напряжение фон Мизеса"), r.maxVonMises, u.stress, unitNameStress(pr.units));
        caeTextSI(_TR("Yield strength", "Предел текучести"), pr.material().yieldStrength, u.stress, unitNameStress(pr.units));
        if (pr.material().yieldStrength > 0.0 && r.maxVonMises > 0.0) {
            const double sf = pr.material().yieldStrength / r.maxVonMises;
            std::snprintf(b, sizeof(b), "%s: %.3f", _TR("Safety factor (yield/vonMises)", "Запас (текучесть/фон Мизес)"), sf);
            ImGui::TextColored(sf < 1.0 ? ImVec4(1.0f, 0.4f, 0.35f, 1.0f) : ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s", b);
            if (sf < 1.0)
                ImGui::TextWrapped("%s", _TR("von Mises exceeds the yield strength: the LINEAR solution is not valid "
                                             "there, a nonlinear analysis is required.",
                                             "Напряжение фон Мизеса превышает предел текучести: ЛИНЕЙНОЕ решение в этой "
                                             "зоне недействительно, нужен нелинейный расчёт."));
        }
        caeTextSI(_TR("Applied load resultant", "Сумма приложенных сил"), r.appliedLoadSum, u.force, unitNameForce(pr.units));
        caeTextSI(_TR("Reaction resultant", "Сумма реакций"), r.reactionResultant, u.force, unitNameForce(pr.units));
        caeTextSI(_TR("Strain energy", "Энергия деформации"), r.strainEnergy, u.energy, unitNameEnergy(pr.units));
        if (r.appliedLoadSum > 0.0) {
            const double err = std::fabs(r.reactionResultant - r.appliedLoadSum) / r.appliedLoadSum * 100.0;
            std::snprintf(b, sizeof(b), "%s: %.3f %%", _TR("Equilibrium check |R|-|F| / |F|", "Проверка равновесия |R|-|F| / |F|"), err);
            ImGui::TextColored(err > 2.0 ? ImVec4(1.0f, 0.75f, 0.3f, 1.0f) : ImVec4(0.75f, 0.85f, 0.9f, 1.0f), "%s", b);
        }
    } else {
        caeTextSI(_TR("Min temperature", "Мин. температура"), r.minTemperature, u.temp, unitNameTemp(pr.units));
        caeTextSI(_TR("Max temperature", "Макс. температура"), r.maxTemperature, u.temp, unitNameTemp(pr.units));
        caeTextSI(_TR("Max heat flux", "Макс. тепловой поток"), r.maxHeatFlux, u.heatFlux, unitNameHeatFlux(pr.units));
        caeTextSI(_TR("Total heat rate", "Суммарный поток"), r.totalHeatRate, u.power, unitNamePower(pr.units));
    }
    if (!r.errorMessage.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s", r.errorMessage.c_str());
    if (!r.warnings.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(_TR("Solver warnings", "Предупреждения решателя"));
        for (const std::string& w : r.warnings)
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "* %s", w.c_str());
    }
}

// -----------------------------------------------------
// Рабочее пространство «Результаты»: дерево полей и панели легенды/пробы/срезов.
// -----------------------------------------------------
inline void caeDrawResultsTree() {
    StudyProject& pr = studyProject();
    CaeViewState& v = caeView();
    const ImGuiTreeNodeFlags gf = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;

    if (ImGui::TreeNodeEx(_TR("Field", "Поле"), gf)) {
        size_t cnt = 0;
        const ResultFieldInfo* tab = resultFieldTable(&cnt);
        for (size_t i = 0; i < cnt; ++i) {
            if (!resultFieldAvailable(pr.results, tab[i].id)) continue;
            const int sel = CAE_RES_FIELD_BASE + (int)i;
            uiDockStateDot(v.field == tab[i].id ? 4 : 1);
            ImGui::PushID((int)i);
            if (ImGui::Selectable(tab[i].name, v.studySel == sel && v.field == tab[i].id,
                                  ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f))) {
                v.field = tab[i].id;
                v.studySel = sel;
                v.resultSel = 0;
                caeApplyFieldToGeometry();
                caeLog(0, "отображение", "выбрано поле «%s»", tab[i].name);
            }
            ImGui::PopID();
        }
        if (!pr.results.valid)
            ImGui::TextDisabled("%s", _TR("no fields yet — run the solver", "полей пока нет — выполните расчёт"));
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx(_TR("Inspection", "Инструменты просмотра"), gf)) {
        struct { int id; const char* en; const char* ru; } items[] = {
            { CAE_RES_LEGEND, "Legend and scale", "Легенда и шкала" },
            { CAE_RES_PROBE,  "Point probe",      "Проба в точке" },
            { CAE_RES_SLICE,  "Slices",           "Срезы" },
            { CAE_RES_PLOT,   "Plots",            "Графики" },
            { CAE_RES_TABLE,  "Key values table", "Таблица величин" },
            { CAE_RES_CONV,   "Mesh convergence", "Сходимость по сетке" },
            { CAE_RES_EXPORT, "Export",           "Экспорт" },
        };
        for (auto& it : items) {
            uiDockStateDot(1);
            if (ImGui::Selectable(_TR(it.en, it.ru), v.resultSel == it.id,
                                  ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f)))
                v.resultSel = it.id;
        }
        ImGui::TreePop();
    }

    ImGui::Separator();
    ImGui::TextDisabled("%s: %s", _TR("Analysis", "Анализ"), studyAnalysisName(pr.analysis));
    ImGui::TextDisabled("%s: %s", _TR("Status", "Статус"), studyStatusName(pr.results.status));
}

// Цвет для легенды: тот же рамп, что у карты давления (единый вид программы).
inline ImU32 caeRampColorU32(float t) {
    CaeViewState& v = caeView();
    const float val = (float)v.scale.lo + t * (float)(v.scale.hi - v.scale.lo);
    const glm::vec3 c = pressureColorInRange(val, (float)v.scale.lo, (float)v.scale.hi);
    return IM_COL32((int)(c.x * 255.0f), (int)(c.y * 255.0f), (int)(c.z * 255.0f), 255);
}

inline void caeDrawResultsProps() {
    StudyProject& pr = studyProject();
    CaeViewState& v = caeView();
    const StudyUnits u = pr.unitsSI();
    const FemMesh& m = caeMesh();

    ImGui::Checkbox(_TR("Color the model by the field", "Раскрасить модель по полю"), &v.colorGeometry);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", _TR("Vertex colors are taken from the FEM field; the Cp map is turned off "
                                    "to avoid mixing two different quantities.",
                                    "Цвета вершин берутся из поля МКЭ; карта Cp отключается, чтобы не смешивать "
                                    "две разные величины."));
    if (ImGui::Button(_TR("Apply field to geometry", "Применить поле к геометрии"))) caeApplyFieldToGeometry();
    ImGui::SameLine();
    if (ImGui::Button(_TR("Restore Cp coloring", "Вернуть раскраску по Cp"))) {
        v.colorGeometry = false;
        caeColorOverride = false;
        showPressure = true;
        updateVertexColors();
        caeLog(0, "отображение", "возвращена раскраска по коэффициенту давления");
    }
    ImGui::Separator();

    // имя и единицы текущего поля
    size_t cnt = 0;
    const ResultFieldInfo* tab = resultFieldTable(&cnt);
    const ResultFieldInfo* fi = nullptr;
    for (size_t i = 0; i < cnt; ++i) if (tab[i].id == v.field) fi = &tab[i];
    if (!fi) fi = &tab[0];
    const char* unit = resultUnitName(fi->unitKind, pr.units);
    const double usize = resultUnitSize(fi->unitKind, u);
    char b[288];
    std::snprintf(b, sizeof(b), "%s: %s (%s)", _TR("Field", "Поле"), fi->name, unit);
    ImGui::TextUnformatted(b);

    if (v.resultSel == CAE_RES_LEGEND || v.resultSel == 0) {
        ImGui::TextUnformatted(_TR("Legend and scale", "Легенда и шкала"));
        v.scale.field = v.field;
        const bool symmetric = (v.field == ResultFieldId::StressXX || v.field == ResultFieldId::StressYY ||
                                v.field == ResultFieldId::StressZZ || v.field == ResultFieldId::StressXY ||
                                v.field == ResultFieldId::StressXZ || v.field == ResultFieldId::StressYZ ||
                                v.field == ResultFieldId::Ux || v.field == ResultFieldId::Uy ||
                                v.field == ResultFieldId::Uz);
        resultScaleUpdate(v.scale, pr.results, symmetric || v.legendSymmetric);
        if (pr.results.valid) {
            std::snprintf(b, sizeof(b), "%s: %.6g %s   %s: %.6g %s",
                          _TR("min", "мин"), unitToDisplay(v.scale.dataMin, usize), unit,
                          _TR("max", "макс"), unitToDisplay(v.scale.dataMax, usize), unit);
            ImGui::TextUnformatted(b);
        }
        if (ImGui::Checkbox(_TR("Auto range", "Автоматический диапазон"), &v.scale.autoRange))
            caeApplyFieldToGeometry();
        if (!v.scale.autoRange) {
            double lo = unitToDisplay(v.scale.manualLo, usize), hi = unitToDisplay(v.scale.manualHi, usize);
            ImGui::InputDouble(_TR("Scale min", "Минимум шкалы"), &lo, 0, 0, "%.6g");
            ImGui::InputDouble(_TR("Scale max", "Максимум шкалы"), &hi, 0, 0, "%.6g");
            v.scale.manualLo = unitFromDisplay(lo, usize);
            v.scale.manualHi = unitFromDisplay(hi, usize);
            resultScaleUpdate(v.scale, pr.results, symmetric || v.legendSymmetric);
            if (ImGui::Button(_TR("Apply manual range", "Применить ручной диапазон"))) caeApplyFieldToGeometry();
            ImGui::SameLine();
            if (ImGui::Button(_TR("Take from data", "Взять из данных"))) {
                v.scale.manualLo = v.scale.dataMin; v.scale.manualHi = v.scale.dataMax;
                caeApplyFieldToGeometry();
            }
        }
        ImGui::Checkbox(_TR("Symmetric range around zero", "Симметричный диапазон относительно нуля"),
                        &v.legendSymmetric);
        std::snprintf(b, sizeof(b), "%s: %.6g .. %.6g %s", _TR("Scale", "Шкала"),
                      unitToDisplay(v.scale.lo, usize), unitToDisplay(v.scale.hi, usize), unit);
        ImGui::TextUnformatted(b);
        // легенда-полоса
        const float w = ImGui::GetContentRegionAvail().x - 8.0f;
        std::snprintf(b, sizeof(b), "%s, %s", fi->name, unit);
        char loTxt[96], hiTxt[96];
        std::snprintf(loTxt, sizeof(loTxt), "%.4g", unitToDisplay(v.scale.lo, usize));
        std::snprintf(hiTxt, sizeof(hiTxt), "%.4g", unitToDisplay(v.scale.hi, usize));
        uiDockLegendBar(w > 60.0f ? w : 60.0f, 18.0f, b, loTxt, hiTxt, caeRampColorU32);
        ImGui::TextWrapped("%s", _TR("The ramp is the same as for the pressure map (the colormap is chosen in the "
                                     "Model workspace), so results and CFD look consistent.",
                                     "Рамп тот же, что у карты давления (палитра выбирается в пространстве «Модель»), "
                                     "поэтому результаты и CFD выглядят согласованно."));
    }
    else if (v.resultSel == CAE_RES_PROBE) {
        ImGui::TextUnformatted(_TR("Point probe", "Проба в точке"));
        ImGui::Checkbox(_TR("Enabled", "Включено"), &v.probeEnabled);
        ImGui::PushItemWidth(-1);
        ImGui::InputDouble(_TR("X, m", "X, м"), &v.probeX, 0, 0, "%.6g");
        ImGui::InputDouble(_TR("Y, m", "Y, м"), &v.probeY, 0, 0, "%.6g");
        ImGui::InputDouble(_TR("Z, m", "Z, м"), &v.probeZ, 0, 0, "%.6g");
        ImGui::PopItemWidth();
        if (m.built) {
            if (ImGui::Button(_TR("Center of bounding box", "Центр габарита"))) {
                v.probeX = 0.5 * (pr.geomMinX + pr.geomMaxX);
                v.probeY = 0.5 * (pr.geomMinY + pr.geomMaxY);
                v.probeZ = 0.5 * (pr.geomMinZ + pr.geomMaxZ);
            }
            ImGui::SameLine();
            if (ImGui::Button(_TR("Sample", "Измерить"))) v.probeValid = true;
            if (v.probeEnabled || v.probeValid) {
                double val = 0.0;
                const std::vector<double>* data = resultFieldData(pr.results, v.field);
                const bool got = data && femSampleField(m, *data, v.probeX, v.probeY, v.probeZ, val);
                v.probeValid = got;
                v.probeValue = got ? val : 0.0;
                if (got) {
                    std::snprintf(b, sizeof(b), "%s = %.6g %s", fi->name, unitToDisplay(val, usize), unit);
                    ImGui::TextColored(ImVec4(0.45f, 0.9f, 1.0f, 1.0f), "%s", b);
                    std::snprintf(b, sizeof(b), "%s: (%.6g, %.6g, %.6g) м", _TR("Point", "Точка"),
                                  v.probeX, v.probeY, v.probeZ);
                    ImGui::TextUnformatted(b);
                    std::snprintf(b, sizeof(b), "%s: %.4f", _TR("Position in scale", "Положение в шкале"),
                                  resultScaleT(v.scale, val));
                    ImGui::TextUnformatted(b);
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s",
                                       _TR("point is outside the body or the field is not computed",
                                           "точка вне тела или поле не рассчитано"));
                }
            }
            ImGui::TextWrapped("%s", _TR("Value is trilinear interpolation of the nodal field. If any of the 8 "
                                         "surrounding nodes is outside the body, the nearest existing node value is "
                                         "returned instead of extrapolating through the void.",
                                         "Значение — трилинейная интерполяция узловых данных. Если хотя бы один из 8 "
                                         "окрестных узлов вне тела, возвращается значение ближайшего существующего "
                                         "узла, а не экстраполяция через пустоту."));
        }
    }
    else if (v.resultSel == CAE_RES_SLICE) {
        ImGui::TextUnformatted(_TR("Slices", "Срезы"));
        const char* axes[] = { "X", "Y", "Z" };
        ImGui::Combo(_TR("Plane normal", "Нормаль плоскости"), &v.sliceAxis, axes, 3);
        if (v.sliceAxis < 0) v.sliceAxis = 0;
        if (v.sliceAxis > 2) v.sliceAxis = 2;
        double cmin = (v.sliceAxis == 0) ? pr.geomMinX : (v.sliceAxis == 1 ? pr.geomMinY : pr.geomMinZ);
        double cmax = (v.sliceAxis == 0) ? pr.geomMaxX : (v.sliceAxis == 1 ? pr.geomMaxY : pr.geomMaxZ);
        if (!(cmax > cmin)) { cmin = 0.0; cmax = 1.0; }
        ImGui::SliderScalar(_TR("Plane coordinate, m", "Координата плоскости, м"), ImGuiDataType_Double,
                            &v.sliceCoord, &cmin, &cmax, "%.6g");
        ImGui::SliderInt(_TR("Samples per side", "Отсчётов на сторону"), &v.sliceSamples, 8, 128);
        ImGui::Checkbox(_TR("Show slice", "Показать срез"), &v.sliceEnabled);
        if (ImGui::Button(_TR("Build slice", "Построить срез"))) {
            const std::vector<double>* data = resultFieldData(pr.results, v.field);
            if (!data) caeLog(2, "срезы", "нет данных поля — выполните расчёт");
            else if (!resultBuildSlice(v.slice, m, *data, v.sliceAxis, v.sliceCoord, v.sliceSamples))
                caeLog(2, "срезы", "срез не построен: точка вне тела или поле пусто");
            else {
                v.sliceEnabled = true;
                caeLog(0, "срезы", "срез %c = %.6g м: %d x %d отсчётов, значения %.6g .. %.6g %s",
                       "XYZ"[v.sliceAxis], v.sliceCoord, v.slice.nu, v.slice.nv,
                       unitToDisplay(v.slice.vmin, usize), unitToDisplay(v.slice.vmax, usize), unit);
            }
        }
        if (v.slice.ok) {
            std::snprintf(b, sizeof(b), "%c = %.6g м, %d x %d, %.6g .. %.6g %s",
                          "XYZ"[v.slice.axis], v.slice.coord, v.slice.nu, v.slice.nv,
                          unitToDisplay(v.slice.vmin, usize), unitToDisplay(v.slice.vmax, usize), unit);
            ImGui::TextUnformatted(b);
            // тепловая карта среза
            const float cell = 8.0f;
            const float w = (float)v.slice.nu * cell;
            const float h = (float)v.slice.nv * cell;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (int j = 0; j < v.slice.nv; ++j)
            for (int i = 0; i < v.slice.nu; ++i) {
                const size_t idx = (size_t)j * (size_t)v.slice.nu + (size_t)i;
                ImU32 col = IM_COL32(40, 40, 44, 255);
                if (v.slice.valid[idx]) {
                    const float t = (float)resultScaleT(v.scale, v.slice.values[idx]);
                    const glm::vec3 c = pressureColorInRange(
                        (float)v.scale.lo + t * (float)(v.scale.hi - v.scale.lo),
                        (float)v.scale.lo, (float)v.scale.hi);
                    col = IM_COL32((int)(c.x * 255.0f), (int)(c.y * 255.0f), (int)(c.z * 255.0f), 255);
                }
                dl->AddRectFilled(ImVec2(p.x + i * cell, p.y + h - (j + 1) * cell),
                                  ImVec2(p.x + (i + 1) * cell, p.y + h - j * cell), col);
            }
            dl->AddRect(p, ImVec2(p.x + w, p.y + h), IM_COL32(255, 255, 255, 90));
            ImGui::Dummy(ImVec2(w, h));
        }
    }
    else if (v.resultSel == CAE_RES_PLOT) {
        ImGui::TextUnformatted(_TR("Plots along a line", "Графики вдоль линии"));
        ImGui::TextWrapped("%s", _TR("Line from point A to point B; values are sampled from the nodal field.",
                                     "Линия от точки A до точки B; значения берутся из узловых данных поля."));
        // концы линии живут в состоянии просмотра (CaeViewState), а не в static
        // внутри панели — см. пояснение в results_view.h
        double& ax = v.lineAx; double& ay = v.lineAy; double& az = v.lineAz;
        double& bx = v.lineBx; double& by = v.lineBy; double& bz = v.lineBz;
        ImGui::PushItemWidth(-1);
        ImGui::InputDouble(_TR("A X, m", "A X, м"), &ax, 0, 0, "%.6g");
        ImGui::InputDouble(_TR("A Y, m", "A Y, м"), &ay, 0, 0, "%.6g");
        ImGui::InputDouble(_TR("A Z, m", "A Z, м"), &az, 0, 0, "%.6g");
        ImGui::InputDouble(_TR("B X, m", "B X, м"), &bx, 0, 0, "%.6g");
        ImGui::InputDouble(_TR("B Y, m", "B Y, м"), &by, 0, 0, "%.6g");
        ImGui::InputDouble(_TR("B Z, m", "B Z, м"), &bz, 0, 0, "%.6g");
        ImGui::PopItemWidth();
        if (ImGui::Button(_TR("Build plot", "Построить график"))) {
            const std::vector<double>* data = resultFieldData(pr.results, v.field);
            if (!data) caeLog(2, "графики", "нет данных поля — выполните расчёт");
            else if (!resultBuildLine(v.line, m, *data, ax, ay, az, bx, by, bz, 64))
                caeLog(2, "графики", "линия не пересекает тело");
            else
                caeLog(0, "графики", "график построен: %zu точек вдоль линии длиной %.6g м",
                       v.line.v.size(), v.line.s.back());
        }
        if (v.line.ok) {
            std::vector<float> xs(v.line.s.size()), ys(v.line.v.size());
            for (size_t i = 0; i < v.line.v.size(); ++i) {
                xs[i] = (float)v.line.s[i];
                ys[i] = (float)unitToDisplay(v.line.v[i], usize);
            }
            char lbl[192];
            std::snprintf(lbl, sizeof(lbl), "%s, %s", fi->name, unit);
            ImGui::PlotLines(lbl, ys.data(), (int)ys.size(), 0, nullptr,
                             FLT_MAX, FLT_MAX, ImVec2(-1, 90));
            std::snprintf(b, sizeof(b), "%s, %s", _TR("distance along the line", "координата вдоль линии"), _TR("m", "м"));
            ImGui::PlotLines(b, xs.data(), (int)xs.size(), 0, nullptr, FLT_MAX, FLT_MAX, ImVec2(-1, 40));
        }
        ImGui::Separator();
        ImGui::TextUnformatted(_TR("Solver residual history", "История невязки решателя"));
        if (pr.results.residualHistory.empty())
            ImGui::TextDisabled("%s", _TR("no history yet", "истории пока нет"));
        else {
            std::vector<float> lg;
            lg.reserve(pr.results.residualHistory.size());
            for (double rr : pr.results.residualHistory) {
                const double d = (rr > 1e-16) ? std::log10(rr) : -16.0;
                lg.push_back((float)d);
            }
            ImGui::PlotLines("log10(||r||/||f||)", lg.data(), (int)lg.size(), 0, nullptr,
                             FLT_MAX, FLT_MAX, ImVec2(-1, 70));
        }
    }
    else if (v.resultSel == CAE_RES_TABLE) {
        ImGui::TextUnformatted(_TR("Key values", "Ключевые величины"));
        std::vector<ResultKeyValue> kv = resultKeyValues(pr, m);
        if (ImGui::BeginTable("##caeKv", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                             ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn(_TR("Quantity", "Величина"), ImGuiTableColumnFlags_WidthStretch, 0.42f);
            ImGui::TableSetupColumn(_TR("Value", "Значение"), ImGuiTableColumnFlags_WidthStretch, 0.30f);
            ImGui::TableSetupColumn(_TR("Note", "Примечание"), ImGuiTableColumnFlags_WidthStretch, 0.28f);
            ImGui::TableHeadersRow();
            for (const ResultKeyValue& e : kv) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(e.name.c_str());
                ImGui::TableNextColumn();
                if (e.unitKind == 6) ImGui::Text("%s", e.note.empty() ? "-" : e.note.c_str());
                else {
                    char vb[160];
                    std::snprintf(vb, sizeof(vb), "%.6g %s",
                                  unitToDisplay(e.valueSI, resultUnitSize(e.unitKind, u)),
                                  resultUnitName(e.unitKind, pr.units));
                    ImGui::TextUnformatted(vb);
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", (e.unitKind == 6) ? "" : e.note.c_str());
            }
            ImGui::EndTable();
        }
    }
    else if (v.resultSel == CAE_RES_CONV) {
        ImGui::TextUnformatted(_TR("Mesh convergence", "Сходимость по сетке"));
        ImGui::TextWrapped("%s", _TR("The program does not claim accuracy it has not verified: run the same case at "
                                     "three resolutions and look at the change of the key value. Automatic error "
                                     "estimation (Richardson) is not implemented.",
                                     "Программа не заявляет точность, которую не проверила: выполните один и тот же "
                                     "случай на трёх разрешениях и посмотрите на изменение ключевой величины. "
                                     "Автоматическая оценка погрешности (Ричардсон) не реализована."));
        if (ImGui::Button(_TR("Run at 3 resolutions (0.5x, 1x, 1.5x)", "Прогнать на 3 разрешениях (0.5x, 1x, 1.5x)"),
                          ImVec2(-1, 0)))
            caeRunMeshConvergence();
        if (v.convergence.empty()) ImGui::TextDisabled("%s", _TR("no data yet", "данных пока нет"));
        else if (ImGui::BeginTable("##caeConv", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn(_TR("res", "разр"));
            ImGui::TableSetupColumn(_TR("elements", "элементов"));
            ImGui::TableSetupColumn(_TR("nodes", "узлов"));
            ImGui::TableSetupColumn(_TR("iters", "итераций"));
            ImGui::TableSetupColumn(_TR("residual", "невязка"));
            ImGui::TableSetupColumn(_TR("time, ms", "время, мс"));
            ImGui::TableSetupColumn(pr.analysis == StudyAnalysis::LinearStatic
                                        ? _TR("max |u|", "макс |u|") : _TR("max T", "макс T"));
            ImGui::TableHeadersRow();
            for (const CaeViewState::MeshPoint& p : v.convergence) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%d", p.resolution);
                ImGui::TableNextColumn(); ImGui::Text("%d", p.elements);
                ImGui::TableNextColumn(); ImGui::Text("%d", p.nodes);
                ImGui::TableNextColumn(); ImGui::Text("%d", p.iterations);
                ImGui::TableNextColumn(); ImGui::Text("%.2e", p.residual);
                ImGui::TableNextColumn(); ImGui::Text("%.1f", p.timeMs);
                ImGui::TableNextColumn();
                ImGui::Text("%.6g %s", unitToDisplay(p.maxValue, pr.analysis == StudyAnalysis::LinearStatic ? u.length : u.temp),
                            pr.analysis == StudyAnalysis::LinearStatic ? unitNameLength(pr.units) : unitNameTemp(pr.units));
            }
            ImGui::EndTable();
        }
    }
    else if (v.resultSel == CAE_RES_EXPORT) {
        ImGui::TextUnformatted(_TR("Export", "Экспорт"));
        ImGui::TextWrapped("%s", _TR("CSV contains node coordinates and field values in SI (the unit system is "
                                     "recorded in the header). The report contains the setup, the mesh, the solver "
                                     "settings, the results and the warnings.",
                                     "CSV содержит координаты узлов и значения полей в СИ (система единиц записана в "
                                     "заголовке). Отчёт содержит постановку, сетку, настройки решателя, результаты и "
                                     "предупреждения."));
        if (ImGui::Button(_TR("Export results to CSV…", "Экспорт результатов в CSV…"), ImVec2(-1, 0))) caeExportCSV();
        if (ImGui::Button(_TR("Export report (TXT)…", "Экспорт отчёта (TXT)…"), ImVec2(-1, 0))) caeExportReport();
        ImGui::Separator();
        ImGui::TextUnformatted(_TR("Study project file", "Файл проекта исследования"));
        ImGui::TextWrapped("%s: %s", _TR("Path", "Путь"),
                           pr.projectPath.empty() ? _TR("(not saved)", "(не сохранён)") : pr.projectPath.c_str());
        if (ImGui::Button(_TR("Save study…", "Сохранить исследование…"), ImVec2(-1, 0))) caeSaveStudy();
        if (ImGui::Button(_TR("Load study…", "Загрузить исследование…"), ImVec2(-1, 0))) caeLoadStudy();
        ImGui::Separator();
        ImGui::TextUnformatted(_TR("Report preview", "Предпросмотр отчёта"));
        const std::string rep = studyBuildReport(pr, m);
        ImGui::BeginChild("##caeReport", ImVec2(-1, 220), true);
        ImGui::TextUnformatted(rep.c_str());
        ImGui::EndChild();
    }
}

// -----------------------------------------------------
// Нижние вкладки оболочки (кроме «Сообщения», которую рисует ui.cpp из общего
// журнала — чтобы не дублировать лог).
// -----------------------------------------------------
inline void caeDrawBottomConvergence() {
    StudyProject& pr = studyProject();
    CaeViewState& v = caeView();
    ImGui::TextDisabled("%s", _TR("FEM solver convergence (CG, relative residual)",
                                  "Сходимость решателя МКЭ (CG, относительная невязка)"));
    char b[256];
    std::snprintf(b, sizeof(b), "%s: %s | %s: %d | %s: %.3e | %s: %.1f %s | %s: %d",
                  _TR("status", "статус"), studyStatusName(pr.results.status),
                  _TR("iterations", "итераций"), pr.results.iterations,
                  _TR("residual", "невязка"), pr.results.finalResidual,
                  _TR("time", "время"), pr.results.solveTimeMs, _TR("ms", "мс"),
                  _TR("DOFs", "степеней свободы"), pr.results.dofs);
    ImGui::TextUnformatted(b);
    std::snprintf(b, sizeof(b), "%s: %d | %s: %.2e | %s: %.2e | %s: %.1e",
                  _TR("tolerance", "допуск"), pr.solver.maxIterations,
                  _TR("criterion", "критерий"), pr.solver.relTolerance,
                  _TR("initial residual", "начальная невязка"), pr.results.initialResidual,
                  _TR("final residual", "итоговая невязка"), pr.results.finalResidual);
    ImGui::TextUnformatted(b);
    const bool conv = pr.results.finalResidual <= pr.solver.relTolerance;
    ImGui::TextColored(conv ? ImVec4(0.45f, 0.9f, 0.45f, 1.0f) : ImVec4(1.0f, 0.7f, 0.25f, 1.0f), "%s",
                       conv ? _TR("convergence criterion is met", "критерий сходимости выполнен")
                            : _TR("convergence criterion NOT met — result is not trustworthy",
                                  "критерий сходимости НЕ выполнен — результату нельзя доверять"));
    if (!pr.results.residualHistory.empty()) {
        std::vector<float> lg;
        lg.reserve(pr.results.residualHistory.size());
        for (double rr : pr.results.residualHistory)
            lg.push_back((float)((rr > 1e-16) ? std::log10(rr) : -16.0));
        ImGui::PlotLines("log10 ||r||/||f||", lg.data(), (int)lg.size(), 0, nullptr,
                         FLT_MAX, FLT_MAX, ImVec2(-1, 64));
    } else {
        ImGui::TextDisabled("%s", _TR("no residual history — run the solver", "нет истории невязки — выполните расчёт"));
    }
    if (!v.convergence.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", _TR("Mesh study", "Исследование по сеткам"));
        for (const CaeViewState::MeshPoint& p : v.convergence) {
            std::snprintf(b, sizeof(b), "разр %2d: элементов %6d, узлов %6d, итераций %4d, невязка %.2e, %.1f мс, ключевая величина %.6g",
                          p.resolution, p.elements, p.nodes, p.iterations, p.residual, p.timeMs, p.maxValue);
            ImGui::TextUnformatted(b);
        }
    }
    (void)v;
}

inline void caeDrawBottomPlots() {
    CaeViewState& v = caeView();
    StudyProject& pr = studyProject();
    const StudyUnits u = pr.unitsSI();
    size_t cnt = 0;
    const ResultFieldInfo* tab = resultFieldTable(&cnt);
    const ResultFieldInfo* fi = nullptr;
    for (size_t i = 0; i < cnt; ++i) if (tab[i].id == v.field) fi = &tab[i];
    if (!fi) fi = &tab[0];
    const double usize = resultUnitSize(fi->unitKind, u);
    char b[192];
    std::snprintf(b, sizeof(b), "%s, %s", fi->name, resultUnitName(fi->unitKind, pr.units));
    if (v.line.ok) {
        std::vector<float> ys(v.line.v.size());
        for (size_t i = 0; i < v.line.v.size(); ++i) ys[i] = (float)unitToDisplay(v.line.v[i], usize);
        ImGui::PlotLines(b, ys.data(), (int)ys.size(), 0, nullptr, FLT_MAX, FLT_MAX, ImVec2(-1, 80));
        ImGui::TextDisabled("%s", _TR("distance along the line, m", "координата вдоль линии, м"));
    } else {
        ImGui::TextDisabled("%s", _TR("no line plot — build one in the Results workspace",
                                      "нет графика — постройте его в пространстве «Результаты»"));
    }
    ImGui::Separator();
    if (v.slice.ok) {
        std::snprintf(b, sizeof(b), "%s: %c = %.6g м, %d x %d, %.6g .. %.6g %s",
                      _TR("slice", "срез"), "XYZ"[v.slice.axis], v.slice.coord, v.slice.nu, v.slice.nv,
                      unitToDisplay(v.slice.vmin, usize), unitToDisplay(v.slice.vmax, usize),
                      resultUnitName(fi->unitKind, pr.units));
        ImGui::TextUnformatted(b);
        // профиль через середину среза
        const int mid = v.slice.nv / 2;
        std::vector<float> prof((size_t)v.slice.nu, 0.0f);
        for (int i = 0; i < v.slice.nu; ++i)
            prof[(size_t)i] = (float)unitToDisplay(v.slice.values[(size_t)mid * v.slice.nu + i], usize);
        ImGui::PlotLines(_TR("profile through the slice center", "профиль через середину среза"),
                         prof.data(), (int)prof.size(), 0, nullptr, FLT_MAX, FLT_MAX, ImVec2(-1, 60));
    } else {
        ImGui::TextDisabled("%s", _TR("no slice", "среза нет"));
    }
}

inline void caeDrawBottomResults() {
    StudyProject& pr = studyProject();
    caeDrawResultsSummary(pr, pr.unitsSI());
}

inline void caeDrawBottomMessages() {
    const std::vector<CaeMessage>& msgs = caeMessages();
    ImGui::TextDisabled("%s", _TR("Study messages: separate lines for mesh, boundary conditions and convergence",
                                  "Сообщения исследования: отдельно по сетке, граничным условиям и сходимости"));
    static int filterLevel = -1;   // -1 = все, 0 инфо, 1 предупреждения, 2 ошибки
    const char* fItems[] = { _TR("all", "все"), _TR("info", "инфо"),
                             _TR("warnings", "предупреждения"), _TR("errors", "ошибки") };
    ImGui::SetNextItemWidth(180);
    ImGui::Combo(_TR("Filter", "Фильтр"), &filterLevel, fItems, 4);
    ImGui::SameLine();
    if (ImGui::Button(_TR("Clear", "Очистить"))) caeClearMessages();
    ImGui::SameLine();
    ImGui::TextDisabled("%s: %zu", _TR("Records", "Записей"), msgs.size());
    ImGui::BeginChild("##caeMsgs", ImVec2(-1, -1), false);
    const int want = filterLevel - 1;
    for (long long i = (long long)msgs.size() - 1; i >= 0; --i) {
        const CaeMessage& m = msgs[(size_t)i];
        if (want >= 0 && m.level != want) continue;
        ImVec4 col(0.80f, 0.84f, 0.88f, 1.0f);
        const char* tag = _TR("INFO", "ИНФО");
        if (m.level == 1) { col = ImVec4(1.0f, 0.80f, 0.28f, 1.0f); tag = _TR("WARN", "ПРЕДУПР"); }
        if (m.level == 2) { col = ImVec4(1.0f, 0.40f, 0.34f, 1.0f); tag = _TR("ERROR", "ОШИБКА"); }
        ImGui::TextColored(col, "[%s][%s] %s", tag, m.category.c_str(), m.text.c_str());
    }
    if (msgs.empty())
        ImGui::TextDisabled("%s", _TR("no messages yet — build the mesh and run the solver",
                                      "сообщений пока нет — постройте сетку и выполните расчёт"));
    ImGui::EndChild();
}

// -----------------------------------------------------
// Главное меню оболочки. Все пункты — реальные действия; неработающих режимов
// в меню нет (модальный/нестационарный анализы появятся, когда будут готовы).
// -----------------------------------------------------
inline void caeDrawMainMenu(UiLayoutState& st) {
    StudyProject& pr = studyProject();
    CaeViewState& v = caeView();

    if (ImGui::BeginMenuBar()) {
        // ---------- ФАЙЛ ----------
        if (ImGui::BeginMenu(_TR("File", "Файл"))) {
            if (ImGui::MenuItem(_TR("Open model (STL)…", "Открыть модель (STL)…"), nullptr)) {
                std::string p = openFileDialog();
                if (!p.empty()) {
                    if (loadModel(p)) {
                        caeLog(0, "геометрия", "модель загружена: %s (вершин %d)", p.c_str(), modelVertexCount);
                        caeSyncGeometry();
                        st.workspace = 0;
                    } else {
                        caeLog(2, "геометрия", "не удалось загрузить модель: %s", p.c_str());
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(_TR("Save study…", "Сохранить исследование…"), nullptr)) caeSaveStudy();
            if (ImGui::MenuItem(_TR("Load study…", "Загрузить исследование…"), nullptr)) caeLoadStudy();
            ImGui::Separator();
            const bool haveRes = pr.results.valid;
            if (ImGui::MenuItem(_TR("Export results to CSV…", "Экспорт результатов в CSV…"), nullptr, false, haveRes))
                caeExportCSV();
            if (ImGui::MenuItem(_TR("Export report (TXT)…", "Экспорт отчёта (TXT)…"), nullptr)) caeExportReport();
            ImGui::Separator();
            if (ImGui::MenuItem(_TR("Exit", "Выход"), nullptr)) {
                caeLog(0, "интерфейс", "выход из программы по команде меню");
                aeroExitRequested = true;
            }
            ImGui::EndMenu();
        }
        // ---------- ВИД ----------
        if (ImGui::BeginMenu(_TR("View", "Вид"))) {
            for (int i = 0; i < UI_WORKSPACE_COUNT; ++i) {
                if (ImGui::MenuItem(uiWorkspaceName(i), (i == 0) ? "Ctrl+1" : (i == 1 ? "Ctrl+2" : "Ctrl+3"),
                                    st.workspace == i)) {
                    st.workspace = i;
                    caeLog(0, "интерфейс", "рабочее пространство: %s", uiWorkspaceName(i));
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(_TR("Main menu", "Главное меню"), nullptr, st.menuBar)) st.menuBar = !st.menuBar;
            if (ImGui::MenuItem(_TR("Project tree (left)", "Дерево проекта (слева)"), nullptr, st.treePanel))
                st.treePanel = !st.treePanel;
            if (ImGui::MenuItem(_TR("Properties (right)", "Свойства (справа)"), nullptr, st.propsPanel))
                st.propsPanel = !st.propsPanel;
            if (ImGui::MenuItem(_TR("Bottom tabs", "Нижние вкладки"), nullptr, st.bottomPanel))
                st.bottomPanel = !st.bottomPanel;
            if (ImGui::MenuItem(_TR("Hide all panels", "Скрыть все панели"), "F12", !st.panelsVisible)) {
                st.panelsVisible = !st.panelsVisible;
                caeLog(0, "интерфейс", "панели: %s", st.panelsVisible ? "показаны" : "скрыты");
            }
            if (ImGui::MenuItem(_TR("Maximize viewport", "Вид на всё окно"), "Ctrl+M", st.maximized)) {
                st.maximized = !st.maximized;
                caeLog(0, "интерфейс", "3D-вид: %s", st.maximized ? "на всё окно" : "в доке");
            }
            ImGui::Separator();
            if (ImGui::BeginMenu(_TR("Font scale", "Масштаб шрифта"))) {
                for (float s : { 0.8f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f }) {
                    char b[32]; std::snprintf(b, sizeof(b), "%.2fx", s);
                    if (ImGui::MenuItem(b, nullptr, std::fabs(st.fontScale - s) < 0.01f)) {
                        st.fontScale = s;
                        uiScale = s;   // общий масштаб интерфейса движка
                        caeLog(0, "интерфейс", "масштаб шрифта панелей: %.2fx", s);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        // ---------- ИССЛЕДОВАНИЕ ----------
        if (ImGui::BeginMenu(_TR("Study", "Исследование"))) {
            if (ImGui::BeginMenu(_TR("Analysis type", "Тип анализа"))) {
                if (ImGui::MenuItem(studyAnalysisName(StudyAnalysis::LinearStatic), nullptr,
                                    pr.analysis == StudyAnalysis::LinearStatic)) {
                    pr.analysis = StudyAnalysis::LinearStatic;
                    pr.results.clear(); pr.status = StudySolveStatus::NotStarted;
                    caeLog(0, "анализ", "выбран линейный статический расчёт");
                }
                if (ImGui::MenuItem(studyAnalysisName(StudyAnalysis::SteadyThermal), nullptr,
                                    pr.analysis == StudyAnalysis::SteadyThermal)) {
                    pr.analysis = StudyAnalysis::SteadyThermal;
                    pr.results.clear(); pr.status = StudySolveStatus::NotStarted;
                    caeLog(0, "анализ", "выбрана стационарная теплопередача");
                }
                ImGui::Separator();
                ImGui::TextDisabled("%s", _TR("modal / transient — not implemented yet",
                                              "модальный / нестационарный — не реализованы"));
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(_TR("Sync geometry from model", "Синхронизировать геометрию из модели"), nullptr))
                caeSyncGeometry();
            if (ImGui::MenuItem(_TR("Build volume mesh", "Построить объёмную сетку"), nullptr)) {
                st.workspace = 1; v.studySel = CAE_STUDY_MESH;
                caeBuildMesh();
            }
            if (ImGui::MenuItem(_TR("Solve", "Рассчитать"), "Ctrl+Enter", nullptr)) {
                st.workspace = 1;
                caeSolve(true);
            }
            if (ImGui::MenuItem(_TR("Clear results", "Очистить результаты"), nullptr, pr.results.valid)) {
                pr.results.clear();
                pr.status = StudySolveStatus::NotStarted;
                caeColorOverride = false;
                caeLog(0, "результаты", "результаты очищены");
            }
            ImGui::Separator();
            if (ImGui::BeginMenu(_TR("Unit system", "Система единиц"))) {
                if (ImGui::MenuItem(unitSystemName(StudyUnitSystem::SI), nullptr, pr.units == StudyUnitSystem::SI)) {
                    pr.units = StudyUnitSystem::SI;
                    caeLog(0, "единицы", "единицы интерфейса: СИ");
                }
                if (ImGui::MenuItem(unitSystemName(StudyUnitSystem::mmMPa), nullptr, pr.units == StudyUnitSystem::mmMPa)) {
                    pr.units = StudyUnitSystem::mmMPa;
                    caeLog(0, "единицы", "единицы интерфейса: мм–МПа–т–мВт");
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        // ---------- РЕЗУЛЬТАТЫ ----------
        if (ImGui::BeginMenu(_TR("Results", "Результаты"))) {
            const bool haveRes = pr.results.valid;
            if (ImGui::BeginMenu(_TR("Field", "Поле"), haveRes)) {
                size_t cnt = 0;
                const ResultFieldInfo* tab = resultFieldTable(&cnt);
                for (size_t i = 0; i < cnt; ++i) {
                    if (!resultFieldAvailable(pr.results, tab[i].id)) continue;
                    if (ImGui::MenuItem(tab[i].name, nullptr, v.field == tab[i].id)) {
                        v.field = tab[i].id;
                        st.workspace = 2;
                        caeApplyFieldToGeometry();
                        caeLog(0, "отображение", "поле: %s", tab[i].name);
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(_TR("Color model by field", "Раскрасить модель по полю"), nullptr, v.colorGeometry, haveRes)) {
                v.colorGeometry = !v.colorGeometry;
                caeApplyFieldToGeometry();
            }
            if (ImGui::MenuItem(_TR("Auto scale range", "Автоматический диапазон"), nullptr, v.scale.autoRange, haveRes))
                v.scale.autoRange = !v.scale.autoRange;
            ImGui::Separator();
            if (ImGui::MenuItem(_TR("Point probe", "Проба в точке"), nullptr, v.probeEnabled, haveRes)) {
                v.probeEnabled = !v.probeEnabled; st.workspace = 2; v.resultSel = CAE_RES_PROBE;
            }
            if (ImGui::MenuItem(_TR("Slices", "Срезы"), nullptr, v.sliceEnabled, haveRes)) {
                st.workspace = 2; v.resultSel = CAE_RES_SLICE;
            }
            if (ImGui::MenuItem(_TR("Plots", "Графики"), nullptr, false, haveRes)) {
                st.workspace = 2; v.resultSel = CAE_RES_PLOT;
            }
            if (ImGui::MenuItem(_TR("Key values table", "Таблица величин"), nullptr, false, haveRes)) {
                st.workspace = 2; v.resultSel = CAE_RES_TABLE; st.bottomTab = 3;
            }
            if (ImGui::MenuItem(_TR("Mesh convergence study", "Исследование сходимости по сетке"), nullptr, false, haveRes)) {
                st.workspace = 2; v.resultSel = CAE_RES_CONV;
                caeRunMeshConvergence();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(_TR("Export results to CSV…", "Экспорт результатов в CSV…"), nullptr, false, haveRes))
                caeExportCSV();
            if (ImGui::MenuItem(_TR("Export report (TXT)…", "Экспорт отчёта (TXT)…"), nullptr)) caeExportReport();
            ImGui::EndMenu();
        }
        // ---------- ОТЛАДКА (существующие функции движка) ----------
        if (ImGui::BeginMenu(_TR("Debug", "Отладка"))) {
            if (ImGui::MenuItem(_TR("Journal (F9)", "Журнал (F9)"), nullptr, debugLogEnabled())) {
                const bool on = !debugLogEnabled();
                debugLogSetEnabled(on);
                debugLogSetPhysics(on);
                debugLogSetOverlay(on);
            }
            if (ImGui::MenuItem(_TR("Debug report (F10)", "Отладочный отчёт (F10)"), nullptr)) {
                g_lastDebugReportPath = debugLogSaveReport();
                caeLog(0, "отладка", "отладочный отчёт сохранён: %s",
                       g_lastDebugReportPath.empty() ? "(ошибка записи)" : g_lastDebugReportPath.c_str());
            }
            if (ImGui::MenuItem(_TR("Recorder (F7)", "Самописец (F7)"), nullptr, tlEnabled)) {
                tlEnabled = !tlEnabled;
                if (tlEnabled) { tlPlaying = false; timelapseClear(); tlShowWindow = true; }
            }
            if (ImGui::MenuItem(_TR("Playback (F8)", "Просмотр (F8)"), nullptr, tlPlaying)) {
                if (timelapseFrameCount() > 0) { tlPlaying = !tlPlaying; tlShowWindow = true; }
                else caeLog(1, "самописец", "записи ещё нет — сначала нажмите F7");
            }
            ImGui::EndMenu();
        }
        // ---------- СПРАВКА ----------
        if (ImGui::BeginMenu(_TR("Help", "Справка"))) {
            if (ImGui::MenuItem(_TR("About Aeros Engine", "О программе Aeros Engine"), nullptr))
                ImGui::OpenPopup("AerosAbout");
            if (ImGui::MenuItem(_TR("Method and limitations", "Методика и ограничения"), nullptr))
                ImGui::OpenPopup("AerosLimits");
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // ---------- диалоги (всплывать могут только меню и диалоги) ----------
    if (ImGui::BeginPopupModal("AerosAbout", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        char b[256];
        ImGui::TextUnformatted("Aeros Engine");
        std::snprintf(b, sizeof(b), "%s %s", _TR("version", "версия"), AEROS_VERSION_STRING);
        ImGui::TextUnformatted(b);
        ImGui::TextWrapped("%s", _TR("AoS CAD/CAE environment: geometry editor, CFD (LBM), FEM structural and "
                                     "thermal analyses, results post-processing.",
                                     "Среда AoS CAD/CAE: редактор геометрии, CFD (LBM), прочностной и тепловой анализ "
                                     "МКЭ, постобработка результатов."));
        if (ImGui::Button(_TR("Close", "Закрыть"), ImVec2(-1, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("AerosLimits", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::BeginChild("##limits", ImVec2(620, 320), true);
        ImGui::TextUnformatted(_TR("Implemented analyses", "Реализованные анализы"));
        ImGui::BulletText("%s", _TR("Linear static structural: isotropic linear elasticity (E, nu, rho), fixed / "
                                    "displacement / force / pressure / gravity conditions; displacements, stresses, "
                                    "von Mises, reactions, strain energy.",
                                    "Линейный статический прочностной: изотропная линейная упругость (E, ν, ρ), "
                                    "закрепления / перемещения / силы / давление / гравитация; перемещения, "
                                    "напряжения, фон Мизес, реакции, энергия деформации."));
        ImGui::BulletText("%s", _TR("Steady-state thermal: conduction (k), prescribed temperature, heat flux, "
                                    "volumetric generation, lumped convection; temperature field and heat flux.",
                                    "Стационарная теплопередача: теплопроводность (k), заданная температура, тепловой "
                                    "поток, объёмное тепловыделение, сосредоточенная конвекция; поле температур и "
                                    "тепловой поток."));
        ImGui::TextUnformatted(_TR("NOT implemented (do not appear as working modes)", "НЕ реализовано (не выдаётся за работающее)"));
        ImGui::BulletText("%s", _TR("Modal, transient, nonlinear, contact, anisotropy, shells/beams, "
                                    "automatic error estimation.",
                                    "Модальный, нестационарный, нелинейный, контактный анализ, анизотропия, "
                                    "оболочки/балки, автоматическая оценка погрешности."));
        ImGui::TextUnformatted(_TR("Method limitations", "Ограничения методики"));
        ImGui::BulletText("%s", _TR("Mesh: hexahedral Q1 over the model voxels; geometry is staircase-approximated "
                                    "with the cell size.",
                                    "Сетка: гексаэдры Q1 по вокселям модели; геометрия ступенчатая с шагом ячейки."));
        ImGui::BulletText("%s", _TR("Q1 elements lock in bending — refine the mesh for bending-dominated cases.",
                                    "Элементы Q1 завышают жёсткость при изгибе — измельчайте сетку."));
        ImGui::BulletText("%s", _TR("Solver: conjugate gradients with Jacobi preconditioning, matrix-free; "
                                    "iteration count grows with mesh size.",
                                    "Решатель: сопряжённые градиенты с диагональным предусловливанием, без хранения "
                                    "матрицы; число итераций растёт с размером сетки."));
        ImGui::BulletText("%s", _TR("The solve is synchronous: the interface freezes while it runs.",
                                    "Расчёт синхронный: интерфейс замирает на время решения."));
        ImGui::BulletText("%s", _TR("Accuracy is not claimed: the program shows the residual, the iteration count "
                                    "and warnings; mesh convergence is checked by the user.",
                                    "Точность не заявляется: программа показывает невязку, число итераций и "
                                    "предупреждения; сходимость по сетке проверяет пользователь."));
        ImGui::EndChild();
        if (ImGui::Button(_TR("Close", "Закрыть"), ImVec2(-1, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// -----------------------------------------------------
// Клавиатурные команды оболочки (вызывается из ui.cpp один раз за кадр).
// Esc намеренно НЕ используется: в движке он закрывает окно (input.cpp).
// -----------------------------------------------------
inline void caeHandleShortcuts(UiLayoutState& st) {
    ImGuiIO& io = ImGui::GetIO();
    const bool ctrl = io.KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_M, false)) {
        st.maximized = !st.maximized;
        caeLog(0, "интерфейс", "Ctrl+M: 3D-вид %s", st.maximized ? "на всё окно" : "в доке");
    }
    if (ctrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) st.workspace = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_2, false)) st.workspace = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_3, false)) st.workspace = 2;
    }
    // F12 намеренно не обрабатывается здесь: его перехватывает input.cpp на
    // уровне GLFW (там же, где и остальные клавиши движка). Он переключает
    // uiLayoutState().panelsVisible — то же самое состояние, что и меню.
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        caeSolve(true);
    }
    // Здесь НЕЛЬЗЯ принудительно возвращать panelsVisible = true при максимизации.
    // uiLayoutCompute() и так скрывает доки: showDocks = panelsVisible && !maximized.
    // Прежняя строка «максимизация не прячет доки навсегда» откатывала выбор
    // пользователя: в полноэкранном виде пункт «Скрыть все панели» (F12) и кнопка
    // «Панели (F12)» становились inert — нажатие тут же отменялось, а в настройки
    // записывалось «панели показаны» вопреки действию пользователя.
}

// -----------------------------------------------------
// Синхронизация нового состояния компоновки с прежними глобальными ключами
// (uiShowTree/uiTreeW/... из v1.28) — чтобы старые настройки не потерялись и
// чтобы остальные части движка видели согласованные значения.
// -----------------------------------------------------
inline void caeSyncLegacyLayout(const UiLayoutState& st) {
    uiDocked      = st.panelsVisible;
    uiShowTree    = st.panelsVisible && st.treePanel;
    uiShowProps   = st.panelsVisible && st.propsPanel;
    uiShowConsole = st.panelsVisible && st.bottomPanel;
    uiTreeW       = (int)st.treeW;
    uiPropsW      = (int)st.propsW;
    uiBottomH     = (int)st.bottomH;
    if (st.fontScale > 0.5f) uiScale = st.fontScale;
}

// Строка состояния оболочки: кроме аэродинамики показывает состояние исследования.
inline void caeDrawStatusBarCae(int workspace) {
    StudyProject& pr = studyProject();
    const StudyUnits u = pr.unitsSI();
    ImGui::Separator();
    char b[384];
    std::snprintf(b, sizeof(b), "%s: %s | %s: %s | %s: %s | %s: %s | %s: %s",
                  _TR("workspace", "пространство"), uiWorkspaceName(workspace),
                  _TR("study", "исследование"), pr.name.c_str(),
                  _TR("analysis", "анализ"), studyAnalysisName(pr.analysis),
                  _TR("units", "единицы"), unitSystemName(pr.units),
                  _TR("solve status", "статус расчёта"), studyStatusName(pr.results.status));
    ImGui::TextUnformatted(b);
    if (pr.results.valid) {
        if (pr.analysis == StudyAnalysis::LinearStatic)
            std::snprintf(b, sizeof(b), "|u|max %.4g %s | σ_vm max %.4g %s | %s %d | невязка %.2e | %.0f мс | элементов %d",
                          unitToDisplay(pr.results.maxDisplacement, u.length), unitNameLength(pr.units),
                          unitToDisplay(pr.results.maxVonMises, u.stress), unitNameStress(pr.units),
                          _TR("iters", "итер"), pr.results.iterations, pr.results.finalResidual,
                          pr.results.solveTimeMs, pr.results.elements);
        else
            std::snprintf(b, sizeof(b), "T %.4g..%.4g %s | q max %.4g %s | %s %d | невязка %.2e | %.0f мс | элементов %d",
                          unitToDisplay(pr.results.minTemperature, u.temp),
                          unitToDisplay(pr.results.maxTemperature, u.temp), unitNameTemp(pr.units),
                          unitToDisplay(pr.results.maxHeatFlux, u.heatFlux), unitNameHeatFlux(pr.units),
                          _TR("iters", "итер"), pr.results.iterations, pr.results.finalResidual,
                          pr.results.solveTimeMs, pr.results.elements);
        ImGui::SameLine();
        ImGui::TextUnformatted(b);
    }
    const FemMesh& m = caeMesh();
    if (m.built) {
        std::snprintf(b, sizeof(b), "| %s %dx%dx%d (%d эл., %d узл.)",
                      _TR("FEM mesh", "сетка МКЭ"), m.ex, m.ey, m.ez, m.elemCount, m.nodeCount);
        ImGui::SameLine();
        ImGui::TextUnformatted(b);
    }
}

#endif // AEROS_CAE_UI_H
