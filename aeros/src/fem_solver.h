#ifndef AEROS_FEM_SOLVER_H
#define AEROS_FEM_SOLVER_H
// =====================================================
// Aeros CAE — решатели: линейная статика и стационарная теплопередача
//
// Метод конечных элементов на структурированной гексаэдральной сетке (Q1) из
// fem_mesh.h. Всё в СИ.
//
// ЧТО РЕАЛЬНО СЧИТАЕТСЯ
//   Линейная статика:  K u = f,
//     K — матрица жёсткости изотропной упругости, элементы Q1, интегрирование
//         по 2x2x2 точкам Гаусса (полное, не редуцированное);
//     f — силы: заданные перемещения/защемления (Дирихле), суммарная сила на
//         область (распределяется поровну на её узлы), давление на внешние грани
//         (согласованные узловые силы p*n*A/4), сила тяжести (rho*g*V/8 на узел);
//     результаты: перемещения, напряжения в узлах (осреднение по элементам),
//         эквивалентное напряжение фон Мизеса, реакции в закреплениях,
//         энергия деформации.
//   Стационарная теплопередача:  K_T T = q,
//     K_T — матрица теплопроводности (k*grad N * grad N),
//     q — заданные температуры (Дирихле), тепловой поток через грани (q*A/4),
//         объёмное тепловыделение (Q*V/8), конвекция (h, T_окр) — Robin-условие
//         с сосредоточенной (lumped) граничной матрицей;
//     результаты: поле температуры, тепловой поток q = -k*grad T, суммарный
//         поток через границы.
//
// КАК РЕШАЕТСЯ: метод сопряжённых градиентов с диагональным (Jacobi)
// предусловливанием. Матрица НЕ хранится: умножение K*u выполняется по
// элементам «на лету». Это возможно и дёшево потому, что сетка структурированная
// и ячейки одинакового размера — элементарная матрица одна и та же для всех
// элементов одного материала (считается один раз). Раскладка по 8 цветовым
// группам (чётность i,j,k) позволяет распараллелить сборку без гонок: элементы
// одной группы не имеют общих узлов.
//
// ТОЧНОСТЬ И ЧЕСТНОСТЬ. Программа не заявляет точность, которую не проверяет:
//   - невязка ||r||/||f||, число итераций и история остатков сохраняются и
//     показываются во вкладке «Сходимость»;
//   - если за maxIterations сходимость не достигнута, статус = «не сошёлся»,
//     а не «завершён»;
//   - сингулярность (тело не закреплено, плавающая несвязная часть) ловится до
//     решения и сообщается как ошибка постановки;
//   - оценка сходимости по сетке: сравнение на нескольких разрешениях есть в
//     headless-тесте, в интерфейсе — кнопка «Сравнить 3 разрешения».
// =====================================================

#include "study.h"
#include "fem_mesh.h"

#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>

#ifdef _OPENMP
#include <omp.h>
#endif

// -----------------------------------------------------
// Элементные матрицы. 24 DOF на элемент для упругости (3 компоненты на узел),
// 8 DOF для теплопроводности (скаляр на узел).
// -----------------------------------------------------
struct FemElemMat {
    double Ke[24*24];
    double Kt[8*8];
    double diagE[24];
    double diagT[8];
    double volume;
    bool   elasticReady = false;
    bool   thermalReady = false;
};

// Локальные координаты узлов Q1 в единичном кубе [-1,1]^3 — порядок совпадает с
// FemMesh::elemNodes().
inline void femLocalCoords(int a, double& xi, double& et, double& ze) {
    static const double t[8][3] = {
        {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
        {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
    };
    xi = t[a][0]; et = t[a][1]; ze = t[a][2];
}

// Производные функций форм в точке Гаусса (в локальных координатах).
inline void femShapeGrad(int a, double xi, double et, double ze,
                         double& dN_dxi, double& dN_det, double& dN_dze) {
    double xa, ea, za;
    femLocalCoords(a, xa, ea, za);
    dN_dxi = 0.125 * xa * (1.0 + ea * et) * (1.0 + za * ze);
    dN_det = 0.125 * (1.0 + xa * xi) * ea * (1.0 + za * ze);
    dN_dze = 0.125 * (1.0 + xa * xi) * (1.0 + ea * et) * za;
}

// Матрица упругости D (форма Вольтга, инженерные касательные).
inline void femElasticD(double E, double nu, double D[6][6]) {
    const double c = E / ((1.0 + nu) * (1.0 - 2.0 * nu));
    for (int i = 0; i < 6; ++i) for (int j = 0; j < 6; ++j) D[i][j] = 0.0;
    const double s = (1.0 - 2.0 * nu) * 0.5;
    D[0][0] = D[1][1] = D[2][2] = c * (1.0 - nu);
    D[0][1] = D[0][2] = D[1][0] = D[1][2] = D[2][0] = D[2][1] = c * nu;
    D[3][3] = D[4][4] = D[5][5] = c * s;
}

// Сборка элементных матриц для ячейки hx*hy*hz. Возвращает false, если материал
// некорректен (E <= 0, nu вне (-1; 0.5), k <= 0).
inline bool femBuildElemMatrices(FemElemMat& em, double hx, double hy, double hz,
                                 double E, double nu, double kCond,
                                 bool needElastic, bool needThermal) {
    em = FemElemMat();   // value-инициализация: массивы зануляются без memset
                         // (memset на таком типе даёт предупреждение -Wclass-memaccess)
    if (!(hx > 0) || !(hy > 0) || !(hz > 0)) return false;
    em.volume = hx * hy * hz;
    const double detJ = em.volume / 8.0;   // |J| для точки Гаусса (w = 1)
    // якобиан отображения: d(xi)/dx = 2/hx и т.д.
    const double i2x = 2.0 / hx, i2y = 2.0 / hy, i2z = 2.0 / hz;

    if (needElastic) {
        if (!(E > 0) || !(nu > -1.0) || !(nu < 0.5)) return false;
        double D[6][6];
        femElasticD(E, nu, D);
        double B[6][24];
        for (int g = 0; g < 8; ++g) {
            // точки Гаусса 2x2x2: +-1/sqrt(3)
            const double s = 1.0 / std::sqrt(3.0);
            const double xi = ((g & 1) ? s : -s);
            const double et = ((g & 2) ? s : -s);
            const double ze = ((g & 4) ? s : -s);
            for (int r = 0; r < 6; ++r) for (int c = 0; c < 24; ++c) B[r][c] = 0.0;
            for (int a = 0; a < 8; ++a) {
                double dxi, det, dze;
                femShapeGrad(a, xi, et, ze, dxi, det, dze);
                const double dx = dxi * i2x, dy = det * i2y, dz = dze * i2z;
                B[0][3*a+0] = dx;
                B[1][3*a+1] = dy;
                B[2][3*a+2] = dz;
                B[3][3*a+0] = dy; B[3][3*a+1] = dx;
                B[4][3*a+0] = dz; B[4][3*a+2] = dx;
                B[5][3*a+1] = dz; B[5][3*a+2] = dy;
            }
            // Ke += B^T D B * detJ
            double DB[6][24];
            for (int r = 0; r < 6; ++r)
                for (int c = 0; c < 24; ++c) {
                    double s2 = 0.0;
                    for (int m = 0; m < 6; ++m) s2 += D[r][m] * B[m][c];
                    DB[r][c] = s2;
                }
            for (int r = 0; r < 24; ++r)
                for (int c = 0; c < 24; ++c) {
                    double s2 = 0.0;
                    for (int m = 0; m < 6; ++m) s2 += B[m][r] * DB[m][c];
                    em.Ke[r*24 + c] += s2 * detJ;
                }
        }
        for (int r = 0; r < 24; ++r) em.diagE[r] = em.Ke[r*24 + r];
        em.elasticReady = true;
    }

    if (needThermal) {
        if (!(kCond > 0)) return false;
        double G[3][8];
        for (int g = 0; g < 8; ++g) {
            const double s = 1.0 / std::sqrt(3.0);
            const double xi = ((g & 1) ? s : -s);
            const double et = ((g & 2) ? s : -s);
            const double ze = ((g & 4) ? s : -s);
            for (int a = 0; a < 8; ++a) {
                double dxi, det, dze;
                femShapeGrad(a, xi, et, ze, dxi, det, dze);
                G[0][a] = dxi * i2x; G[1][a] = det * i2y; G[2][a] = dze * i2z;
            }
            for (int r = 0; r < 8; ++r)
                for (int c = 0; c < 8; ++c) {
                    double s2 = G[0][r]*G[0][c] + G[1][r]*G[1][c] + G[2][r]*G[2][c];
                    em.Kt[r*8 + c] += kCond * s2 * detJ;
                }
        }
        for (int r = 0; r < 8; ++r) em.diagT[r] = em.Kt[r*8 + r];
        em.thermalReady = true;
    }
    return needElastic ? em.elasticReady : em.thermalReady;
}

// -----------------------------------------------------
// Система: оператор K*u без хранения матрицы + граничные Robin-члены
// -----------------------------------------------------
struct FemOperator {
    const FemMesh* mesh = nullptr;
    FemElemMat     em;
    bool           thermal = false;
    int            ndof = 0;
    // цветовые группы элементов (чётность i,j,k) — элементы одной группы не
    // имеют общих узлов, поэтому сборку можно параллелить без атомика
    std::vector<int> groupStart;   // 9 значений: границы 8 групп в orderElem
    std::vector<int> orderElem;    // плотные индексы элементов, отсортированные по группам
    std::vector<double> robinNode; // конвекция: Σ h·A по граням узла (0 — узла нет)
    std::vector<double> robinFlow; // конвекция: Σ h·A·T_окр по граням узла (Вт)
    // Robin-члены (конвекция), СОБРАННЫЕ ПО УЗЛАМ: robinNode[i] = Σ h·A/4 по всем
    // граням узла i, и дальше Ku[i] += robinNode[i]*u[i].
    // Хранить пары «грань-узел» (как было раньше) нельзя: узел входит в несколько
    // граней, значит один и тот же Ku[i] обновлялся бы из разных нитей OpenMP
    // одновременно — гонка, потерянные слагаемые и недетерминированный ответ
    // (на задаче с конвекцией поток занижался до 69% от аналитического и менялся
    // от запуска к запуску). Агрегация по узлам делает запись однозначной.

    void prepare(const FemMesh& m, bool isThermal) {
        mesh = &m; thermal = isThermal;
        const int ncomp = isThermal ? 1 : 3;
        ndof = m.nodeCount * ncomp;
        orderElem.clear();
        groupStart.assign(9, 0);
        robinNode.assign((size_t)m.nodeCount, 0.0);
        robinFlow.assign((size_t)m.nodeCount, 0.0);
        std::vector<std::vector<int>> g(8);
        for (int e = 0; e < m.elemCount; ++e) {
            const int i = m.elemI[e], j = m.elemJ[e], k = m.elemK[e];
            const int col = (i & 1) | ((j & 1) << 1) | ((k & 1) << 2);
            g[col].push_back(e);
        }
        int acc = 0;
        for (int c = 0; c < 8; ++c) {
            groupStart[c] = acc;
            for (int e : g[c]) orderElem.push_back(e);
            acc += (int)g[c].size();
        }
        groupStart[8] = acc;
    }

    // Ku = K * u  (u уже содержит предписанные значения на закреплённых DOF)
    inline void apply(const std::vector<double>& u, std::vector<double>& Ku) const {
        std::fill(Ku.begin(), Ku.end(), 0.0);
        if (!mesh) return;
        const int ncomp = thermal ? 1 : 3;
        const int en = thermal ? 8 : 24;
        const double* Kmat = thermal ? &em.Kt[0] : &em.Ke[0];
        int nodes[8];
        for (int c = 0; c < 8; ++c) {
            const int b0 = groupStart[c], b1 = groupStart[c+1];
            #ifdef _OPENMP
            #pragma omp parallel for schedule(static) private(nodes)
            #endif
            for (int t = b0; t < b1; ++t) {
                const int e = orderElem[t];
                mesh->elemNodesByIdx(e, nodes);
                // DOF-раскладка элемента: для теплового задачи a = номер узла,
                // для упругости a = 3*узел + компонента (узел = a/3, компонента = a%3).
                double ue[24], ke[24];
                int dofNode[24], dofComp[24];
                for (int a = 0; a < en; ++a) {
                    dofNode[a] = thermal ? a : (a / 3);
                    dofComp[a] = thermal ? 0 : (a % 3);
                }
                for (int a = 0; a < en; ++a)
                    ue[a] = u[nodes[dofNode[a]] * ncomp + dofComp[a]];
                for (int r = 0; r < en; ++r) {
                    const double* row = Kmat + r * en;
                    double s = 0.0;
                    for (int q = 0; q < en; ++q) s += row[q] * ue[q];
                    ke[r] = s;
                }
                for (int a = 0; a < en; ++a)
                    Ku[nodes[dofNode[a]] * ncomp + dofComp[a]] += ke[a];
            }
        }
        if (!robinNode.empty()) {
            // одна запись в Ku[i] на итерацию — гонки нет при любом числе нитей
            #ifdef _OPENMP
            #pragma omp parallel for schedule(static)
            #endif
            for (int i = 0; i < (int)robinNode.size(); ++i) {
                const double h = robinNode[(size_t)i];
                if (h != 0.0) Ku[(size_t)i] += h * u[(size_t)i];
            }
        }
    }
};

// -----------------------------------------------------
// Предусловленный метод сопряжённых градиентов.
// Закреплённые DOF не обновляются: их значения лежат в u (предписанные),
// невязка по ним обнуляется.
// Возвращает число выполненных итераций; finalRel — ||r||/||f||.
// Допуск сходимости, которому можно доверять.
//
// Значение tol >= 1 (а также NaN, Inf и неположительные числа) делает условие
// сходимости истинным на первой же итерации: решатель рапортует «сошёлся»,
// фактически не решив систему. Такой ответ хуже явной ошибки — его принимают за
// результат. Поэтому бессмысленный допуск заменяется безопасным 1e-10, а
// вызывающий код пишет об этом предупреждение и показывает реально использованное
// значение.
inline double femSafeRelTolerance(const StudySolverSettings& st, bool& wasFixed) {
    const double t = st.relTolerance;
    wasFixed = !(std::isfinite(t) && t > 0.0 && t < 1.0);
    return wasFixed ? 1e-10 : t;
}

// -----------------------------------------------------
inline int femPCG(const FemOperator& op,
                  const std::vector<double>& diag,
                  const std::vector<double>& f,
                  const std::vector<char>& fixed,
                  std::vector<double>& u,
                  const StudySolverSettings& st,
                  std::vector<double>& history,
                  double& finalRel,
                  bool& converged)
{
    const int n = (int)f.size();
    converged = false;
    finalRel = 1.0;
    history.clear();
    bool tolFixed = false;
    const double tol = femSafeRelTolerance(st, tolFixed);
    (void)tolFixed;   // о замене допуска сообщает вызывающий решатель
    std::vector<double> r(n), z(n), p(n), Kp(n);

    double fnorm = 0.0;
    for (int i = 0; i < n; ++i) fnorm += f[i] * f[i];
    fnorm = std::sqrt(fnorm);
    if (!(fnorm > 0.0)) fnorm = 1.0;

    op.apply(u, Kp);                       // Kp = K*u0
    for (int i = 0; i < n; ++i) {
        r[i] = fixed[i] ? 0.0 : (f[i] - Kp[i]);
        z[i] = (st.jacobiPrecond && diag[i] > 0.0) ? r[i] / diag[i] : r[i];
        p[i] = z[i];
    }
    double rz = 0.0;
    for (int i = 0; i < n; ++i) rz += r[i] * z[i];

    double rnorm = 0.0;
    for (int i = 0; i < n; ++i) rnorm += r[i] * r[i];
    rnorm = std::sqrt(rnorm);
    finalRel = rnorm / fnorm;
    history.push_back(finalRel);

    const int maxIt = st.maxIterations > 0 ? st.maxIterations : 1000;
    int it = 0;
    for (; it < maxIt; ++it) {
        if (finalRel <= tol) { converged = true; break; }
        op.apply(p, Kp);
        double pKp = 0.0;
        for (int i = 0; i < n; ++i) pKp += p[i] * Kp[i];
        if (!(pKp > 0.0)) break;           // вырождение: дальше идти некуда
        const double alpha = rz / pKp;
        if (!std::isfinite(alpha)) break;
        #ifdef _OPENMP
        #pragma omp parallel for schedule(static)
        #endif
        for (int i = 0; i < n; ++i) {
            if (fixed[i]) continue;
            u[i] += alpha * p[i];
            r[i] -= alpha * Kp[i];
        }
        double rzNew = 0.0, rn = 0.0;
        for (int i = 0; i < n; ++i) {
            if (fixed[i]) { r[i] = 0.0; z[i] = 0.0; continue; }
            z[i] = (st.jacobiPrecond && diag[i] > 0.0) ? r[i] / diag[i] : r[i];
            rzNew += r[i] * z[i];
            rn += r[i] * r[i];
        }
        rnorm = std::sqrt(rn);
        finalRel = rnorm / fnorm;
        if ((it % 5) == 0 || it == maxIt - 1) history.push_back(finalRel);
        if (!(rzNew > 0.0) || !std::isfinite(rzNew)) break;
        const double beta = rzNew / (rz > 0.0 ? rz : 1.0);
        rz = rzNew;
        #ifdef _OPENMP
        #pragma omp parallel for schedule(static)
        #endif
        for (int i = 0; i < n; ++i) p[i] = fixed[i] ? 0.0 : (z[i] + beta * p[i]);
    }
    if (finalRel <= tol) converged = true;
    return it;
}

// -----------------------------------------------------
// Подготовка системы: граничные условия и нагрузки -> fixed, prescribed, f, diag
// -----------------------------------------------------
struct FemSystemSetup {
    std::vector<double> f;
    std::vector<double> diag;
    std::vector<char>   fixed;
    std::vector<double> prescribed;
    double appliedLoadSum = 0;
    double totalHeatRate  = 0;   // заданный подвод тепла: тепловыделение + поток (Вт)
    std::vector<std::string> warnings;
};

inline bool femSetupSystem(const FemMesh& mesh, const StudyProject& pr, bool thermal,
                           FemOperator& op, FemSystemSetup& sys, std::string& err) {
    err.clear();
    sys.warnings.clear();
    const StudyMaterial& mat = pr.material();
    if (!femBuildElemMatrices(op.em, mesh.hx, mesh.hy, mesh.hz,
                              mat.youngE, mat.poisson, mat.conductivity,
                              !thermal, thermal)) {
        err = "не удалось собрать элементные матрицы: проверьте свойства материала";
        return false;
    }
    op.prepare(mesh, thermal);
    const int ncomp = thermal ? 1 : 3;
    const int ndof = mesh.nodeCount * ncomp;
    sys.f.assign((size_t)ndof, 0.0);
    sys.fixed.assign((size_t)ndof, 0);
    sys.prescribed.assign((size_t)ndof, 0.0);
    sys.diag.assign((size_t)ndof, 0.0);
    sys.appliedLoadSum = 0.0;
    sys.totalHeatRate = 0.0;

    // диагональ (Jacobi): сумма элементных диагоналей по всем элементам узла
    {
        const int en = thermal ? 8 : 24;
        const double* dsrc = thermal ? &op.em.diagT[0] : &op.em.diagE[0];
        int nodes[8];
        for (int e = 0; e < mesh.elemCount; ++e) {
            mesh.elemNodesByIdx(e, nodes);
            for (int a = 0; a < en; ++a) {
                const int ni = thermal ? a : (a / 3);
                const int ci = thermal ? 0 : (a % 3);
                const int d = nodes[ni] * ncomp + ci;
                sys.diag[(size_t)d] += dsrc[a];
            }
        }
        // Robin-члены (конвекция) добавляются к диагонали ниже
    }

    const double tol = femMeshTolerance(mesh);

    // --- объёмные нагрузки: сила тяжести и тепловыделение ---
    for (const StudyLoad& l : pr.loads) {
        if (!l.enabled) continue;
        if (thermal && l.kind == StudyBCKind::HeatGeneration) {
            int nodes[8];
            const double Q = l.value;                     // Вт/м3
            const double contrib = Q * op.em.volume / 8.0;
            int cnt = 0;
            for (int e = 0; e < mesh.elemCount; ++e) {
                const double cx = mesh.ox + (mesh.elemI[e] + 0.5) * mesh.hx;
                const double cy = mesh.oy + (mesh.elemJ[e] + 0.5) * mesh.hy;
                const double cz = mesh.oz + (mesh.elemK[e] + 0.5) * mesh.hz;
                if (!l.region.contains(cx, cy, cz, tol)) continue;
                mesh.elemNodesByIdx(e, nodes);
                for (int a = 0; a < 8; ++a) { sys.f[(size_t)nodes[a]] += contrib; cnt++; }
                sys.totalHeatRate += Q * op.em.volume;
            }
            if (cnt == 0) sys.warnings.push_back("тепловыделение «" + l.name + "»: под условие не попал ни один элемент");
        }
        if (!thermal && l.kind == StudyBCKind::Gravity) {
            int nodes[8];
            const double contrib = mat.density * op.em.volume / 8.0;
            for (int e = 0; e < mesh.elemCount; ++e) {
                mesh.elemNodesByIdx(e, nodes);
                for (int a = 0; a < 8; ++a) {
                    sys.f[(size_t)nodes[a]*3 + 0] += contrib * l.vx;
                    sys.f[(size_t)nodes[a]*3 + 1] += contrib * l.vy;
                    sys.f[(size_t)nodes[a]*3 + 2] += contrib * l.vz;
                }
            }
            sys.appliedLoadSum += mat.density * mesh.volume *
                                  std::sqrt(l.vx*l.vx + l.vy*l.vy + l.vz*l.vz);
        }
    }

    // --- условия на узлах: защемление, перемещение, температура, сила ---
    for (const StudyLoad& l : pr.loads) {
        if (!l.enabled) continue;
        if (thermal) {
            if (l.kind != StudyBCKind::Temperature) continue;
            int hit = 0;
            for (int nd = 0; nd < mesh.nodeCount; ++nd) {
                if (!femNodeInBox(mesh, nd, l.region.minX, l.region.minY, l.region.minZ,
                                  l.region.maxX, l.region.maxY, l.region.maxZ, tol)) continue;
                // для пресета грани — только слой у плоскости грани (иначе условие
                // Дирихле ставится не на грани, а вглубь тела на толщину допуска)
                if (!l.region.regionOnFacePlane(mesh.nodeX(nd), mesh.nodeY(nd), mesh.nodeZ(nd), tol)) continue;
                if (l.region.onlyBoundary && !mesh.nodeIsBoundary[(size_t)nd]) continue;
                sys.fixed[(size_t)nd] = 1;
                sys.prescribed[(size_t)nd] = l.value;
                hit++;
            }
            if (hit == 0) sys.warnings.push_back("температура «" + l.name + "»: под условие не попал ни один узел");
        } else {
            if (l.kind == StudyBCKind::Fixed || l.kind == StudyBCKind::Displacement) {
                int hit = 0;
                for (int nd = 0; nd < mesh.nodeCount; ++nd) {
                    if (!femNodeInBox(mesh, nd, l.region.minX, l.region.minY, l.region.minZ,
                                      l.region.maxX, l.region.maxY, l.region.maxZ, tol)) continue;
                    // пресет грани: закрепление лежит В ПЛОСКОСТИ грани, независимо
                    // от того, каким было разрешение сетки при создании условия
                    if (!l.region.regionOnFacePlane(mesh.nodeX(nd), mesh.nodeY(nd), mesh.nodeZ(nd), tol)) continue;
                    if (l.region.onlyBoundary && !mesh.nodeIsBoundary[(size_t)nd]) continue;
                    const double ux = (l.kind == StudyBCKind::Fixed) ? 0.0 : l.vx;
                    const double uy = (l.kind == StudyBCKind::Fixed) ? 0.0 : l.vy;
                    const double uz = (l.kind == StudyBCKind::Fixed) ? 0.0 : l.vz;
                    for (int c = 0; c < 3; ++c) {
                        sys.fixed[(size_t)nd*3 + c] = 1;
                        sys.prescribed[(size_t)nd*3 + c] = (c == 0) ? ux : (c == 1 ? uy : uz);
                    }
                    hit++;
                }
                if (hit == 0) sys.warnings.push_back(
                    std::string(l.kind == StudyBCKind::Fixed ? "защемление «" : "перемещение «") + l.name +
                    "»: под условие не попал ни один узел — проверьте область");
            } else if (l.kind == StudyBCKind::Force) {
                std::vector<int> sel;
                for (int nd = 0; nd < mesh.nodeCount; ++nd) {
                    if (!femNodeInBox(mesh, nd, l.region.minX, l.region.minY, l.region.minZ,
                                      l.region.maxX, l.region.maxY, l.region.maxZ, tol)) continue;
                    // пресет грани: сила прикладывается к узлам В ПЛОСКОСТИ грани
                    if (!l.region.regionOnFacePlane(mesh.nodeX(nd), mesh.nodeY(nd), mesh.nodeZ(nd), tol)) continue;
                    if (l.region.onlyBoundary && !mesh.nodeIsBoundary[(size_t)nd]) continue;
                    sel.push_back(nd);
                }
                if (sel.empty()) {
                    sys.warnings.push_back("сила «" + l.name + "»: под условие не попал ни один узел");
                } else {
                    const double w = 1.0 / (double)sel.size();
                    for (int nd : sel) {
                        sys.f[(size_t)nd*3 + 0] += l.vx * w;
                        sys.f[(size_t)nd*3 + 1] += l.vy * w;
                        sys.f[(size_t)nd*3 + 2] += l.vz * w;
                    }
                    sys.appliedLoadSum += std::sqrt(l.vx*l.vx + l.vy*l.vy + l.vz*l.vz);
                }
            }
        }
    }

    // --- условия на гранях: давление, тепловой поток, конвекция ---
    for (const StudyLoad& l : pr.loads) {
        if (!l.enabled) continue;
        if (thermal) {
            if (l.kind != StudyBCKind::HeatFlux && l.kind != StudyBCKind::Convection) continue;
            int hit = 0;
            for (const FemMesh::Face& fc : mesh.faces) {
                if (!l.region.contains(fc.cx, fc.cy, fc.cz, tol)) continue;
                if (!l.region.matchesNormal(fc.nx, fc.ny, fc.nz)) continue;   // выбор грани
                hit++;
                if (l.kind == StudyBCKind::HeatFlux) {
                    const double contrib = l.value * fc.area / 4.0;   // Вт на узел
                    for (int q = 0; q < 4; ++q) sys.f[(size_t)fc.nodes[q]] += contrib;
                    sys.totalHeatRate += l.value * fc.area;
                } else {
                    // конвекция: q = h*(T_окр - T). Сосредоточенная граничная матрица:
                    //   f += h*T_окр*A/4,  K[dof] += h*A/4 (по каждому узлу грани)
                    const double hA4 = l.value * fc.area / 4.0;
                    for (int q = 0; q < 4; ++q) {
                        sys.f[(size_t)fc.nodes[q]] += hA4 * l.ambient;
                        op.robinNode[(size_t)fc.nodes[q]] += hA4;
                        op.robinFlow[(size_t)fc.nodes[q]] += hA4 * l.ambient;
                        sys.diag[(size_t)fc.nodes[q]] += hA4;
                    }
                    // Обмен через конвекцию заранее НЕ оценивается (раньше здесь
                    // стояло h*(T_окр - 293.15)*A — догадка с фиксированной
                    // опорной температурой). Фактический поток получается из
                    // решения: см. femHeatReactions и баланс в femSolveSteadyThermal.
                }
            }
            if (hit == 0) sys.warnings.push_back(std::string(studyBCKindName(l.kind)) +
                                                 " «" + l.name + "»: под условие не попала ни одна внешняя грань");
        } else if (l.kind == StudyBCKind::Pressure) {
            int hit = 0;
            for (const FemMesh::Face& fc : mesh.faces) {
                if (!l.region.contains(fc.cx, fc.cy, fc.cz, tol)) continue;
                if (!l.region.matchesNormal(fc.nx, fc.ny, fc.nz)) continue;   // выбор грани
                hit++;
                // согласованные узловые силы: F_узел = p * n * A / 4
                const double w = l.value * fc.area * 0.25;
                for (int q = 0; q < 4; ++q) {
                    sys.f[(size_t)fc.nodes[q]*3 + 0] += w * fc.nx;
                    sys.f[(size_t)fc.nodes[q]*3 + 1] += w * fc.ny;
                    sys.f[(size_t)fc.nodes[q]*3 + 2] += w * fc.nz;
                }
                sys.appliedLoadSum += std::fabs(l.value) * fc.area;
            }
            if (hit == 0) sys.warnings.push_back("давление «" + l.name +
                                                 "»: под условие не попала ни одна внешняя грань");
        }
    }

    if (ndof <= 0) { err = "в сетке нет узлов"; return false; }
    return true;
}

// -----------------------------------------------------
// Напряжения в элементе (среднее по 8 точкам Гаусса) и фон Мизес.
//
// Осреднение по узлам — стандартная выдача результатов в МКЭ: без него поле
// кусочно-постоянно по элементам и выглядит «клетчатым». Осреднение выполняется
// по числу примыкающих элементов.
// -----------------------------------------------------
inline void femElemStress(const FemMesh& mesh, double E, double nu,
                          const std::vector<double>& u, int denseElem, double sigma[6]) {
    int nodes[8];
    mesh.elemNodesByIdx(denseElem, nodes);
    double ue[24];
    for (int a = 0; a < 8; ++a)
        for (int c = 0; c < 3; ++c)
            ue[3*a + c] = u[(size_t)nodes[a]*3 + c];

    const double i2x = 2.0 / mesh.hx, i2y = 2.0 / mesh.hy, i2z = 2.0 / mesh.hz;
    double D[6][6];
    femElasticD(E, nu, D);
    for (int r = 0; r < 6; ++r) sigma[r] = 0.0;

    double B[6][24], eps6[6];
    const double gs = 1.0 / std::sqrt(3.0);
    for (int g = 0; g < 8; ++g) {
        const double xi = ((g & 1) ? gs : -gs), et = ((g & 2) ? gs : -gs), ze = ((g & 4) ? gs : -gs);
        for (int r = 0; r < 6; ++r) for (int c = 0; c < 24; ++c) B[r][c] = 0.0;
        for (int a = 0; a < 8; ++a) {
            double dxi, det, dze;
            femShapeGrad(a, xi, et, ze, dxi, det, dze);
            const double dx = dxi * i2x, dy = det * i2y, dz = dze * i2z;
            B[0][3*a+0] = dx;
            B[1][3*a+1] = dy;
            B[2][3*a+2] = dz;
            B[3][3*a+0] = dy; B[3][3*a+1] = dx;
            B[4][3*a+0] = dz; B[4][3*a+2] = dx;
            B[5][3*a+1] = dz; B[5][3*a+2] = dy;
        }
        for (int r = 0; r < 6; ++r) {
            double e = 0.0;
            for (int c = 0; c < 24; ++c) e += B[r][c] * ue[c];
            eps6[r] = e;
        }
        for (int r = 0; r < 6; ++r) {
            double sg = 0.0;
            for (int c = 0; c < 6; ++c) sg += D[r][c] * eps6[c];
            sigma[r] += sg * 0.125;   // среднее по 8 точкам Гаусса
        }
    }
}

inline double femVonMises(double sxx, double syy, double szz,
                          double sxy, double sxz, double syz) {
    const double d1 = sxx - syy, d2 = syy - szz, d3 = szz - sxx;
    const double v = 0.5*(d1*d1 + d2*d2 + d3*d3) + 3.0*(sxy*sxy + sxz*sxz + syz*syz);
    return (v > 0.0) ? std::sqrt(v) : 0.0;
}

// Напряжения и фон Мизес по узлам (осреднение по примыкающим элементам).
inline void femNodalStresses(const FemMesh& mesh, double E, double nu,
                             const std::vector<double>& u, StudyResults& res) {
    const int n = mesh.nodeCount;
    res.sxx.assign((size_t)n, 0.0); res.syy.assign((size_t)n, 0.0); res.szz.assign((size_t)n, 0.0);
    res.sxy.assign((size_t)n, 0.0); res.sxz.assign((size_t)n, 0.0); res.syz.assign((size_t)n, 0.0);
    res.vonMises.assign((size_t)n, 0.0);
    std::vector<double> cnt((size_t)n, 0.0);
    double sigma[6];
    int nodes[8];
    for (int e = 0; e < mesh.elemCount; ++e) {
        femElemStress(mesh, E, nu, u, e, sigma);
        mesh.elemNodesByIdx(e, nodes);
        for (int a = 0; a < 8; ++a) {
            const int nd = nodes[a];
            if (nd < 0) continue;
            res.sxx[(size_t)nd] += sigma[0];
            res.syy[(size_t)nd] += sigma[1];
            res.szz[(size_t)nd] += sigma[2];
            res.sxy[(size_t)nd] += sigma[3];
            res.sxz[(size_t)nd] += sigma[4];
            res.syz[(size_t)nd] += sigma[5];
            cnt[(size_t)nd] += 1.0;
        }
    }
    res.maxVonMises = 0.0;
    for (int i = 0; i < n; ++i) {
        if (cnt[i] > 0.0) {
            const double inv = 1.0 / cnt[i];
            res.sxx[(size_t)i] *= inv; res.syy[(size_t)i] *= inv; res.szz[(size_t)i] *= inv;
            res.sxy[(size_t)i] *= inv; res.sxz[(size_t)i] *= inv; res.syz[(size_t)i] *= inv;
        }
        res.vonMises[(size_t)i] = femVonMises(res.sxx[(size_t)i], res.syy[(size_t)i], res.szz[(size_t)i],
                                              res.sxy[(size_t)i], res.sxz[(size_t)i], res.syz[(size_t)i]);
        if (res.vonMises[(size_t)i] > res.maxVonMises) res.maxVonMises = res.vonMises[(size_t)i];
    }
}

// Реакции в закреплениях: R = (K*u - f) по закреплённым DOF.
// Проверка равновесия (сумма реакций уравновешивает приложенные силы) делается
// в решателе и попадает в предупреждения.
inline void femReactions(const FemOperator& op, const std::vector<double>& u,
                         const std::vector<double>& f, const std::vector<char>& fixed,
                         StudyResults& res) {
    const int n = (int)f.size();
    std::vector<double> Ku((size_t)n, 0.0);
    op.apply(u, Ku);
    const int nn = op.mesh ? op.mesh->nodeCount : 0;
    res.rx.assign((size_t)nn, 0.0);
    res.ry.assign((size_t)nn, 0.0);
    res.rz.assign((size_t)nn, 0.0);
    double sx = 0, sy = 0, sz = 0;
    for (int i = 0; i < n; ++i) {
        if (!fixed[i]) continue;
        const double R = Ku[(size_t)i] - f[(size_t)i];
        const int nd = i / 3, c = i % 3;
        if (nd < 0 || nd >= nn) continue;
        if (c == 0) { res.rx[(size_t)nd] += R; sx += R; }
        else if (c == 1) { res.ry[(size_t)nd] += R; sy += R; }
        else { res.rz[(size_t)nd] += R; sz += R; }
    }
    res.reactionResultant = std::sqrt(sx*sx + sy*sy + sz*sz);
}

// Тепловые «реакции»: поток, который граничное условие с заданной температурой
// подводит к телу. R = K·T - f в узлах с условием Дирихле; R > 0 — подвод,
// R < 0 — отвод. Это единственный честный способ получить суммарный поток через
// границы: заранее он неизвестен, а оценка по «опорной» температуре была бы догадкой.
inline void femHeatReactions(const FemOperator& op, const std::vector<double>& T,
                             const std::vector<double>& f, const std::vector<char>& fixed,
                             StudyResults& res) {
    const int n = (int)f.size();
    std::vector<double> KT((size_t)n, 0.0);
    op.apply(T, KT);
    const int nn = op.mesh ? op.mesh->nodeCount : 0;
    res.heatReaction.assign((size_t)nn, 0.0);
    double in = 0.0, out = 0.0;
    for (int i = 0; i < n; ++i) {
        if (!fixed[i]) continue;
        const double R = KT[(size_t)i] - f[(size_t)i];
        if (!std::isfinite(R)) continue;
        if (i < nn) res.heatReaction[(size_t)i] += R;
        if (R > 0.0) in += R; else out -= R;
    }
    // Съём (или подвод) тепла конвекцией — из решения:
    //   Q_конв = Σ_i [ (Σ h·A)_i · T_i - (Σ h·A·T_окр)_i ]   > 0 — тепло уходит в среду.
    // Это НЕ реакция Дирихле: узлы с конвекцией свободны, поэтому раньше такая
    // граница вообще не попадала в баланс и «отведено» оставалось нулём.
    double conv = 0.0;
    for (size_t i = 0; i < op.robinNode.size(); ++i) {
        if (op.robinNode[i] == 0.0) continue;
        const double q = op.robinNode[i] * T[i] - op.robinFlow[i];
        if (std::isfinite(q)) conv += q;
        if ((int)i < nn) res.heatReaction[i] += q;   // узлу приписываем его обмен со средой
    }
    res.heatRateConvective = conv;
    if (conv > 0.0) out += conv; else in -= conv;
    res.heatRateIn = in;
    res.heatRateOut = out;
}

inline double femStrainEnergy(const FemOperator& op, const std::vector<double>& u) {
    std::vector<double> Ku(u.size(), 0.0);
    op.apply(u, Ku);
    double s = 0.0;
    for (size_t i = 0; i < u.size(); ++i) s += u[i] * Ku[i];
    return 0.5 * s;
}

// Тепловой поток q = -k*grad T: в элементах, затем осреднение по узлам.
inline void femNodalHeatFlux(const FemMesh& mesh, double kCond,
                             const std::vector<double>& T, StudyResults& res) {
    const int n = mesh.nodeCount;
    res.qx.assign((size_t)n, 0.0); res.qy.assign((size_t)n, 0.0); res.qz.assign((size_t)n, 0.0);
    res.heatFluxMag.assign((size_t)n, 0.0);
    std::vector<double> cnt((size_t)n, 0.0);
    const double i2x = 2.0 / mesh.hx, i2y = 2.0 / mesh.hy, i2z = 2.0 / mesh.hz;
    int nodes[8];
    res.maxHeatFlux = 0.0;
    for (int e = 0; e < mesh.elemCount; ++e) {
        mesh.elemNodesByIdx(e, nodes);
        double gx = 0, gy = 0, gz = 0;
        // градиент в центре элемента: dN/dx там равен +-1/(2h)*0.5 = +-1/(4h)
        for (int a = 0; a < 8; ++a) {
            double dxi, det, dze;
            femShapeGrad(a, 0.0, 0.0, 0.0, dxi, det, dze);
            const double t = T[(size_t)nodes[a]];
            gx += dxi * i2x * t;
            gy += det * i2y * t;
            gz += dze * i2z * t;
        }
        const double qxe = -kCond * gx, qye = -kCond * gy, qze = -kCond * gz;
        for (int a = 0; a < 8; ++a) {
            const int nd = nodes[a];
            if (nd < 0) continue;
            res.qx[(size_t)nd] += qxe; res.qy[(size_t)nd] += qye; res.qz[(size_t)nd] += qze;
            cnt[(size_t)nd] += 1.0;
        }
    }
    for (int i = 0; i < n; ++i) {
        if (cnt[i] > 0.0) {
            const double inv = 1.0 / cnt[i];
            res.qx[(size_t)i] *= inv; res.qy[(size_t)i] *= inv; res.qz[(size_t)i] *= inv;
        }
        const double m = std::sqrt(res.qx[(size_t)i]*res.qx[(size_t)i] +
                                   res.qy[(size_t)i]*res.qy[(size_t)i] +
                                   res.qz[(size_t)i]*res.qz[(size_t)i]);
        res.heatFluxMag[(size_t)i] = m;
        if (m > res.maxHeatFlux) res.maxHeatFlux = m;
    }
}

// -----------------------------------------------------
// Линейный статический анализ: постановка -> решение -> результаты
// -----------------------------------------------------
inline bool femSolveLinearStatic(const FemMesh& mesh, const StudyProject& pr,
                                 StudyResults& res, std::string& err) {
    res.clear();
    res.analysis = StudyAnalysis::LinearStatic;
    err.clear();
    if (!mesh.built || mesh.elemCount <= 0) {
        // статус обязан быть «ошибка», а не «не запускался»: интерфейс и отчёт
        // показывают причину отказа, а не пустое состояние
        err = "нет объёмной сетки — постройте её";
        res.status = StudySolveStatus::Error;
        res.errorMessage = err;
        return false;
    }

    FemOperator op;
    FemSystemSetup sys;
    res.status = StudySolveStatus::Preparing;
    if (!femSetupSystem(mesh, pr, false, op, sys, err)) {
        res.status = StudySolveStatus::Error; res.errorMessage = err; return false;
    }

    const int ndof = op.ndof;
    if (ndof <= 0) {
        err = "в задаче нет степеней свободы";
        res.status = StudySolveStatus::Error; res.errorMessage = err; return false;
    }

    // Сингулярность ловится ДО решения: иначе CG «сойдётся» на мусоре.
    long long nFixed = 0;
    for (int i = 0; i < ndof; ++i) if (sys.fixed[i]) nFixed++;
    if (nFixed == 0) {
        err = "тело не закреплено: матрица жёсткости сингулярна";
        res.status = StudySolveStatus::Error; res.errorMessage = err; return false;
    }
    if (mesh.components > 1) {
        std::vector<char> compFixed((size_t)mesh.components, 0);
        int nodes[8];
        for (int e = 0; e < mesh.elemCount; ++e) {
            mesh.elemNodesByIdx(e, nodes);
            const int c = mesh.elemComponent[(size_t)e];
            if (c < 0) continue;
            for (int a = 0; a < 8; ++a)
                for (int comp = 0; comp < 3; ++comp)
                    if (sys.fixed[(size_t)nodes[a]*3 + comp]) compFixed[(size_t)c] = 1;
        }
        for (int c = 0; c < mesh.components; ++c) {
            if (!compFixed[(size_t)c]) {
                char b[192];
                std::snprintf(b, sizeof(b), "часть тела №%d не закреплена — система сингулярна", c + 1);
                err = b;
                res.status = StudySolveStatus::Error; res.errorMessage = err; return false;
            }
        }
    }

    std::vector<double> u((size_t)ndof, 0.0);
    for (int i = 0; i < ndof; ++i) if (sys.fixed[i]) u[(size_t)i] = sys.prescribed[(size_t)i];

    res.status = StudySolveStatus::Running;
    const auto t0 = std::chrono::steady_clock::now();
    bool conv = false;
    double finalRel = 1.0;
    const int iters = femPCG(op, sys.diag, sys.f, sys.fixed, u, pr.solver,
                             res.residualHistory, finalRel, conv);
    const auto t1 = std::chrono::steady_clock::now();

    res.nodes = mesh.nodeCount; res.elements = mesh.elemCount; res.dofs = ndof;
    res.iterations = iters;
    res.finalResidual = finalRel;
    res.initialResidual = res.residualHistory.empty() ? finalRel : res.residualHistory.front();
    res.solveTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    res.volumeM3 = mesh.volume;
    res.massKg = mesh.massKg;
    res.appliedLoadSum = sys.appliedLoadSum;
    res.warnings.insert(res.warnings.end(), sys.warnings.begin(), sys.warnings.end());
    {
        // Если допуск сходимости задан бессмысленно, решатель использовал безопасный —
        // и обязан об этом сказать: иначе «сошёлся» выглядит как честно полученный ответ.
        bool tolFixed = false;
        const double tolUsed = femSafeRelTolerance(pr.solver, tolFixed);
        if (tolFixed) {
            char tb[224];
            std::snprintf(tb, sizeof(tb),
                          "допуск сходимости %.3g вне диапазона (0; 1) — использован %.1g",
                          pr.solver.relTolerance, tolUsed);
            res.warnings.push_back(tb);
        }
    }

    if (conv) res.status = StudySolveStatus::Converged;
    else {
        res.status = StudySolveStatus::NotConverged;
        char b[256];
        std::snprintf(b, sizeof(b),
                      "сходимость не достигнута за %d итераций: остаточная невязка %.3e (допуск %.1e)",
                      iters, finalRel, pr.solver.relTolerance);
        res.errorMessage = b;
        res.warnings.push_back(b);
    }

    res.ux.assign((size_t)mesh.nodeCount, 0.0);
    res.uy.assign((size_t)mesh.nodeCount, 0.0);
    res.uz.assign((size_t)mesh.nodeCount, 0.0);
    res.maxDisplacement = 0.0;
    for (int nd = 0; nd < mesh.nodeCount; ++nd) {
        const double a = u[(size_t)nd*3+0], b = u[(size_t)nd*3+1], c = u[(size_t)nd*3+2];
        res.ux[(size_t)nd] = a; res.uy[(size_t)nd] = b; res.uz[(size_t)nd] = c;
        const double m = std::sqrt(a*a + b*b + c*c);
        if (m > res.maxDisplacement) res.maxDisplacement = m;
    }
    const StudyMaterial& mat = pr.material();
    if (pr.solver.vonMises) femNodalStresses(mesh, mat.youngE, mat.poisson, u, res);
    if (pr.solver.reactions) femReactions(op, u, sys.f, sys.fixed, res);
    res.strainEnergy = femStrainEnergy(op, u);
    res.valid = (res.status == StudySolveStatus::Converged);

    // Физическая проверка равновесия: сумма реакций уравновешивает приложенные силы.
    {
        double fx = 0, fy = 0, fz = 0;
        for (int i = 0; i < ndof; ++i) {
            if (sys.fixed[i]) continue;
            const int c = i % 3;
            if (c == 0) fx += sys.f[(size_t)i];
            else if (c == 1) fy += sys.f[(size_t)i];
            else fz += sys.f[(size_t)i];
        }
        double rx = 0, ry = 0, rz = 0;
        for (size_t i = 0; i < res.rx.size(); ++i) { rx += res.rx[i]; ry += res.ry[i]; rz += res.rz[i]; }
        const double appl = std::sqrt(fx*fx + fy*fy + fz*fz);
        const double reac = std::sqrt(rx*rx + ry*ry + rz*rz);
        const double imbalance = (appl > 0.0) ? std::fabs(reac - appl) / appl : 0.0;
        if (appl > 0.0 && imbalance > 0.02) {
            char b[256];
            std::snprintf(b, sizeof(b),
                          "нарушено равновесие: сумма реакций %.6g Н против приложенных %.6g Н (расхождение %.1f%%)",
                          reac, appl, imbalance * 100.0);
            res.warnings.push_back(b);
        }
    }
    return res.status == StudySolveStatus::Converged;
}

// -----------------------------------------------------
// Стационарный тепловой анализ
// -----------------------------------------------------
inline bool femSolveSteadyThermal(const FemMesh& mesh, const StudyProject& pr,
                                  StudyResults& res, std::string& err) {
    res.clear();
    res.analysis = StudyAnalysis::SteadyThermal;
    err.clear();
    if (!mesh.built || mesh.elemCount <= 0) {
        err = "нет объёмной сетки — постройте её";
        res.status = StudySolveStatus::Error;
        res.errorMessage = err;
        return false;
    }

    FemOperator op;
    FemSystemSetup sys;
    res.status = StudySolveStatus::Preparing;
    if (!femSetupSystem(mesh, pr, true, op, sys, err)) {
        res.status = StudySolveStatus::Error; res.errorMessage = err; return false;
    }
    const int ndof = op.ndof;
    long long nFixed = 0;
    for (int i = 0; i < ndof; ++i) if (sys.fixed[i]) nFixed++;
    if (nFixed == 0) {
        err = "нет заданной температуры (условия Дирихле): стационарное решение не единственно";
        res.status = StudySolveStatus::Error; res.errorMessage = err; return false;
    }

    std::vector<double> u((size_t)ndof, 0.0);
    for (int i = 0; i < ndof; ++i) if (sys.fixed[i]) u[(size_t)i] = sys.prescribed[(size_t)i];

    res.status = StudySolveStatus::Running;
    const auto t0 = std::chrono::steady_clock::now();
    bool conv = false;
    double finalRel = 1.0;
    const int iters = femPCG(op, sys.diag, sys.f, sys.fixed, u, pr.solver,
                             res.residualHistory, finalRel, conv);
    const auto t1 = std::chrono::steady_clock::now();

    res.nodes = mesh.nodeCount; res.elements = mesh.elemCount; res.dofs = ndof;
    res.iterations = iters;
    res.finalResidual = finalRel;
    res.initialResidual = res.residualHistory.empty() ? finalRel : res.residualHistory.front();
    res.solveTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    res.volumeM3 = mesh.volume;
    res.massKg = mesh.massKg;
    res.warnings.insert(res.warnings.end(), sys.warnings.begin(), sys.warnings.end());
    {
        // Если допуск сходимости задан бессмысленно, решатель использовал безопасный —
        // и обязан об этом сказать: иначе «сошёлся» выглядит как честно полученный ответ.
        bool tolFixed = false;
        const double tolUsed = femSafeRelTolerance(pr.solver, tolFixed);
        if (tolFixed) {
            char tb[224];
            std::snprintf(tb, sizeof(tb),
                          "допуск сходимости %.3g вне диапазона (0; 1) — использован %.1g",
                          pr.solver.relTolerance, tolUsed);
            res.warnings.push_back(tb);
        }
    }

    if (conv) res.status = StudySolveStatus::Converged;
    else {
        res.status = StudySolveStatus::NotConverged;
        char b[256];
        std::snprintf(b, sizeof(b), "сходимость не достигнута за %d итераций: невязка %.3e (допуск %.1e)",
                      iters, finalRel, pr.solver.relTolerance);
        res.errorMessage = b;
        res.warnings.push_back(b);
    }

    res.temperature = u;
    res.minTemperature = res.maxTemperature = u.empty() ? 0.0 : u[0];
    for (size_t i = 0; i < u.size(); ++i) {
        if (u[i] < res.minTemperature) res.minTemperature = u[i];
        if (u[i] > res.maxTemperature) res.maxTemperature = u[i];
    }
    femNodalHeatFlux(mesh, pr.material().conductivity, u, res);

    // Суммарный поток через границы — из решения (R = K·T - f на узлах Дирихле),
    // а не из заранее заданных величин: только так он совпадает с аналитикой.
    femHeatReactions(op, u, sys.f, sys.fixed, res);
    res.heatRateImposed = sys.totalHeatRate;   // тепловыделение + заданные потоки
    res.totalHeatRate = std::max(res.heatRateIn, res.heatRateOut);

    // Проверка теплового баланса: что вошло (границы Дирихле + заданные потоки и
    // тепловыделение) = что вышло (границы Дирихле + конвекция). Конвективный съём
    // теперь считается из решения (femHeatReactions), поэтому проверка полная и
    // работает для любого набора условий — пропускать её при конвекции не нужно.
    {
        const double supplied = res.heatRateIn + res.heatRateImposed;
        const double removed  = res.heatRateOut;
        const double scale = std::max(std::fabs(supplied), std::fabs(removed));
        if (scale > 0.0 && std::fabs(supplied - removed) / scale > 0.02) {
            char b[288];
            std::snprintf(b, sizeof(b),
                          "нарушен тепловой баланс: подведено %.6g Вт, отведено %.6g Вт (расхождение %.1f%%)",
                          supplied, removed, std::fabs(supplied - removed) / scale * 100.0);
            res.warnings.push_back(b);
        }
    }

    res.valid = (res.status == StudySolveStatus::Converged);
    return res.valid;
}

#endif // AEROS_FEM_SOLVER_H
