// ============================================================================
// Теплоперенос и плавучесть: реализация (v1.37.0).
// См. thermal_lbm.h — там список литературы и единицы измерения.
// ============================================================================
#include "thermal_lbm.h"
#include "debug_log.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>

ThermalParams thermalParams;

std::vector<float> thermalT;
std::vector<float> thermalBuoyancyY;
bool  thermalReady = false;
float thermalNu = 0.0f;
float thermalAlpha = 0.0f;
float thermalRayleigh = 0.0f;
float thermalPrandtl = 0.0f;
float thermalGravityLb = 0.0f;
float thermalNusselt = 0.0f;
float thermalMeanT = 0.5f;

namespace {

// D3Q7: покой + 6 соседей. Индексы совпадают с первыми семью направлениями
// D3Q19 решателя (0 — покой, 1..6 — ±x, ±y, ±z), поэтому нет отдельной
// таблицы соответствия.
const int   kQ = 7;
const int   kc[kQ][3] = {
    {0,0,0}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
};
const float kw[kQ] = {
    0.25f, 0.125f, 0.125f, 0.125f, 0.125f, 0.125f, 0.125f
};
const int   kOpp[kQ] = {0, 2, 1, 4, 3, 6, 5};
const float kCs2 = 0.25f;
const float kInvCs2 = 4.0f;

int Nx = 0, Ny = 0, Nz = 0;
std::vector<char>  solid;      // 1 = твёрдое (включая землю)
std::vector<float> g, gNext;   // популяции D3Q7

int yLo = 1, yHi = 0;          // диапазон рядов жидкости (границы полости)
float H = 1.0f;                // высота полости в ячейках
float tauT = 0.7f;             // время релаксации температуры

inline int idx(int x, int y, int z) { return (z*Ny + y)*Nx + x; }

// Равновесная популяция D3Q7: g_eq = w T (1 + c.u / cs^2).
inline float geq(int i, float T, float ux, float uy, float uz) {
    const float cu = (float)kc[i][0]*ux + (float)kc[i][1]*uy + (float)kc[i][2]*uz;
    return kw[i] * T * (1.0f + kInvCs2 * cu);
}

// Температура стенки для граничной ячейки: нижняя стенка горячая, верхняя
// холодная, всё остальное твёрдое — адиабата.
inline bool wallTemp(int y, float* Tw) {
    if (!thermalParams.fixedWallTemps) return false;
    if (y <= 0)      { *Tw = thermalParams.hotTemp;  return true; }
    if (y >= Ny - 1) { *Tw = thermalParams.coldTemp; return true; }
    return false;
}

void computeDiagnostics() {
    double sum = 0.0;
    for (int z = 0; z < Nz; ++z)
        for (int y = yLo; y <= yHi; ++y)
            for (int x = 0; x < Nx; ++x) {
                const int c = idx(x, y, z);
                if (solid[c]) continue;
                sum += thermalT[c];
            }
    const long n = (long)Nx * (long)Nz * (long)(yHi - yLo + 1);
    thermalMeanT = n > 0 ? (float)(sum / (double)n) : 0.5f;
    thermalRayleigh = thermalParams.rayleigh;
    thermalPrandtl = thermalParams.prandtl;
}

} // namespace

bool thermalInit(int nx, int ny, int nz, const std::vector<char>& solidMask, float nuLB) {
    thermalShutdown();
    if (nx < 4 || ny < 4 || nz < 1) return false;
    if (nx * ny * nz > 20 * 1024 * 1024) return false;
    if (!std::isfinite(nuLB) || nuLB <= 0.0f) nuLB = 0.05f;

    Nx = nx; Ny = ny; Nz = nz;
    solid = solidMask;
    if ((int)solid.size() != Nx * Ny * Nz) solid.assign((size_t)Nx*Ny*Nz, 0);

    // Ряды жидкости: по маске, а не по «Ny-2», чтобы модуль работал и с
    // телом внутри домена.
    yLo = -1; yHi = -1;
    for (int y = 0; y < Ny; ++y) {
        bool fluid = false;
        for (int z = 0; z < Nz && !fluid; ++z)
            for (int x = 0; x < Nx && !fluid; ++x)
                if (!solid[idx(x, y, z)]) fluid = true;
        if (fluid) { if (yLo < 0) yLo = y; yHi = y; }
    }
    if (yLo < 0 || yHi <= yLo) return false;
    H = (float)(yHi - yLo + 1);

    thermalNu = nuLB;
    thermalPrandtl = std::max(0.05f, thermalParams.prandtl);
    thermalAlpha = thermalNu / thermalPrandtl;
    // tau_T из alpha = cs^2 (tau - 1/2); предел устойчивости явной схемы —
    // tau > 1/2, поэтому alpha > 0. Верхняя граница 1.5 — как у решателя.
    tauT = 0.5f + thermalAlpha / kCs2;
    if (!std::isfinite(tauT) || tauT < 0.505f) tauT = 0.505f;
    if (tauT > 1.5f) {
        // Просить Pr, который решётка не держит, — это подмена: alpha
        // пересчитывается из максимально допустимого tau, и это пишется в
        // журнал, а не замалчивается.
        tauT = 1.5f;
        thermalAlpha = kCs2 * (tauT - 0.5f);
        thermalPrandtl = thermalNu / thermalAlpha;
        DBG_PHYS("тепло: запрошенный Pr %.2f требует tau_T > 1.5 (предел устойчивости) — "
                 "взят Pr = %.2f, alpha = %.4f", thermalParams.prandtl, thermalPrandtl, thermalAlpha);
    }

    // g = Ra nu alpha / (beta dT H^3); beta = 1 (безразмерная).
    const float dT = thermalParams.hotTemp - thermalParams.coldTemp;
    thermalGravityLb = thermalParams.rayleigh * thermalNu * thermalAlpha /
                       (std::max(1e-6f, std::fabs(dT)) * H * H * H);
    thermalRayleigh = thermalGravityLb * std::fabs(dT) * H * H * H / (thermalNu * thermalAlpha);

    thermalT.assign((size_t)Nx*Ny*Nz, 0.5f * (thermalParams.hotTemp + thermalParams.coldTemp));
    thermalBuoyancyY.assign((size_t)Nx*Ny*Nz, 0.0f);
    g.assign((size_t)Nx*Ny*Nz*kQ, 0.0f);
    gNext.assign((size_t)Nx*Ny*Nz*kQ, 0.0f);

    thermalResetField();
    thermalReady = true;
    thermalLogSummary();
    return true;
}

void thermalShutdown() {
    thermalReady = false;
    Nx = Ny = Nz = 0;
    thermalT.clear();
    thermalBuoyancyY.clear();
    g.clear();
    gNext.clear();
    solid.clear();
    thermalMeanT = 0.5f;
    thermalNusselt = 0.0f;
}

void thermalResetField() {
    if (Nx <= 0) return;
    const float Th = thermalParams.hotTemp;
    const float Tc = thermalParams.coldTemp;
    const float dT = Th - Tc;
    const float amp = thermalParams.perturbation * dT;
    for (int z = 0; z < Nz; ++z)
        for (int y = 0; y < Ny; ++y)
            for (int x = 0; x < Nx; ++x) {
                const int c = idx(x, y, z);
                float T;
                if (solid[c]) {
                    float Tw;
                    T = wallTemp(y, &Tw) ? Tw : 0.5f * (Th + Tc);
                } else {
                    const float s = (float)(y - yLo) / std::max(1.0f, H - 1.0f);
                    T = Th + (Tc - Th) * s;
                    // Возмущение, которое раскачивает конвекцию: одна ячейка
                    // по x и полуволна по высоте (классическая затравка).
                    const float px = 6.2831853f * (float)x / (float)Nx;
                    const float py = 3.1415926f * s;
                    T += amp * std::cos(px) * std::sin(py);
                }
                thermalT[c] = T;
            }
    // Популяции — в равновесие с нулевой скоростью.
    for (int c = 0; c < Nx*Ny*Nz; ++c) {
        const float T = thermalT[c];
        for (int i = 0; i < kQ; ++i) g[(size_t)c*kQ + i] = geq(i, T, 0, 0, 0);
    }
    computeDiagnostics();
}

bool thermalUpload(const std::vector<float>& T) {
    if (!thermalReady) return false;
    if (T.size() != thermalT.size()) return false;
    thermalT = T;
    for (int c = 0; c < Nx*Ny*Nz; ++c) {
        const float Tc = thermalT[c];
        for (int i = 0; i < kQ; ++i) g[(size_t)c*kQ + i] = geq(i, Tc, 0, 0, 0);
    }
    computeDiagnostics();
    return true;
}

void thermalStep(const float* ux, const float* uy, const float* uz) {
    if (!thermalReady || !thermalParams.enabled) return;
    if (!ux || !uy || !uz) return;

    const float invTau = 1.0f / tauT;

    // --- Перенос (pull) + границы -------------------------------------
    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (int z = 0; z < Nz; ++z) {
        for (int y = 0; y < Ny; ++y) {
            for (int x = 0; x < Nx; ++x) {
                const int c = idx(x, y, z);
                if (solid[c]) {
                    for (int i = 0; i < kQ; ++i) gNext[(size_t)c*kQ + i] = g[(size_t)c*kQ + i];
                    continue;
                }
                // Скорость в этой ячейке; на твёрдых она нулевая, поэтому
                // равновесие там не считается.
                for (int i = 0; i < kQ; ++i) {
                    int sx = x - kc[i][0];
                    int sy = y - kc[i][1];
                    int sz = z - kc[i][2];
                    const bool outX = (sx < 0 || sx >= Nx);
                    const bool outY = (sy < 0 || sy >= Ny);
                    const bool outZ = (sz < 0 || sz >= Nz);
                    if (outX) sx = (sx + Nx) % Nx;      // периодичность по X
                    if (outZ) sz = (sz + Nz) % Nz;      // периодичность по Z

                    float Tw = 0.0f;
                    bool dirichlet = false;
                    bool bounce = false;
                    if (outY) {
                        bounce = true;
                        dirichlet = wallTemp(sy < 0 ? 0 : Ny - 1, &Tw);
                    } else if (solid[idx(sx, sy, sz)]) {
                        bounce = true;
                        dirichlet = wallTemp(sy, &Tw);
                    }
                    if (bounce) {
                        const float gOpp = g[(size_t)c*kQ + kOpp[i]];
                        gNext[(size_t)c*kQ + i] = dirichlet
                            ? (-gOpp + 2.0f * kw[i] * Tw)
                            : gOpp;
                    } else {
                        gNext[(size_t)c*kQ + i] = g[(size_t)idx(sx, sy, sz)*kQ + i];
                    }
                }
            }
        }
    }

    // --- Столкновение BGK для скаляра и сила плавучести ---------------
    const float Tref = 0.5f * (thermalParams.hotTemp + thermalParams.coldTemp);
    const float gLb = thermalGravityLb;
    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (int z = 0; z < Nz; ++z) {
        for (int y = 0; y < Ny; ++y) {
            for (int x = 0; x < Nx; ++x) {
                const int c = idx(x, y, z);
                if (solid[c]) {
                    thermalBuoyancyY[c] = 0.0f;
                    continue;
                }
                const float vx = ux[c], vy = uy[c], vz = uz[c];
                float T = 0.0f;
                for (int i = 0; i < kQ; ++i) T += gNext[(size_t)c*kQ + i];
                if (!std::isfinite(T)) T = Tref;
                T = std::min(thermalParams.hotTemp + 1.0f,
                             std::max(thermalParams.coldTemp - 1.0f, T));
                for (int i = 0; i < kQ; ++i) {
                    const float gi = gNext[(size_t)c*kQ + i];
                    const float eq = geq(i, T, vx, vy, vz);
                    gNext[(size_t)c*kQ + i] = gi - (gi - eq) * invTau;
                }
                thermalT[c] = T;
                // Плавучесть: только отклонение от средней температуры.
                thermalBuoyancyY[c] = gLb * (T - Tref);
            }
        }
    }

    // --- Число Нуссельта: поток у нижней стенки -----------------------
    // q = alpha dT/dy, расстояние от стенки до центра первой ячейки — 0.5.
    double qSum = 0.0; long qN = 0;
    for (int z = 0; z < Nz; ++z)
        for (int x = 0; x < Nx; ++x) {
            const int c = idx(x, yLo, z);
            if (solid[c]) continue;
            qSum += (thermalParams.hotTemp - thermalT[c]) / 0.5;
            ++qN;
        }
    if (qN > 0) {
        const float dT = std::max(1e-6f, std::fabs(thermalParams.hotTemp - thermalParams.coldTemp));
        thermalNusselt = (float)(qSum / (double)qN) * H / dT;
    }
    computeDiagnostics();

    g.swap(gNext);
}

void thermalLogSummary() {
    if (!thermalReady) return;
    DBG_PHYS("тепло: D3Q7, Ra=%.1f, Pr=%.2f (nu=%.4f, alpha=%.4f, tau_T=%.4f), "
             "g_lb=%.3e, H=%.1f ячеек, стенки: %s",
             thermalRayleigh, thermalPrandtl, thermalNu, thermalAlpha, tauT,
             thermalGravityLb, H,
             thermalParams.fixedWallTemps ? "нижняя горячая / верхняя холодная" : "адиабатические");
}
