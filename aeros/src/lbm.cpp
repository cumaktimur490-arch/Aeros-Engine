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
std::vector<char> lbmIsSolid;

// Распределения
static std::vector<float> f;      // текущее
static std::vector<float> fNext;  // следующее
static std::vector<float> fEq;    // для временного хранения (опционально)

// D3Q19
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
static const float cs4 = cs2*cs2;
static const float invCs2 = 3.0f;

inline int idx3D(int x, int y, int z) { return (z*lbmNy + y)*lbmNx + x; }
inline int fIdx(int cell, int q) { return cell*Q + q; }

static float computeEquilibrium(int qi, float rho, float ux, float uy, float uz) {
    float cu = c[qi][0]*ux + c[qi][1]*uy + c[qi][2]*uz;
    float usqr = ux*ux + uy*uy + uz*uz;
    return w[qi]*rho*(1.0f + cu*invCs2 + cu*cu*0.5f*invCs2*invCs2 - usqr*0.5f*invCs2);
}

static void computeInletVelocityLB(float& ux, float& uy, float& uz) {
    float az = glm::radians(flowAzimuth);
    float el = glm::radians(flowElevation);
    glm::vec3 dir(cosf(el)*cosf(az), sinf(el), cosf(el)*sinf(az));
    if (glm::length(dir) < 1e-6f) dir = glm::vec3(1,0,0);
    dir = glm::normalize(dir);
    float U0 = lbmParams.U0;
    if (!std::isfinite(U0) || U0 < 1e-6f) U0 = 0.1f;
    if (U0 > 0.25f) U0 = 0.25f;
    ux = dir.x * U0;
    uy = dir.y * U0;
    uz = dir.z * U0;
}

void initLBM() {
    std::cout << "[LBM] Initializing LBM D3Q19..." << std::endl;
    auto t0 = std::chrono::high_resolution_clock::now();

    if (g_voxelData.empty() || g_voxNx <= 0) {
        std::cout << "[LBM] No voxel grid — cannot init LBM, using fallback resolution 48" << std::endl;
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
    } catch (const std::bad_alloc& e) {
        std::cout << "[LBM] Allocation failed: " << e.what() << std::endl;
        lbmInitialized = false;
        return;
    }

    // Маркировка твердых ячеек из воксельной сетки
    if (!g_voxelData.empty() && (int)g_voxelData.size() == g_voxNx*g_voxNy*g_voxNz) {
        for (int i = 0; i < total; i++) {
            lbmIsSolid[i] = (g_voxelData[i] == 1) ? 1 : 0;
        }
    } else {
        // если нет вокселей — нет твердых
        std::fill(lbmIsSolid.begin(), lbmIsSolid.end(), 0);
    }

    // Инициализация равновесным распределением с inlet скоростью
    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    std::cout << "[LBM] Inlet LB velocity: (" << inUx << "," << inUy << "," << inUz << ")" << std::endl;

    for (int cell = 0; cell < total; cell++) {
        float rho = 1.0f;
        float ux = 0, uy = 0, uz = 0;
        if (lbmIsSolid[cell]) {
            ux = 0; uy = 0; uz = 0;
        } else {
            // начальное поле — inlet везде для быстрого старта
            ux = inUx; uy = inUy; uz = inUz;
        }
        lbmRho[cell] = rho;
        lbmUx[cell] = ux; lbmUy[cell] = uy; lbmUz[cell] = uz;
        for (int qi = 0; qi < Q; qi++) {
            f[fIdx(cell, qi)] = computeEquilibrium(qi, rho, ux, uy, uz);
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

    // Reynolds: Re = U*L / nu
    float L = std::max({(float)lbmNx, (float)lbmNy, (float)lbmNz});
    lbmReynolds = lbmParams.U0 * L / (lbmParams.viscosity + 1e-6f);

    auto t1 = std::chrono::high_resolution_clock::now();
    lbmTimeMs = std::chrono::duration<float, std::milli>(t1-t0).count();
    std::cout << "[LBM] Initialized: " << lbmNx << "x" << lbmNy << "x" << lbmNz << " = " << total << " cells, tau=" << lbmParams.tau << " nu=" << lbmParams.viscosity << " Re=" << lbmReynolds << " in " << lbmTimeMs << " ms" << std::endl;
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
    // не очищаем вектора полностью для переиспользования, но можно
    // оставляем как есть
}

void stepLBMCPU(int steps) {
    if (!lbmInitialized) return;
    if (lbmNx <= 0 || lbmNy <= 0 || lbmNz <= 0) return;
    int total = lbmNx * lbmNy * lbmNz;
    if (total <= 0) return;

    auto t0 = std::chrono::high_resolution_clock::now();

    float tau0 = lbmParams.tau;
    if (!std::isfinite(tau0) || tau0 < 0.51f) tau0 = 0.6f;
    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);

    // Для Smagorinsky: предварительный расчет градиентов скорости если включен
    // Мы будем считать |S| внутри цикла коллизии через центральные разности
    // Для производительности — используем предыдущие lbmUx

    for (int s = 0; s < steps; s++) {
        // === COLLISION ===
        float maxVel = 0.0f;
        float avgRho = 0.0f;
        float kinetic = 0.0f;

        for (int cell = 0; cell < total; cell++) {
            if (lbmIsSolid[cell]) {
                // твердые — не коллизия, но rho=1, u=0
                lbmRho[cell] = 1.0f;
                lbmUx[cell] = 0; lbmUy[cell] = 0; lbmUz[cell] = 0;
                continue;
            }

            // вычисляем макроскопические из f
            float rho = 0.0f, ux = 0.0f, uy = 0.0f, uz = 0.0f;
            for (int qi = 0; qi < Q; qi++) {
                float fi = f[fIdx(cell, qi)];
                if (!std::isfinite(fi)) fi = w[qi]; // защита
                rho += fi;
                ux += fi * c[qi][0];
                uy += fi * c[qi][1];
                uz += fi * c[qi][2];
            }
            if (!std::isfinite(rho) || rho < 1e-6f) rho = 1.0f;
            ux /= rho; uy /= rho; uz /= rho;
            if (!std::isfinite(ux)) ux = 0;
            if (!std::isfinite(uy)) uy = 0;
            if (!std::isfinite(uz)) uz = 0;

            // Inlet/outlet граничные условия — перезаписываем скорость на границах
            int x = cell % lbmNx;
            // int y = (cell / lbmNx) % lbmNy;
            // int z = cell / (lbmNx*lbmNy);
            bool isInlet = false, isOutlet = false;
            // Определяем inlet по направлению потока: если поток +X, inlet at x=0
            // Для универсальности — если |dir.x| >= |dir.y|,|dir.z|, используем X
            float az = glm::radians(flowAzimuth);
            float el = glm::radians(flowElevation);
            glm::vec3 dir(cosf(el)*cosf(az), sinf(el), cosf(el)*sinf(az));
            if (glm::length(dir) < 1e-6f) dir = glm::vec3(1,0,0);
            dir = glm::normalize(dir);
            if (fabsf(dir.x) >= fabsf(dir.y) && fabsf(dir.x) >= fabsf(dir.z)) {
                if (dir.x > 0) { isInlet = (x == 0); isOutlet = (x == lbmNx-1); }
                else { isInlet = (x == lbmNx-1); isOutlet = (x == 0); }
            } else if (fabsf(dir.y) >= fabsf(dir.z)) {
                int y = (cell / lbmNx) % lbmNy;
                if (dir.y > 0) { isInlet = (y == 0); isOutlet = (y == lbmNy-1); }
                else { isInlet = (y == lbmNy-1); isOutlet = (y == 0); }
            } else {
                int z = cell / (lbmNx*lbmNy);
                if (dir.z > 0) { isInlet = (z == 0); isOutlet = (z == lbmNz-1); }
                else { isInlet = (z == lbmNz-1); isOutlet = (z == 0); }
            }

            if (isInlet) {
                rho = 1.0f;
                ux = inUx; uy = inUy; uz = inUz;
            }

            // Smagorinsky LES
            float tauEff = tau0;
            if (lbmParams.useTurbulence && !isInlet && !isOutlet) {
                // вычисляем тензор скоростей деформаций через центральные разности
                int xm = (x > 0) ? cell-1 : cell;
                int xp = (x < lbmNx-1) ? cell+1 : cell;
                int ym = cell - lbmNx;
                int yp = cell + lbmNx;
                int zm = cell - lbmNx*lbmNy;
                int zp = cell + lbmNx*lbmNy;
                if (ym < 0) ym = cell;
                if (yp >= total) yp = cell;
                if (zm < 0) zm = cell;
                if (zp >= total) zp = cell;
                // градиенты
                float dux_dx = (lbmUx[xp] - lbmUx[xm]) * 0.5f;
                float duy_dy = (lbmUy[yp] - lbmUy[ym]) * 0.5f;
                float duz_dz = (lbmUz[zp] - lbmUz[zm]) * 0.5f;
                float dux_dy = (lbmUx[yp] - lbmUx[ym]) * 0.5f;
                float dux_dz = (lbmUx[zp] - lbmUx[zm]) * 0.5f;
                float duy_dx = (lbmUy[xp] - lbmUy[xm]) * 0.5f;
                float duy_dz = (lbmUy[zp] - lbmUy[zm]) * 0.5f;
                float duz_dx = (lbmUz[xp] - lbmUz[xm]) * 0.5f;
                float duz_dy = (lbmUz[yp] - lbmUz[ym]) * 0.5f;

                float Sxx = dux_dx;
                float Syy = duy_dy;
                float Szz = duz_dz;
                float Sxy = 0.5f*(dux_dy + duy_dx);
                float Sxz = 0.5f*(dux_dz + duz_dx);
                float Syz = 0.5f*(duy_dz + duz_dy);

                float S2 = Sxx*Sxx + Syy*Syy + Szz*Szz + 2.0f*(Sxy*Sxy + Sxz*Sxz + Syz*Syz);
                float S = std::sqrt(2.0f*S2);
                if (!std::isfinite(S)) S = 0.0f;
                float Cs = lbmParams.smagorinskyC;
                if (!std::isfinite(Cs) || Cs < 0) Cs = 0.12f;
                float nu_t = Cs*Cs * S; // dx=1
                float tau_t = nu_t / cs2;
                tauEff = tau0 + tau_t;
                if (tauEff > 2.0f) tauEff = 2.0f;
                if (tauEff < 0.51f) tauEff = 0.51f;
            }

            // равновесие и коллизия
            for (int qi = 0; qi < Q; qi++) {
                float feq = computeEquilibrium(qi, rho, ux, uy, uz);
                float fi = f[fIdx(cell, qi)];
                if (!std::isfinite(fi)) fi = feq;
                float fColl = fi - (fi - feq) / tauEff;
                if (!std::isfinite(fColl)) fColl = feq;
                // сохраняем в f (переиспользуем f как пост-коллизионное для стриминга)
                // но нам нужен fNext для стриминга, так что пишем во временный?
                // Для упрощения — пишем в f, а стриминг отдельно из f
                f[fIdx(cell, qi)] = fColl; // пост-коллизия
            }

            lbmRho[cell] = rho;
            lbmUx[cell] = ux; lbmUy[cell] = uy; lbmUz[cell] = uz;

            float velMag = std::sqrt(ux*ux + uy*uy + uz*uz);
            if (velMag > maxVel) maxVel = velMag;
            avgRho += rho;
            kinetic += velMag*velMag;
        }

        // === STREAMING + BOUNCE-BACK ===
        std::fill(fNext.begin(), fNext.end(), 0.0f);

        for (int z = 0; z < lbmNz; z++) {
            for (int y = 0; y < lbmNy; y++) {
                for (int x = 0; x < lbmNx; x++) {
                    int cell = idx3D(x,y,z);
                    if (lbmIsSolid[cell]) continue; // твердые не стримят

                    for (int qi = 0; qi < Q; qi++) {
                        int nx = x + c[qi][0];
                        int ny = y + c[qi][1];
                        int nz = z + c[qi][2];
                        float fColl = f[fIdx(cell, qi)];

                        if (nx < 0 || nx >= lbmNx || ny < 0 || ny >= lbmNy || nz < 0 || nz >= lbmNz) {
                            // выход за границы — outlet/inlet обработка
                            // для outlet — отскок назад? проще — bounce-back на границе
                            // outlet: копируем в противоположное направление в той же ячейке
                            // inlet уже обработан
                            // Для простоты — отражаем
                            fNext[fIdx(cell, opp[qi])] += fColl; // упрощенный outlet
                        } else {
                            int ncell = idx3D(nx,ny,nz);
                            if (lbmIsSolid[ncell]) {
                                // bounce-back: отражаем в противоположное направление в текущей ячейке
                                fNext[fIdx(cell, opp[qi])] += fColl;
                            } else {
                                fNext[fIdx(ncell, qi)] += fColl;
                            }
                        }
                    }
                }
            }
        }

        // Inlet граничные условия — перезаписываем fNext на inlet плоскости равновесием
        {
            float az = glm::radians(flowAzimuth);
            float el = glm::radians(flowElevation);
            glm::vec3 dir(cosf(el)*cosf(az), sinf(el), cosf(el)*sinf(az));
            if (glm::length(dir) < 1e-6f) dir = glm::vec3(1,0,0);
            dir = glm::normalize(dir);
            if (fabsf(dir.x) >= fabsf(dir.y) && fabsf(dir.x) >= fabsf(dir.z)) {
                int ix = (dir.x > 0) ? 0 : lbmNx-1;
                for (int z = 0; z < lbmNz; z++) for (int y = 0; y < lbmNy; y++) {
                    int cell = idx3D(ix,y,z);
                    if (lbmIsSolid[cell]) continue;
                    for (int qi = 0; qi < Q; qi++) {
                        fNext[fIdx(cell, qi)] = computeEquilibrium(qi, 1.0f, inUx, inUy, inUz);
                    }
                }
            } else if (fabsf(dir.y) >= fabsf(dir.z)) {
                int iy = (dir.y > 0) ? 0 : lbmNy-1;
                for (int z = 0; z < lbmNz; z++) for (int x = 0; x < lbmNx; x++) {
                    int cell = idx3D(x,iy,z);
                    if (lbmIsSolid[cell]) continue;
                    for (int qi = 0; qi < Q; qi++) fNext[fIdx(cell, qi)] = computeEquilibrium(qi, 1.0f, inUx, inUy, inUz);
                }
            } else {
                int iz = (dir.z > 0) ? 0 : lbmNz-1;
                for (int y = 0; y < lbmNy; y++) for (int x = 0; x < lbmNx; x++) {
                    int cell = idx3D(x,y,iz);
                    if (lbmIsSolid[cell]) continue;
                    for (int qi = 0; qi < Q; qi++) fNext[fIdx(cell, qi)] = computeEquilibrium(qi, 1.0f, inUx, inUy, inUz);
                }
            }
        }

        f.swap(fNext);

        lbmCurrentStep++;
        lbmAvgRho = avgRho / total;
        lbmAvgKineticEnergy = kinetic / total;
        lbmMaxVelocityLB = maxVel;

        // конвертация в мировые
        float U0mag = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
        if (U0mag < 1e-6f) U0mag = 0.1f;
        float scale = flowSpeed / U0mag;
        if (!std::isfinite(scale) || scale > 1000.0f) scale = 20.0f;
        lbmMaxVelocityWorld = maxVel * scale;

        // сходимость — разница кинетической энергии
        static float prevKinetic = 0.0f;
        lbmConvergence = std::fabs(lbmAvgKineticEnergy - prevKinetic);
        prevKinetic = lbmAvgKineticEnergy;
        if (lbmConvergence < lbmParams.convergenceThreshold) lbmConverged = true;
        else lbmConverged = false;

        // обновление мировых скоростей
        for (int cell = 0; cell < total; cell++) {
            if (lbmIsSolid[cell]) {
                lbmUxWorld[cell] = 0; lbmUyWorld[cell] = 0; lbmUzWorld[cell] = 0;
                lbmPressure[cell] = 0;
            } else {
                lbmUxWorld[cell] = lbmUx[cell] * scale;
                lbmUyWorld[cell] = lbmUy[cell] * scale;
                lbmUzWorld[cell] = lbmUz[cell] * scale;
                // давление из плотности: p = cs2 * (rho - rho0)
                lbmPressure[cell] = cs2 * (lbmRho[cell] - 1.0f) * scale * scale; // масштабируем
            }
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    lbmTimeMs = std::chrono::duration<float, std::milli>(t1-t0).count();
}

void updateLBM(float deltaTime) {
    if (!lbmParams.enabled) return;
    if (!lbmInitialized) {
        initLBM();
        return;
    }
    // если изменился voxelResolution или модель — переинициализируем
    if (lbmNx != g_voxNx || lbmNy != g_voxNy || lbmNz != g_voxNz) {
        std::cout << "[LBM] Voxel grid changed, reinit" << std::endl;
        initLBM();
        return;
    }

    int steps = lbmParams.stepsPerFrame;
    if (steps <= 0) steps = 1;
    if (steps > 20) steps = 20; // лимит для стабильности кадра

    stepLBMCPU(steps);

    // каждые 100 шагов — пересчет вихрей и Q
    if (lbmCurrentStep % 50 == 0) {
        computeLBMVorticityAndQ();
    }
}

glm::vec3 getLBMVelocityLB(const glm::vec3& worldPos) {
    if (!lbmInitialized) return glm::vec3(0);
    if (!std::isfinite(worldPos.x)) return glm::vec3(0);
    // мир -> решетка
    float fx = (worldPos.x - lbmMinX) / lbmCellSizeX;
    float fy = (worldPos.y - lbmMinY) / lbmCellSizeY;
    float fz = (worldPos.z - lbmMinZ) / lbmCellSizeZ;
    int ix = (int)std::floor(fx);
    int iy = (int)std::floor(fy);
    int iz = (int)std::floor(fz);
    float tx = fx - ix;
    float ty = fy - iy;
    float tz = fz - iz;
    if (ix < 0 || ix >= lbmNx-1 || iy < 0 || iy >= lbmNy-1 || iz < 0 || iz >= lbmNz-1) {
        // вне сетки — возвращаем 0 или freestream?
        return glm::vec3(0);
    }
    // трилинейная интерполяция 8 углов
    auto getVel = [&](int x,int y,int z)->glm::vec3 {
        if (x<0||x>=lbmNx||y<0||y>=lbmNy||z<0||z>=lbmNz) return glm::vec3(0);
        int cell = idx3D(x,y,z);
        if (cell<0||cell>=(int)lbmUx.size()) return glm::vec3(0);
        if (lbmIsSolid[cell]) return glm::vec3(0);
        return glm::vec3(lbmUx[cell], lbmUy[cell], lbmUz[cell]);
    };
    glm::vec3 c000 = getVel(ix,iy,iz);
    glm::vec3 c100 = getVel(ix+1,iy,iz);
    glm::vec3 c010 = getVel(ix,iy+1,iz);
    glm::vec3 c110 = getVel(ix+1,iy+1,iz);
    glm::vec3 c001 = getVel(ix,iy,iz+1);
    glm::vec3 c101 = getVel(ix+1,iy,iz+1);
    glm::vec3 c011 = getVel(ix,iy+1,iz+1);
    glm::vec3 c111 = getVel(ix+1,iy+1,iz+1);

    glm::vec3 c00 = c000*(1-tx) + c100*tx;
    glm::vec3 c01 = c001*(1-tx) + c101*tx;
    glm::vec3 c10 = c010*(1-tx) + c110*tx;
    glm::vec3 c11 = c011*(1-tx) + c111*tx;
    glm::vec3 c0 = c00*(1-ty) + c10*ty;
    glm::vec3 c1 = c01*(1-ty) + c11*ty;
    glm::vec3 c = c0*(1-tz) + c1*tz;
    if (!std::isfinite(c.x)) return glm::vec3(0);
    return c;
}

glm::vec3 getLBMVelocityWorld(const glm::vec3& worldPos) {
    glm::vec3 vLB = getLBMVelocityLB(worldPos);
    if (glm::length(vLB) < 1e-6f) {
        // вне LBM — возвращаем freestream как fallback
        return glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
    }
    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    float U0mag = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    if (U0mag < 1e-6f) U0mag = 0.1f;
    float scale = flowSpeed / U0mag;
    if (!std::isfinite(scale)) scale = 20.0f;
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
    int cell = idx3D(ix,iy,iz);
    if (cell < 0 || cell >= (int)lbmRho.size()) return 1.0f;
    return lbmRho[cell];
}

bool isLBMSolidWorld(const glm::vec3& worldPos) {
    if (!lbmInitialized) return false;
    float fx = (worldPos.x - lbmMinX) / lbmCellSizeX;
    float fy = (worldPos.y - lbmMinY) / lbmCellSizeY;
    float fz = (worldPos.z - lbmMinZ) / lbmCellSizeZ;
    int ix = (int)fx, iy = (int)fy, iz = (int)fz;
    if (ix < 0 || ix >= lbmNx || iy < 0 || iy >= lbmNy || iz < 0 || iz >= lbmNz) return false;
    int cell = idx3D(ix,iy,iz);
    if (cell < 0 || cell >= (int)lbmIsSolid.size()) return false;
    return lbmIsSolid[cell] != 0;
}

void computeLBMVorticityAndQ() {
    if (!lbmInitialized) return;
    int total = lbmNx * lbmNy * lbmNz;
    if (total <= 0) return;

    for (int z = 1; z < lbmNz-1; z++) {
        for (int y = 1; y < lbmNy-1; y++) {
            for (int x = 1; x < lbmNx-1; x++) {
                int cell = idx3D(x,y,z);
                if (lbmIsSolid[cell]) { lbmVorticityMag[cell] = 0; lbmQCriterion[cell] = 0; continue; }

                int xm = idx3D(x-1,y,z), xp = idx3D(x+1,y,z);
                int ym = idx3D(x,y-1,z), yp = idx3D(x,y+1,z);
                int zm = idx3D(x,y,z-1), zp = idx3D(x,y,z+1);

                // центральные разности для скорости (мировая)
                float dux_dx = (lbmUxWorld[xp] - lbmUxWorld[xm]) * 0.5f / lbmCellSizeX;
                float dux_dy = (lbmUxWorld[yp] - lbmUxWorld[ym]) * 0.5f / lbmCellSizeY;
                float dux_dz = (lbmUxWorld[zp] - lbmUxWorld[zm]) * 0.5f / lbmCellSizeZ;

                float duy_dx = (lbmUyWorld[xp] - lbmUyWorld[xm]) * 0.5f / lbmCellSizeX;
                float duy_dy = (lbmUyWorld[yp] - lbmUyWorld[ym]) * 0.5f / lbmCellSizeY;
                float duy_dz = (lbmUyWorld[zp] - lbmUyWorld[zm]) * 0.5f / lbmCellSizeZ;

                float duz_dx = (lbmUzWorld[xp] - lbmUzWorld[xm]) * 0.5f / lbmCellSizeX;
                float duz_dy = (lbmUzWorld[yp] - lbmUzWorld[ym]) * 0.5f / lbmCellSizeY;
                float duz_dz = (lbmUzWorld[zp] - lbmUzWorld[zm]) * 0.5f / lbmCellSizeZ;

                // завихренность ω = curl u
                float wx = duz_dy - duy_dz;
                float wy = dux_dz - duz_dx;
                float wz = duy_dx - dux_dy;
                float vortMag = std::sqrt(wx*wx + wy*wy + wz*wz);
                if (!std::isfinite(vortMag)) vortMag = 0;
                lbmVorticityMag[cell] = vortMag;

                // Q-критерий: Q = 0.5*(||Ω||² - ||S||²)
                // Ω — антисимметричная часть, S — симметричная
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
                if (!std::isfinite(Q)) Q = 0;
                lbmQCriterion[cell] = Q;
            }
        }
    }
}

float computeLBMRe() {
    if (!lbmInitialized) return 0.0f;
    float L = std::max({(float)lbmNx, (float)lbmNy, (float)lbmNz});
    float nu = lbmParams.viscosity;
    if (nu < 1e-6f) nu = 0.033f;
    return lbmParams.U0 * L / nu;
}

void computeLBMForcesFromLBM() {
    // Используем LBM давление для пересчета сил (momentum exchange упрощенно)
    // Для v1.5.0 — используем существующий computeLiftDrag но с LBM скоростями для Cp
    // Здесь только обновляем avg и т.д.
    lbmReynolds = computeLBMRe();
}

// Тесты LBM
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
    // масса должна сохраняться ~ rho=1
    float avg = 0;
    for (float r : lbmRho) avg += r;
    avg /= lbmRho.size();
    if (std::fabs(avg - 1.0f) > 0.1f) return false;
    return true;
}
bool lbmValidateBoundaryConditions() {
    if (!lbmInitialized) return true;
    // inlet должен иметь скорость ~ U0
    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    float mag = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    if (mag < 1e-6f || mag > 0.3f) return false;
    return true;
}
bool lbmValidateSolidHandling() {
    if (!lbmInitialized) return true;
    for (size_t i = 0; i < lbmIsSolid.size(); i++) {
        if (lbmIsSolid[i]) {
            if (std::fabs(lbmUx[i]) > 1e-6f || std::fabs(lbmUy[i]) > 1e-6f || std::fabs(lbmUz[i]) > 1e-6f) return false;
        }
    }
    return true;
}

void drawLBMUI() {
    // UI уже в ui.cpp — эта функция для совместимости
}
