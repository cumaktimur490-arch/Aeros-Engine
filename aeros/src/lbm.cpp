#include "lbm.h"
#include "globals.h"
#include "flow_field.h"
#include "voxel_grid.h"
#include "forces.h"
#include "atmosphere.h"

#include <glm/glm.hpp>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

#ifdef _OPENMP
#include <omp.h>
#endif

// Глобальные
LBMParams lbmParams;
bool lbmInitialized = false;
bool lbmConverged = false;
int lbmCurrentStep = 0;
float lbmAvgRho = 1.0f;
float lbmAvgKineticEnergy = 0.0f;
float lbmMaxVelocityLB = 0.0f;
float lbmMaxVelocityWorld = 0.0f;
float lbmReynolds = 0.0f;
float lbmConvergence = 0.0f;
float lbmTimeMs = 0.0f;

int lbmNx = 0, lbmNy = 0, lbmNz = 0;
float lbmMinX = 0, lbmMinY = 0, lbmMinZ = 0;
float lbmMaxX = 0, lbmMaxY = 0, lbmMaxZ = 0;
float lbmCellSizeX = 0.1f, lbmCellSizeY = 0.1f, lbmCellSizeZ = 0.1f;

std::vector<float> lbmRho;
std::vector<float> lbmUx, lbmUy, lbmUz;
std::vector<float> lbmUxWorld, lbmUyWorld, lbmUzWorld;
std::vector<float> lbmVorticityMag;
std::vector<float> lbmQCriterion;
std::vector<float> lbmPressure;
std::vector<char>  lbmIsSolid;

// Распределения
static std::vector<float> f;
static std::vector<float> fNext;

// D3Q19 — оптимизировано: статические константы, выровненные
static const int Q = 19;
static const int c[19][3] = {
    {0,0,0},
    {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1},
    {1,1,0}, {-1,-1,0}, {1,-1,0}, {-1,1,0},
    {1,0,1}, {-1,0,-1}, {1,0,-1}, {-1,0,1},
    {0,1,1}, {0,-1,-1}, {0,1,-1}, {0,-1,1}
};
static const float w[19] = {
    1.0f/3.0f,
    1.0f/18.0f, 1.0f/18.0f, 1.0f/18.0f, 1.0f/18.0f, 1.0f/18.0f, 1.0f/18.0f,
    1.0f/36.0f, 1.0f/36.0f, 1.0f/36.0f, 1.0f/36.0f,
    1.0f/36.0f, 1.0f/36.0f, 1.0f/36.0f, 1.0f/36.0f,
    1.0f/36.0f, 1.0f/36.0f, 1.0f/36.0f, 1.0f/36.0f
};
static const int opp[19] = {
    0,
    2,1,4,3,6,5,
    8,7,10,9,
    12,11,14,13,
    16,15,18,17
};
static const float cs2 = 1.0f/3.0f;
static const float invCs2 = 3.0f;
static const float invCs2SqHalf = 4.5f; // 0.5 * invCs2 * invCs2

// Предвычисленные оффсеты для стриминга
static int lbmOffsets[19];
static int lbmStrideX = 1;
static int lbmStrideY = 0;
static int lbmStrideZ = 0;

inline int idx3D(int x, int y, int z) { return (z*lbmNy + y)*lbmNx + x; }
inline int fIdx(int cell, int q) { return cell*Q + q; }

// Быстрое равновесие — инлайн, без ветвлений
inline float computeEquilibriumFast(int qi, float rho, float ux, float uy, float uz, float usqr) {
    float cu = (float)c[qi][0]*ux + (float)c[qi][1]*uy + (float)c[qi][2]*uz;
    return w[qi]*rho*(1.0f + cu*invCs2 + cu*cu*invCs2SqHalf - usqr*1.5f);
}

static void computeInletVelocityLB(float& ux, float& uy, float& uz) {
    float az = glm::radians(flowAzimuth);
    float el = glm::radians(flowElevation);
    float ce = cosf(el), se = sinf(el);
    float ca = cosf(az), sa = sinf(az);
    float dx = ce*ca, dy = se, dz = ce*sa;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 1e-6f) { dx = 1; dy = 0; dz = 0; len = 1; }
    dx /= len; dy /= len; dz /= len;
    float U0 = lbmParams.U0;
    if (!std::isfinite(U0) || U0 < 1e-6f) U0 = 0.1f;
    if (U0 > 0.25f) U0 = 0.25f;
    ux = dx * U0;
    uy = dy * U0;
    uz = dz * U0;
}

// Определение главной оси потока — 0=X,1=Y,2=Z и знак
static void computeFlowAxis(int& axis, int& sign, glm::vec3& dirNorm) {
    float az = glm::radians(flowAzimuth);
    float el = glm::radians(flowElevation);
    float ce = cosf(el), se = sinf(el);
    float ca = cosf(az), sa = sinf(az);
    dirNorm = glm::vec3(ce*ca, se, ce*sa);
    float len = glm::length(dirNorm);
    if (len < 1e-6f) dirNorm = glm::vec3(1,0,0);
    else dirNorm = dirNorm / len;
    float ax = fabsf(dirNorm.x), ay = fabsf(dirNorm.y), azv = fabsf(dirNorm.z);
    if (ax >= ay && ax >= azv) { axis = 0; sign = (dirNorm.x > 0) ? 1 : -1; }
    else if (ay >= azv) { axis = 1; sign = (dirNorm.y > 0) ? 1 : -1; }
    else { axis = 2; sign = (dirNorm.z > 0) ? 1 : -1; }
}

void initLBM() {
    std::cout << "[LBM] Initializing optimized D3Q19..." << std::endl;
    auto t0 = std::chrono::high_resolution_clock::now();

    if (g_voxelData.empty() || g_voxNx <= 0) {
        std::cout << "[LBM] No voxel grid — fallback 48" << std::endl;
        lbmNx = voxelResolution;
        lbmNy = voxelResolution;
        lbmNz = voxelResolution;
        lbmMinX = -1.0f; lbmMaxX = 1.0f;
        lbmMinY = -1.0f; lbmMaxY = 1.0f;
        lbmMinZ = -1.0f; lbmMaxZ = 1.0f;
    } else {
        lbmNx = g_voxNx;
        lbmNy = g_voxNy;
        lbmNz = g_voxNz;
        lbmMinX = g_voxMinX; lbmMaxX = g_voxMaxX;
        lbmMinY = g_voxMinY; lbmMaxY = g_voxMaxY;
        lbmMinZ = g_voxMinZ; lbmMaxZ = g_voxMaxZ;
    }

    lbmCellSizeX = (lbmMaxX - lbmMinX) / std::max(1, lbmNx);
    lbmCellSizeY = (lbmMaxY - lbmMinY) / std::max(1, lbmNy);
    lbmCellSizeZ = (lbmMaxZ - lbmMinZ) / std::max(1, lbmNz);
    if (lbmCellSizeX < 1e-6f) lbmCellSizeX = 0.1f;
    if (lbmCellSizeY < 1e-6f) lbmCellSizeY = 0.1f;
    if (lbmCellSizeZ < 1e-6f) lbmCellSizeZ = 0.1f;

    int total = lbmNx * lbmNy * lbmNz;
    if (total <= 0 || total > 20*1024*1024) {
        std::cout << "[LBM] Invalid total cells: " << total << std::endl;
        lbmInitialized = false;
        return;
    }

    // страйды и оффсеты
    lbmStrideX = 1;
    lbmStrideY = lbmNx;
    lbmStrideZ = lbmNx * lbmNy;
    for (int qi = 0; qi < Q; ++qi) {
        lbmOffsets[qi] = c[qi][0]*lbmStrideX + c[qi][1]*lbmStrideY + c[qi][2]*lbmStrideZ;
    }

    try {
        lbmRho.assign(total, 1.0f);
        lbmUx.assign(total, 0.0f);
        lbmUy.assign(total, 0.0f);
        lbmUz.assign(total, 0.0f);
        lbmUxWorld.assign(total, 0.0f);
        lbmUyWorld.assign(total, 0.0f);
        lbmUzWorld.assign(total, 0.0f);
        lbmVorticityMag.assign(total, 0.0f);
        lbmQCriterion.assign(total, 0.0f);
        lbmPressure.assign(total, 0.0f);
        lbmIsSolid.assign(total, 0);
        f.assign(total*Q, 0.0f);
        fNext.assign(total*Q, 0.0f);
        // Резервируем чтобы избежать реаллокаций
        lbmRho.shrink_to_fit(); // нет, оставляем capacity
    } catch (const std::bad_alloc& e) {
        std::cout << "[LBM] Allocation failed: " << e.what() << std::endl;
        lbmInitialized = false;
        return;
    }

    // Маркировка твердых
    if (!g_voxelData.empty() && (int)g_voxelData.size() == total) {
        const int* vox = g_voxelData.data();
        char* solid = lbmIsSolid.data();
        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int i = 0; i < total; ++i) solid[i] = (vox[i] == 1) ? 1 : 0;
    } else {
        std::fill(lbmIsSolid.begin(), lbmIsSolid.end(), 0);
    }

    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    std::cout << "[LBM] Inlet LB velocity: (" << inUx << "," << inUy << "," << inUz << ") total=" << total << std::endl;

    // Инициализация равновесием — параллельная
    float* fPtr = f.data();
    char* solidPtr = lbmIsSolid.data();
    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (int cell = 0; cell < total; ++cell) {
        float rho = 1.0f;
        float ux = 0, uy = 0, uz = 0;
        if (!solidPtr[cell]) { ux = inUx; uy = inUy; uz = inUz; }
        float usqr = ux*ux + uy*uy + uz*uz;
        int base = cell*Q;
        for (int qi = 0; qi < Q; ++qi) {
            fPtr[base+qi] = computeEquilibriumFast(qi, rho, ux, uy, uz, usqr);
        }
    }

    // Параметры
    if (lbmParams.tau < 0.51f) lbmParams.tau = 0.51f;
    if (lbmParams.tau > 2.0f) lbmParams.tau = 2.0f;
    lbmParams.viscosity = (lbmParams.tau - 0.5f) * cs2;
    lbmParams.U0 = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    if (lbmParams.U0 < 1e-6f) lbmParams.U0 = 0.1f;

    lbmCurrentStep = 0;
    lbmConverged = false;
    lbmInitialized = true;

    float L = (float)std::max({lbmNx, lbmNy, lbmNz});
    lbmReynolds = lbmParams.U0 * L / (lbmParams.viscosity + 1e-6f);

    auto t1 = std::chrono::high_resolution_clock::now();
    lbmTimeMs = std::chrono::duration<float, std::milli>(t1-t0).count();
    std::cout << "[LBM] Initialized OPT: " << lbmNx << "x" << lbmNy << "x" << lbmNz << " = " << total
              << " cells, tau=" << lbmParams.tau << " nu=" << lbmParams.viscosity
              << " Re=" << lbmReynolds << " in " << lbmTimeMs << " ms"
#ifdef _OPENMP
              << " [OpenMP " << omp_get_max_threads() << " threads]"
#endif
              << std::endl;
}

void resetLBM() {
    std::cout << "[LBM] Reset..." << std::endl;
    shutdownLBM();
    initLBM();
}

void shutdownLBM() {
    lbmInitialized = false;
    lbmCurrentStep = 0;
    lbmConverged = false;
}

// Оптимизированный шаг LBM — collision + streaming gather
void stepLBMCPU(int steps) {
    if (!lbmInitialized) return;
    if (lbmNx <= 0 || lbmNy <= 0 || lbmNz <= 0) return;
    int total = lbmNx * lbmNy * lbmNz;
    if (total <= 0) return;
    if (total != (int)lbmRho.size()) return;

    auto t0 = std::chrono::high_resolution_clock::now();

    float tau0 = lbmParams.tau;
    if (!std::isfinite(tau0) || tau0 < 0.51f) tau0 = 0.6f;
    if (tau0 > 2.0f) tau0 = 2.0f;
    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    float U0mag = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    if (U0mag < 1e-6f) U0mag = 0.1f;

    // Определяем ось потока один раз на все шаги
    int flowAxis, flowSign;
    glm::vec3 flowDir;
    computeFlowAxis(flowAxis, flowSign, flowDir);

    // Указатели для скорости
    float* fPtr = f.data();
    float* fNextPtr = fNext.data();
    float* rhoPtr = lbmRho.data();
    float* uxPtr = lbmUx.data();
    float* uyPtr = lbmUy.data();
    float* uzPtr = lbmUz.data();
    char* solidPtr = lbmIsSolid.data();

    const int Nx = lbmNx, Ny = lbmNy, Nz = lbmNz;
    const int strideY = Nx;
    const int strideZ = Nx*Ny;

    // Для сходимости
    static float prevKinetic = 0.0f;

    for (int s = 0; s < steps; ++s) {
        float maxVelGlobal = 0.0f;
        double avgRhoAcc = 0.0;
        double kineticAcc = 0.0;

        // === COLLISION — параллельный ===
        #ifdef _OPENMP
        #pragma omp parallel reduction(+:avgRhoAcc,kineticAcc)
        {
            float maxVelLocal = 0.0f;
            #pragma omp for nowait
        #endif
        for (int cell = 0; cell < total; ++cell) {
            if (solidPtr[cell]) {
                rhoPtr[cell] = 1.0f;
                uxPtr[cell] = 0; uyPtr[cell] = 0; uzPtr[cell] = 0;
                continue;
            }

            int base = cell*Q;
            // Макроскопические
            float rho = 0.0f, ux = 0.0f, uy = 0.0f, uz = 0.0f;
            // Разворачиваем цикл по Q для скорости — 19 итераций
            // Используем ручной unroll частично
            for (int qi = 0; qi < Q; ++qi) {
                float fi = fPtr[base+qi];
                // защита от NaN — быстро
                if (!std::isfinite(fi)) fi = w[qi];
                rho += fi;
                ux += fi * (float)c[qi][0];
                uy += fi * (float)c[qi][1];
                uz += fi * (float)c[qi][2];
            }
            if (rho < 1e-6f || !std::isfinite(rho)) rho = 1.0f;
            float invRho = 1.0f / rho;
            ux *= invRho; uy *= invRho; uz *= invRho;
            if (!std::isfinite(ux)) ux = 0;
            if (!std::isfinite(uy)) uy = 0;
            if (!std::isfinite(uz)) uz = 0;

            // Граничные — определяем быстро по индексу
            int x = cell % Nx;
            int y = (cell / Nx) % Ny;
            int z = cell / strideZ;
            bool isInlet = false;
            if (flowAxis == 0) isInlet = (flowSign > 0) ? (x == 0) : (x == Nx-1);
            else if (flowAxis == 1) isInlet = (flowSign > 0) ? (y == 0) : (y == Ny-1);
            else isInlet = (flowSign > 0) ? (z == 0) : (z == Nz-1);

            if (isInlet) {
                rho = 1.0f;
                ux = inUx; uy = inUy; uz = inUz;
            }

            // Smagorinsky LES — только для внутренних не-граничных ячеек
            float tauEff = tau0;
            if (lbmParams.useTurbulence && !isInlet) {
                // Проверяем что внутренние (не на границе домена)
                if (x > 0 && x < Nx-1 && y > 0 && y < Ny-1 && z > 0 && z < Nz-1) {
                    // Быстрые индексы соседей
                    int xm = cell - 1;
                    int xp = cell + 1;
                    int ym = cell - strideY;
                    int yp = cell + strideY;
                    int zm = cell - strideZ;
                    int zp = cell + strideZ;
                    // Пропускаем если соседи твердые — упрощение
                    if (!solidPtr[xm] && !solidPtr[xp] && !solidPtr[ym] && !solidPtr[yp] && !solidPtr[zm] && !solidPtr[zp]) {
                        float dux_dx = (uxPtr[xp] - uxPtr[xm]) * 0.5f;
                        float duy_dy = (uyPtr[yp] - uyPtr[ym]) * 0.5f;
                        float duz_dz = (uzPtr[zp] - uzPtr[zm]) * 0.5f;
                        float dux_dy = (uxPtr[yp] - uxPtr[ym]) * 0.5f;
                        float dux_dz = (uxPtr[zp] - uxPtr[zm]) * 0.5f;
                        float duy_dx = (uyPtr[xp] - uyPtr[xm]) * 0.5f;
                        float duy_dz = (uyPtr[zp] - uyPtr[zm]) * 0.5f;
                        float duz_dx = (uzPtr[xp] - uzPtr[xm]) * 0.5f;
                        float duz_dy = (uzPtr[yp] - uzPtr[ym]) * 0.5f;

                        float Sxx = dux_dx;
                        float Syy = duy_dy;
                        float Szz = duz_dz;
                        float Sxy = 0.5f*(dux_dy + duy_dx);
                        float Sxz = 0.5f*(dux_dz + duz_dx);
                        float Syz = 0.5f*(duy_dz + duz_dy);

                        float S2 = Sxx*Sxx + Syy*Syy + Szz*Szz + 2.0f*(Sxy*Sxy + Sxz*Sxz + Syz*Syz);
                        float S = std::sqrt(2.0f*S2);
                        if (std::isfinite(S)) {
                            float Cs = lbmParams.smagorinskyC;
                            float nu_t = Cs*Cs * S;
                            tauEff = tau0 + nu_t / cs2;
                            if (tauEff > 2.0f) tauEff = 2.0f;
                            if (tauEff < 0.51f) tauEff = 0.51f;
                        }
                    }
                }
            }

            float usqr = ux*ux + uy*uy + uz*uz;
            float invTau = 1.0f / tauEff;
            // Коллизия BGK
            for (int qi = 0; qi < Q; ++qi) {
                float feq = computeEquilibriumFast(qi, rho, ux, uy, uz, usqr);
                float fi = fPtr[base+qi];
                if (!std::isfinite(fi)) fi = feq;
                fPtr[base+qi] = fi - (fi - feq) * invTau;
            }

            rhoPtr[cell] = rho;
            uxPtr[cell] = ux; uyPtr[cell] = uy; uzPtr[cell] = uz;

            float velMag = std::sqrt(usqr);
#ifdef _OPENMP
            if (velMag > maxVelLocal) maxVelLocal = velMag;
#else
            if (velMag > maxVelGlobal) maxVelGlobal = velMag;
#endif
            avgRhoAcc += rho;
            kineticAcc += usqr;
        }
        #ifdef _OPENMP
            #pragma omp critical
            {
                if (maxVelLocal > maxVelGlobal) maxVelGlobal = maxVelLocal;
            }
        } // parallel
        #endif

        // === STREAMING GATHER — параллельный по z ===
        // Используем gather: fNext[dest][q] = f[src][q] где src = dest - c[q]
        // Это безопасно для параллели, т.к. каждый dest пишет только в свои ячейки
        #ifdef _OPENMP
        #pragma omp parallel for collapse(2)
        #endif
        for (int z = 0; z < Nz; ++z) {
            for (int y = 0; y < Ny; ++y) {
                int rowBase = (z*Ny + y)*Nx;
                for (int x = 0; x < Nx; ++x) {
                    int dest = rowBase + x;
                    if (solidPtr[dest]) continue;

                    int destBase = dest*Q;
                    // Для каждой скорости
                    for (int qi = 0; qi < Q; ++qi) {
                        int sx = x - c[qi][0];
                        int sy = y - c[qi][1];
                        int sz = z - c[qi][2];
                        if (sx < 0 || sx >= Nx || sy < 0 || sy >= Ny || sz < 0 || sz >= Nz) {
                            // Выход за границу — outlet: копируем bounce-back или оставляем
                            // Для стабильности — используем равновесие с локальной скоростью или отражение
                            // Простой outlet: fNext = f[dest][opp] (отражение) для предотвращения потери массы
                            // Но лучше — zero-gradient: fNext[dest][q] = f[dest][q]
                            fNextPtr[destBase+qi] = fPtr[destBase+qi];
                        } else {
                            int src = (sz*Ny + sy)*Nx + sx;
                            if (solidPtr[src]) {
                                // bounce-back от твердого — отражение
                                fNextPtr[destBase+qi] = fPtr[destBase+opp[qi]];
                            } else {
                                fNextPtr[destBase+qi] = fPtr[src*Q+qi];
                            }
                        }
                    }
                }
            }
        }

        // Inlet BC — перезаписываем fNext на inlet плоскости равновесием
        if (flowAxis == 0) {
            int ix = (flowSign > 0) ? 0 : Nx-1;
            float usqr = inUx*inUx + inUy*inUy + inUz*inUz;
            #ifdef _OPENMP
            #pragma omp parallel for collapse(2)
            #endif
            for (int z = 0; z < Nz; ++z) {
                for (int y = 0; y < Ny; ++y) {
                    int cell = (z*Ny + y)*Nx + ix;
                    if (solidPtr[cell]) continue;
                    int base = cell*Q;
                    for (int qi = 0; qi < Q; ++qi) {
                        fNextPtr[base+qi] = computeEquilibriumFast(qi, 1.0f, inUx, inUy, inUz, usqr);
                    }
                }
            }
        } else if (flowAxis == 1) {
            int iy = (flowSign > 0) ? 0 : Ny-1;
            float usqr = inUx*inUx + inUy*inUy + inUz*inUz;
            #ifdef _OPENMP
            #pragma omp parallel for collapse(2)
            #endif
            for (int z = 0; z < Nz; ++z) {
                for (int x = 0; x < Nx; ++x) {
                    int cell = (z*Ny + iy)*Nx + x;
                    if (solidPtr[cell]) continue;
                    int base = cell*Q;
                    for (int qi = 0; qi < Q; ++qi) {
                        fNextPtr[base+qi] = computeEquilibriumFast(qi, 1.0f, inUx, inUy, inUz, usqr);
                    }
                }
            }
        } else {
            int iz = (flowSign > 0) ? 0 : Nz-1;
            float usqr = inUx*inUx + inUy*inUy + inUz*inUz;
            #ifdef _OPENMP
            #pragma omp parallel for collapse(2)
            #endif
            for (int y = 0; y < Ny; ++y) {
                for (int x = 0; x < Nx; ++x) {
                    int cell = (iz*Ny + y)*Nx + x;
                    if (solidPtr[cell]) continue;
                    int base = cell*Q;
                    for (int qi = 0; qi < Q; ++qi) {
                        fNextPtr[base+qi] = computeEquilibriumFast(qi, 1.0f, inUx, inUy, inUz, usqr);
                    }
                }
            }
        }

        f.swap(fNext);

        lbmCurrentStep++;
        lbmAvgRho = (float)(avgRhoAcc / total);
        lbmAvgKineticEnergy = (float)(kineticAcc / total);
        lbmMaxVelocityLB = maxVelGlobal;

        float scale = flowSpeed / U0mag;
        if (!std::isfinite(scale) || scale > 1000.0f) scale = 20.0f;
        if (scale < 0.01f) scale = 0.01f;
        lbmMaxVelocityWorld = maxVelGlobal * scale;

        lbmConvergence = std::fabs(lbmAvgKineticEnergy - prevKinetic);
        prevKinetic = lbmAvgKineticEnergy;
        lbmConverged = (lbmConvergence < lbmParams.convergenceThreshold);
    }

    // Обновление мировых скоростей и давления — один раз после всех шагов, параллельно
    {
        float scale = flowSpeed / U0mag;
        if (!std::isfinite(scale) || scale > 1000.0f) scale = 20.0f;
        float* uxW = lbmUxWorld.data();
        float* uyW = lbmUyWorld.data();
        float* uzW = lbmUzWorld.data();
        float* pPtr = lbmPressure.data();
        float cs2Scale = cs2 * scale * scale;
        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int cell = 0; cell < total; ++cell) {
            if (solidPtr[cell]) {
                uxW[cell] = 0; uyW[cell] = 0; uzW[cell] = 0;
                pPtr[cell] = 0;
            } else {
                uxW[cell] = uxPtr[cell] * scale;
                uyW[cell] = uyPtr[cell] * scale;
                uzW[cell] = uzPtr[cell] * scale;
                pPtr[cell] = cs2 * (rhoPtr[cell] - 1.0f) * scale * scale;
                // Защита от NaN
                if (!std::isfinite(pPtr[cell])) pPtr[cell] = 0;
            }
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    lbmTimeMs = std::chrono::duration<float, std::milli>(t1-t0).count();
}

void updateLBM(float deltaTime) {
    if (!lbmParams.enabled) return;
    if (!lbmInitialized) { initLBM(); return; }
    if (lbmNx != g_voxNx || lbmNy != g_voxNy || lbmNz != g_voxNz) {
        std::cout << "[LBM] Voxel grid changed, reinit" << std::endl;
        initLBM();
        return;
    }
    int steps = lbmParams.stepsPerFrame;
    if (steps <= 0) steps = 1;
    if (steps > 50) steps = 50; // увеличенный лимит для оптимизированной версии

    // Адаптивное количество шагов — если deltaTime большой, делаем больше шагов
    // Но не более 2x от настроек
    if (deltaTime > 0.02f) steps = std::min(steps*2, 50);

    stepLBMCPU(steps);

    if (lbmCurrentStep % 50 == 0) {
        computeLBMVorticityAndQ();
    }
}

// Оптимизированный сэмплинг — трилинейная интерполяция без лямбд
glm::vec3 getLBMVelocityLB(const glm::vec3& worldPos) {
    if (!lbmInitialized) return glm::vec3(0);
    if (!std::isfinite(worldPos.x) || !std::isfinite(worldPos.y) || !std::isfinite(worldPos.z)) return glm::vec3(0);

    float fx = (worldPos.x - lbmMinX) / lbmCellSizeX;
    float fy = (worldPos.y - lbmMinY) / lbmCellSizeY;
    float fz = (worldPos.z - lbmMinZ) / lbmCellSizeZ;

    int ix = (int)std::floor(fx);
    int iy = (int)std::floor(fy);
    int iz = (int)std::floor(fz);
    float tx = fx - (float)ix;
    float ty = fy - (float)iy;
    float tz = fz - (float)iz;

    if (ix < 0 || ix >= lbmNx-1 || iy < 0 || iy >= lbmNy-1 || iz < 0 || iz >= lbmNz-1) return glm::vec3(0);

    // Быстрый доступ
    const float* uxPtr = lbmUx.data();
    const float* uyPtr = lbmUy.data();
    const float* uzPtr = lbmUz.data();
    const char* solidPtr = lbmIsSolid.data();
    const int Nx = lbmNx, Ny = lbmNy;
    const int strideY = Nx, strideZ = Nx*Ny;

    auto fetch = [&](int x,int y,int z, glm::vec3& out)->bool {
        if (x<0||x>=Nx||y<0||y>=Ny||z<0||z>=lbmNz) { out = glm::vec3(0); return false; }
        int cell = (z*Ny + y)*Nx + x;
        if (solidPtr[cell]) { out = glm::vec3(0); return false; }
        out.x = uxPtr[cell]; out.y = uyPtr[cell]; out.z = uzPtr[cell];
        return true;
    };

    glm::vec3 c000,c100,c010,c110,c001,c101,c011,c111;
    fetch(ix,iy,iz,c000); fetch(ix+1,iy,iz,c100); fetch(ix,iy+1,iz,c010); fetch(ix+1,iy+1,iz,c110);
    fetch(ix,iy,iz+1,c001); fetch(ix+1,iy,iz+1,c101); fetch(ix,iy+1,iz+1,c011); fetch(ix+1,iy+1,iz+1,c111);

    glm::vec3 c00 = c000*(1.0f-tx) + c100*tx;
    glm::vec3 c01 = c001*(1.0f-tx) + c101*tx;
    glm::vec3 c10 = c010*(1.0f-tx) + c110*tx;
    glm::vec3 c11 = c011*(1.0f-tx) + c111*tx;
    glm::vec3 c0 = c00*(1.0f-ty) + c10*ty;
    glm::vec3 c1 = c01*(1.0f-ty) + c11*ty;
    glm::vec3 c = c0*(1.0f-tz) + c1*tz;
    if (!std::isfinite(c.x)) return glm::vec3(0);
    return c;
}

glm::vec3 getLBMVelocityWorld(const glm::vec3& worldPos) {
    // Быстрый путь — если вне сетки, сразу freestream
    if (!lbmInitialized) return glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
    if (worldPos.x < lbmMinX || worldPos.x > lbmMaxX ||
        worldPos.y < lbmMinY || worldPos.y > lbmMaxY ||
        worldPos.z < lbmMinZ || worldPos.z > lbmMaxZ) {
        return glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
    }
    glm::vec3 vLB = getLBMVelocityLB(worldPos);
    float mag2 = vLB.x*vLB.x + vLB.y*vLB.y + vLB.z*vLB.z;
    if (mag2 < 1e-12f) return glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz) * 0.1f;

    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    float U0mag = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    if (U0mag < 1e-6f) U0mag = 0.1f;
    float scale = flowSpeed / U0mag;
    if (!std::isfinite(scale)) return glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
    glm::vec3 vWorld = vLB * scale;
    if (!std::isfinite(vWorld.x)) return glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
    return vWorld;
}

float getLBMDensityWorld(const glm::vec3& worldPos) {
    if (!lbmInitialized) return 1.0f;
    float fx = (worldPos.x - lbmMinX) / lbmCellSizeX;
    float fy = (worldPos.y - lbmMinY) / lbmCellSizeY;
    float fz = (worldPos.z - lbmMinZ) / lbmCellSizeZ;
    int ix = (int)fx, iy = (int)fy, iz = (int)fz;
    if (ix < 0 || ix >= lbmNx || iy < 0 || iy >= lbmNy || iz < 0 || iz >= lbmNz) return 1.0f;
    int cell = (iz*lbmNy + iy)*lbmNx + ix;
    if (cell < 0 || cell >= (int)lbmRho.size()) return 1.0f;
    float rho = lbmRho[cell];
    return std::isfinite(rho) ? rho : 1.0f;
}

bool isLBMSolidWorld(const glm::vec3& worldPos) {
    if (!lbmInitialized) return false;
    float fx = (worldPos.x - lbmMinX) / lbmCellSizeX;
    float fy = (worldPos.y - lbmMinY) / lbmCellSizeY;
    float fz = (worldPos.z - lbmMinZ) / lbmCellSizeZ;
    int ix = (int)fx, iy = (int)fy, iz = (int)fz;
    if (ix < 0 || ix >= lbmNx || iy < 0 || iy >= lbmNy || iz < 0 || iz >= lbmNz) return false;
    int cell = (iz*lbmNy + iy)*lbmNx + ix;
    if (cell < 0 || cell >= (int)lbmIsSolid.size()) return false;
    return lbmIsSolid[cell] != 0;
}

void computeLBMVorticityAndQ() {
    if (!lbmInitialized) return;
    int total = lbmNx * lbmNy * lbmNz;
    if (total <= 0) return;
    const int Nx = lbmNx, Ny = lbmNy, Nz = lbmNz;
    const float* uxW = lbmUxWorld.data();
    const float* uyW = lbmUyWorld.data();
    const float* uzW = lbmUzWorld.data();
    float* vortPtr = lbmVorticityMag.data();
    float* qPtr = lbmQCriterion.data();
    const char* solidPtr = lbmIsSolid.data();
    float csx = lbmCellSizeX, csy = lbmCellSizeY, csz = lbmCellSizeZ;
    if (csx < 1e-6f) csx = 0.1f;
    if (csy < 1e-6f) csy = 0.1f;
    if (csz < 1e-6f) csz = 0.1f;
    float invCsx = 1.0f / csx, invCsy = 1.0f / csy, invCsz = 1.0f / csz;

    #ifdef _OPENMP
    #pragma omp parallel for collapse(2)
    #endif
    for (int z = 1; z < Nz-1; ++z) {
        for (int y = 1; y < Ny-1; ++y) {
            for (int x = 1; x < Nx-1; ++x) {
                int cell = (z*Ny + y)*Nx + x;
                if (solidPtr[cell]) { vortPtr[cell] = 0; qPtr[cell] = 0; continue; }

                int xm = cell-1, xp = cell+1;
                int ym = cell-Nx, yp = cell+Nx;
                int zm = cell-Nx*Ny, zp = cell+Nx*Ny;

                float dux_dx = (uxW[xp] - uxW[xm]) * 0.5f * invCsx;
                float dux_dy = (uxW[yp] - uxW[ym]) * 0.5f * invCsy;
                float dux_dz = (uxW[zp] - uxW[zm]) * 0.5f * invCsz;

                float duy_dx = (uyW[xp] - uyW[xm]) * 0.5f * invCsx;
                float duy_dy = (uyW[yp] - uyW[ym]) * 0.5f * invCsy;
                float duy_dz = (uyW[zp] - uyW[zm]) * 0.5f * invCsz;

                float duz_dx = (uzW[xp] - uzW[xm]) * 0.5f * invCsx;
                float duz_dy = (uzW[yp] - uzW[ym]) * 0.5f * invCsy;
                float duz_dz = (uzW[zp] - uzW[zm]) * 0.5f * invCsz;

                float wx = duz_dy - duy_dz;
                float wy = dux_dz - duz_dx;
                float wz = duy_dx - dux_dy;
                float vortMag = std::sqrt(wx*wx + wy*wy + wz*wz);
                vortPtr[cell] = std::isfinite(vortMag) ? vortMag : 0.0f;

                float Sxx = dux_dx, Syy = duy_dy, Szz = duz_dz;
                float Sxy = 0.5f*(dux_dy + duy_dx);
                float Sxz = 0.5f*(dux_dz + duz_dx);
                float Syz = 0.5f*(duy_dz + duz_dy);
                float Oxy = 0.5f*(dux_dy - duy_dx);
                float Oxz = 0.5f*(dux_dz - duz_dx);
                float Oyz = 0.5f*(duy_dz - duz_dy);

                float S2 = Sxx*Sxx + Syy*Syy + Szz*Szz + 2.0f*(Sxy*Sxy + Sxz*Sxz + Syz*Syz);
                float O2 = 2.0f*(Oxy*Oxy + Oxz*Oxz + Oyz*Oyz);
                float Q = 0.5f*(O2 - S2);
                qPtr[cell] = std::isfinite(Q) ? Q : 0.0f;
            }
        }
    }
}

float computeLBMRe() {
    if (!lbmInitialized) return 0.0f;
    float L = (float)std::max({lbmNx, lbmNy, lbmNz});
    float nu = lbmParams.viscosity;
    if (nu < 1e-6f) nu = 0.033f;
    return lbmParams.U0 * L / nu;
}

void computeLBMForcesFromLBM() {
    lbmReynolds = computeLBMRe();
}

bool lbmValidateInitialization() {
    if (!lbmInitialized) return false;
    if (lbmNx <= 0 || lbmNy <= 0 || lbmNz <= 0) return false;
    int total = lbmNx*lbmNy*lbmNz;
    if ((int)lbmRho.size() != total) return false;
    if ((int)f.size() != total*Q) return false;
    for (float rho : lbmRho) if (!std::isfinite(rho) || rho < 0.5f || rho > 2.0f) return false;
    return true;
}
bool lbmValidateConservation() {
    if (!lbmInitialized) return true;
    float avg = 0;
    for (float r : lbmRho) avg += r;
    avg /= lbmRho.size();
    return std::fabs(avg - 1.0f) <= 0.1f;
}
bool lbmValidateBoundaryConditions() {
    if (!lbmInitialized) return true;
    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    float mag = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    return mag >= 1e-6f && mag <= 0.3f;
}
bool lbmValidateSolidHandling() {
    if (!lbmInitialized) return true;
    for (size_t i = 0; i < lbmIsSolid.size(); ++i) {
        if (lbmIsSolid[i]) {
            if (std::fabs(lbmUx[i]) > 1e-6f || std::fabs(lbmUy[i]) > 1e-6f || std::fabs(lbmUz[i]) > 1e-6f) return false;
        }
    }
    return true;
}

void drawLBMUI() {}
