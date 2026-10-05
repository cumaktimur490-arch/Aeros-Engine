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
float lbmTKE = 0.0f;

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
std::vector<float> lbmTKEField;
std::vector<float> lbmStrainMag;
std::vector<char>  lbmIsSolid;
std::vector<char>  lbmIsGround;

static std::vector<float> f;
static std::vector<float> fNext;

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
    0, 2,1,4,3,6,5, 8,7,10,9, 12,11,14,13, 16,15,18,17
};
static const float cs2 = 1.0f/3.0f;
static const float invCs2 = 3.0f;
static const float invCs2SqHalf = 4.5f;

inline int idx3D(int x, int y, int z) { return (z*lbmNy + y)*lbmNx + x; }
inline int fIdx(int cell, int q) { return cell*Q + q; }

// =====================================================
// v1.11.0 Physics Fix — корректное равновесие без костылей
// =====================================================
inline float computeEquilibriumFast(int qi, float rho, float ux, float uy, float uz, float usqr) {
    if (!std::isfinite(rho)) rho = 1.0f;
    rho = glm::clamp(rho, 0.8f, 1.2f); // мягкий clamp для стабильности, без потери физики

    // Ограничиваем usqr для стабильности, но сохраняем направление
    if (usqr > 0.09f) { // |u| < 0.3 в решеточных единицах — предел стабильности
        float scale = sqrtf(0.09f / usqr);
        ux *= scale; uy *= scale; uz *= scale;
        usqr = ux*ux + uy*uy + uz*uz;
    }

    float cu = (float)c[qi][0]*ux + (float)c[qi][1]*uy + (float)c[qi][2]*uz;
    // cu может быть до ~0.3, не климпим жестко, только проверка на NaN
    if (!std::isfinite(cu)) cu = 0.0f;

    float feq = w[qi]*rho*(1.0f + cu*invCs2 + cu*cu*invCs2SqHalf - usqr*1.5f);
    // Только проверка на NaN/Inf, без искусственного ограничения 2.0
    if (!std::isfinite(feq)) feq = w[qi]*rho;
    // Минимальная положительность — LBM допускает небольшие отрицательные, но для стабильности >0
    if (feq < 0.0f && qi != 0) feq = 0.0f; // только не центральная может быть 0
    if (feq < 0.0f) feq = w[qi]*rho*0.1f;
    return feq;
}

static void computeInletVelocityLB(float& ux, float& uy, float& uz) {
    float az = glm::radians(flowAzimuth);
    float el = glm::radians(flowElevation);
    float ce = cosf(el), se = sinf(el);
    float ca = cosf(az), sa = sinf(az);
    float dx = ce*ca, dy = se, dz = ce*sa;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 1e-6f || !std::isfinite(len)) { dx = 1; dy = 0; dz = 0; len = 1; }
    dx /= len; dy /= len; dz /= len;
    float U0 = lbmParams.U0;
    if (!std::isfinite(U0) || U0 < 1e-6f) U0 = 0.08f;
    if (U0 > 0.15f) U0 = 0.15f; // стабильный предел 0.15, не 0.25
    ux = dx * U0; uy = dy * U0; uz = dz * U0;
}

static void computeFlowAxis(int& axis, int& sign, glm::vec3& dirNorm) {
    float az = glm::radians(flowAzimuth);
    float el = glm::radians(flowElevation);
    float ce = cosf(el), se = sinf(el);
    float ca = cosf(az), sa = sinf(az);
    dirNorm = glm::vec3(ce*ca, se, ce*sa);
    float len = glm::length(dirNorm);
    if (len < 1e-6f || !std::isfinite(len)) dirNorm = glm::vec3(1,0,0);
    else dirNorm = dirNorm / len;
    float ax = fabsf(dirNorm.x), ay = fabsf(dirNorm.y), azv = fabsf(dirNorm.z);
    if (ax >= ay && ax >= azv) { axis = 0; sign = (dirNorm.x > 0) ? 1 : -1; }
    else if (ay >= azv) { axis = 1; sign = (dirNorm.y > 0) ? 1 : -1; }
    else { axis = 2; sign = (dirNorm.z > 0) ? 1 : -1; }
}

// =====================================================
// Цветовые карты v1.11.0 — без изменений, но с clamp
// =====================================================
glm::vec3 getRealisticPressureColor(float cp) {
    if (!std::isfinite(cp)) cp = 0;
    float t = (cp + 3.0f) / 4.0f;
    t = glm::clamp(t, 0.0f, 1.0f);
    
    if (aeroColorMap == 1) {
        if (t < 0.25f) { float k=t/0.25f; return glm::vec3(0.267f+k*0.1f, 0.004f+k*0.2f, 0.329f+k*0.3f); }
        else if (t < 0.5f) { float k=(t-0.25f)/0.25f; return glm::vec3(0.22f+k*0.05f, 0.3f+k*0.2f, 0.5f); }
        else if (t < 0.75f) { float k=(t-0.5f)/0.25f; return glm::vec3(0.12f+k*0.4f, 0.5f+k*0.2f, 0.5f-k*0.2f); }
        else { float k=(t-0.75f)/0.25f; return glm::vec3(0.5f+k*0.49f, 0.7f+k*0.2f, 0.3f-k*0.1f); }
    } else if (aeroColorMap == 3) {
        if (t < 0.5f) { float k=t*2.0f; return glm::vec3(0.2f+k*0.3f, 0.3f+k*0.3f, 0.9f-k*0.2f); }
        else { float k=(t-0.5f)*2.0f; return glm::vec3(0.5f+k*0.5f, 0.6f-k*0.4f, 0.7f-k*0.5f); }
    }
    
    if (t < 0.2f) {
        float k = t / 0.2f;
        return glm::vec3(0.0f, k*0.5f, 0.5f + 0.5f*k);
    } else if (t < 0.4f) {
        float k = (t-0.2f)/0.2f;
        return glm::vec3(0.0f, 0.5f + 0.5f*k, 1.0f - 0.3f*k);
    } else if (t < 0.6f) {
        float k = (t-0.4f)/0.2f;
        return glm::vec3(k*0.8f, 1.0f, 0.7f - 0.7f*k);
    } else if (t < 0.8f) {
        float k = (t-0.6f)/0.2f;
        return glm::vec3(0.8f + 0.2f*k, 1.0f - 0.5f*k, 0.0f);
    } else {
        float k = (t-0.8f)/0.2f;
        return glm::vec3(1.0f, 0.5f - 0.5f*k, 0.0f);
    }
}

glm::vec3 getVelocityMagnitudeColor(float velMag, float maxVel) {
    if (!std::isfinite(velMag)) velMag = 0;
    if (!std::isfinite(maxVel) || maxVel < 1e-6f) maxVel = 1.0f;
    float t = velMag / maxVel;
    t = glm::clamp(t, 0.0f, 1.0f);

    if (aeroColorMap == 1) {
        if (t < 0.25f) { float k=t/0.25f; return glm::vec3(0.267f, 0.004f+k*0.3f, 0.329f+k*0.2f); }
        else if (t < 0.5f) { float k=(t-0.25f)/0.25f; return glm::vec3(0.229f+k*0.0f, 0.322f+k*0.2f, 0.545f); }
        else if (t < 0.75f) { float k=(t-0.5f)/0.25f; return glm::vec3(0.127f+k*0.4f, 0.566f+k*0.2f, 0.550f-k*0.2f); }
        else { float k=(t-0.75f)/0.25f; return glm::vec3(0.5f+k*0.49f, 0.79f+k*0.1f, 0.3f); }
    } else if (aeroColorMap == 2) {
        if (t < 0.2f) return glm::vec3(t*5.0f*0.1f, t*2.0f, 0.8f);
        else if (t < 0.4f) { float k=(t-0.2f)/0.2f; return glm::vec3(0.1f+k*0.1f, 0.4f+k*0.4f, 0.8f-k*0.2f); }
        else if (t < 0.6f) { float k=(t-0.4f)/0.2f; return glm::vec3(0.2f+k*0.5f, 0.8f, 0.6f-k*0.4f); }
        else if (t < 0.8f) { float k=(t-0.6f)/0.2f; return glm::vec3(0.7f+k*0.3f, 0.8f-k*0.4f, 0.2f); }
        else { float k=(t-0.8f)/0.2f; return glm::vec3(1.0f, 0.4f-k*0.3f, k*0.1f); }
    }

    if (t < 0.15f) return glm::vec3(0, 0, 0.5f + 0.5f*(t/0.15f));
    else if (t < 0.3f) { float k=(t-0.15f)/0.15f; return glm::vec3(0, k, 1); }
    else if (t < 0.45f) { float k=(t-0.3f)/0.15f; return glm::vec3(0, 1, 1-k); }
    else if (t < 0.6f) { float k=(t-0.45f)/0.15f; return glm::vec3(k, 1, 0); }
    else if (t < 0.8f) { float k=(t-0.6f)/0.2f; return glm::vec3(1, 1-k*0.5f, 0); }
    else { float k=(t-0.8f)/0.2f; return glm::vec3(1, 0.5f-0.5f*k, k*0.3f); }
}

glm::vec3 getVorticityColor(float vortMag) {
    if (!std::isfinite(vortMag)) vortMag = 0;
    float t = glm::clamp(vortMag / 20.0f, 0.0f, 1.0f);
    if (t < 0.5f) {
        float k = t*2.0f;
        return glm::vec3(k, k, 1.0f);
    } else {
        float k = (t-0.5f)*2.0f;
        return glm::vec3(1.0f, 1.0f-k, 1.0f-k);
    }
}

glm::vec3 getQCriterionColor(float q) {
    if (!std::isfinite(q)) return glm::vec3(0.5f);
    if (q > 0) {
        float t = glm::clamp(q*10.0f, 0.0f, 1.0f);
        return glm::vec3(1.0f, 0.3f + 0.7f*(1.0f-t), 0.0f);
    } else {
        float t = glm::clamp(-q*5.0f, 0.0f, 1.0f);
        return glm::vec3(0.0f, 0.3f + 0.4f*t, 0.8f + 0.2f*t);
    }
}

glm::vec3 getTKEColor(float tke) {
    if (!std::isfinite(tke)) return glm::vec3(0.5f,0,0.5f);
    float t = glm::clamp(tke*20.0f, 0.0f, 1.0f);
    return glm::vec3(t, t*0.3f, 1.0f-t*0.5f);
}

glm::vec3 getMachColor(float mach) {
    if (!std::isfinite(mach)) return glm::vec3(0.5f);
    float t = glm::clamp(mach / 1.5f, 0.0f, 1.0f);
    if (t < 0.2f) { float k=t/0.2f; return glm::vec3(0, k*0.5f, 0.8f+0.2f*k); }
    else if (t < 0.5f) { float k=(t-0.2f)/0.3f; return glm::vec3(k*0.3f, 0.5f+0.5f*k, 1.0f-k*0.5f); }
    else if (t < 0.75f) { float k=(t-0.5f)/0.25f; return glm::vec3(0.3f+0.7f*k, 1.0f, 0.5f-k*0.5f); }
    else { float k=(t-0.75f)/0.25f; return glm::vec3(1.0f, 1.0f-k*0.8f, k*0.2f); }
}

glm::vec3 getHelicityColor(float helicity) {
    if (!std::isfinite(helicity)) return glm::vec3(0.5f);
    float t = glm::clamp(helicity*0.5f + 0.5f, 0.0f, 1.0f);
    if (t < 0.5f) {
        float k = t*2.0f;
        return glm::vec3(k, k, 1.0f);
    } else {
        float k = (t-0.5f)*2.0f;
        return glm::vec3(1.0f, 1.0f-k, 1.0f-k);
    }
}

glm::vec3 getTotalPressureColor(float pt, float ptInf) {
    if (!std::isfinite(pt) || !std::isfinite(ptInf) || ptInf < 1e-6f) return glm::vec3(0.5f);
    float ratio = pt / ptInf;
    float t = glm::clamp(ratio, 0.0f, 1.2f) / 1.2f;
    if (t < 0.3f) return glm::vec3(0, 0, 0.5f + 0.5f*(t/0.3f));
    else if (t < 0.6f) { float k=(t-0.3f)/0.3f; return glm::vec3(k*0.5f, k, 1.0f-k*0.5f); }
    else if (t < 0.85f) { float k=(t-0.6f)/0.25f; return glm::vec3(0.5f+0.5f*k, 1.0f, 0); }
    else { float k=(t-0.85f)/0.35f; return glm::vec3(1.0f, 1.0f-k*0.5f, 0); }
}

void initLBM() {
    std::cout << "[LBM] Initializing v1.11.0 Physics Fix..." << std::endl;
    auto t0 = std::chrono::high_resolution_clock::now();

    if (g_voxelData.empty() || g_voxNx <= 0) {
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

    const int MAX_CELLS = 8*1024*1024;
    int totalEst = lbmNx*lbmNy*lbmNz;
    if (totalEst > MAX_CELLS) {
        float scale = powf((float)MAX_CELLS / totalEst, 1.0f/3.0f);
        lbmNx = (int)(lbmNx*scale); if (lbmNx < 8) lbmNx = 8;
        lbmNy = (int)(lbmNy*scale); if (lbmNy < 8) lbmNy = 8;
        lbmNz = (int)(lbmNz*scale); if (lbmNz < 8) lbmNz = 8;
        std::cout << "[LBM] Clamped grid to " << lbmNx << "x" << lbmNy << "x" << lbmNz << std::endl;
    }

    lbmCellSizeX = (lbmMaxX - lbmMinX) / std::max(1, lbmNx);
    lbmCellSizeY = (lbmMaxY - lbmMinY) / std::max(1, lbmNy);
    lbmCellSizeZ = (lbmMaxZ - lbmMinZ) / std::max(1, lbmNz);
    if (!std::isfinite(lbmCellSizeX) || lbmCellSizeX < 1e-6f) lbmCellSizeX = 0.1f;
    if (!std::isfinite(lbmCellSizeY) || lbmCellSizeY < 1e-6f) lbmCellSizeY = 0.1f;
    if (!std::isfinite(lbmCellSizeZ) || lbmCellSizeZ < 1e-6f) lbmCellSizeZ = 0.1f;

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
        lbmTKEField.assign(total, 0.0f);
        lbmStrainMag.assign(total, 0.0f);
        lbmIsSolid.assign(total, 0);
        lbmIsGround.assign(total, 0);
        f.assign(total*Q, 0.0f);
        fNext.assign(total*Q, 0.0f);
    } catch (const std::bad_alloc& e) {
        std::cout << "[LBM] Allocation failed: " << e.what() << " total=" << total << std::endl;
        lbmInitialized = false;
        return;
    }

    if (!g_voxelData.empty() && (int)g_voxelData.size() == total) {
        const int* vox = g_voxelData.data();
        char* solid = lbmIsSolid.data();
        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int i = 0; i < total; ++i) solid[i] = (vox[i] == 1) ? 1 : 0;
    } else if (!g_voxelData.empty()) {
        std::cout << "[LBM] Voxel size mismatch " << g_voxelData.size() << " vs " << total << " — using nearest" << std::endl;
        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int z = 0; z < lbmNz; ++z) {
            for (int y = 0; y < lbmNy; ++y) {
                for (int x = 0; x < lbmNx; ++x) {
                    int gz = (int)((float)z / lbmNz * g_voxNz);
                    int gy = (int)((float)y / lbmNy * g_voxNy);
                    int gx = (int)((float)x / lbmNx * g_voxNx);
                    gz = glm::clamp(gz, 0, g_voxNz-1);
                    gy = glm::clamp(gy, 0, g_voxNy-1);
                    gx = glm::clamp(gx, 0, g_voxNx-1);
                    int gIdx = (gz*g_voxNy + gy)*g_voxNx + gx;
                    int lIdx = (z*lbmNy + y)*lbmNx + x;
                    if (gIdx >=0 && gIdx < (int)g_voxelData.size())
                        lbmIsSolid[lIdx] = (g_voxelData[gIdx]==1)?1:0;
                }
            }
        }
    }

    if (aeroGroundEffect || lbmParams.useGround) {
        float groundY = g_voxMinY + aeroGroundHeight;
        if (lbmParams.useGround) groundY = g_voxMinY + lbmParams.groundHeight;
        if (!std::isfinite(groundY)) groundY = g_voxMinY;
        int gy0 = (int)((groundY - lbmMinY) / lbmCellSizeY);
        if (gy0 < 0) gy0 = 0;
        if (gy0 >= lbmNy) gy0 = lbmNy-1;
        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int z = 0; z < lbmNz; ++z) {
            for (int y = 0; y <= gy0; ++y) {
                for (int x = 0; x < lbmNx; ++x) {
                    int cell = (z*lbmNy + y)*lbmNx + x;
                    if (!lbmIsSolid[cell]) lbmIsGround[cell] = 1;
                }
            }
        }
        std::cout << "[LBM] Ground enabled at Y=" << groundY << " cells 0.." << gy0 << std::endl;
    }

    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    std::cout << "[LBM] Inlet LB velocity: (" << inUx << "," << inUy << "," << inUz << ") total=" << total << std::endl;

    float* fPtr = f.data();
    char* solidPtr = lbmIsSolid.data();
    char* groundPtr = lbmIsGround.data();

    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (int cell = 0; cell < total; ++cell) {
        float rho = 1.0f;
        float ux = 0, uy = 0, uz = 0;
        if (!solidPtr[cell] && !groundPtr[cell]) { ux = inUx; uy = inUy; uz = inUz; }
        // v1.11.0: убран искусственный шум — он ломает сохранение массы
        // Вместо него — небольшая дивергенция-free турбулентность только если включена
        if (lbmParams.inletTurbulence > 0.001f && !solidPtr[cell] && !groundPtr[cell]) {
            // Дивергенция-free шум через векторный потенциал — упрощенно: только поперечные флуктуации
            float turb = lbmParams.inletTurbulence * 0.05f; // уменьшено в 2 раза
            // Используем более коррелированный шум — по координатам ячейки, а не индексу
            int x = cell % lbmNx;
            int y = (cell / lbmNx) % lbmNy;
            int z = cell / (lbmNx*lbmNy);
            float fx = (float)x / lbmNx, fy = (float)y / lbmNy, fz = (float)z / lbmNz;
            float rx = sinf(fx*12.3f + fy*5.1f) * turb;
            float ry = sinf(fy*8.7f + fz*3.3f) * turb;
            float rz = sinf(fz*11.2f + fx*4.9f) * turb;
            // Убираем компоненту вдоль потока чтобы не менять массовый расход
            glm::vec3 flowDir(inUx, inUy, inUz);
            float fLen = sqrtf(inUx*inUx+inUy*inUy+inUz*inUz);
            if (fLen > 1e-6f) {
                flowDir /= fLen;
                glm::vec3 turbVec(rx,ry,rz);
                float along = glm::dot(turbVec, flowDir);
                turbVec -= flowDir * along; // только поперек
                ux += turbVec.x; uy += turbVec.y; uz += turbVec.z;
            }
        }
        float usqr = ux*ux + uy*uy + uz*uz;
        if (usqr > 0.09f) {
            float scale = sqrtf(0.09f / usqr);
            ux *= scale; uy *= scale; uz *= scale;
            usqr = ux*ux + uy*uy + uz*uz;
        }
        int base = cell*Q;
        for (int qi = 0; qi < Q; ++qi) {
            fPtr[base+qi] = computeEquilibriumFast(qi, rho, ux, uy, uz, usqr);
        }
    }

    // v1.14.0 Physics Ultra — Sutherland + физически корректный tau
    // Sutherland: mu = mu0 * (T/T0)^{3/2} * (T0+S)/(T+S)
    float T = airTemperature + 273.15f;
    if (!std::isfinite(T) || T < 50.0f) T = 288.15f;
    const float T0 = 273.15f;
    const float S = 110.4f;
    const float mu0 = 1.716e-5f;
    float muSuth = mu0 * powf(T/T0, 1.5f) * (T0+S)/(T+S);
    if (!std::isfinite(muSuth) || muSuth < 1e-6f) muSuth = 1.81e-5f;

    float L = maxDim;
    if (L < 1e-6f) L = 1.0f;
    float rho = airDensity;
    if (!std::isfinite(rho) || rho < 0.01f) rho = 1.225f;
    float nuPhys = muSuth / rho; // физическая кинематическая вязкость

    // tau из физического Re: tau = nu_LB/cs2 + 0.5, где nu_LB = (U_LB * L_LB)/Re
    // L_LB ~ Nx характерный, U_LB = U0
    float U0tmp = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    if (!std::isfinite(U0tmp) || U0tmp < 1e-6f) U0tmp = 0.08f;
    float RePhys = rho * flowSpeed * L / muSuth;
    if (!std::isfinite(RePhys) || RePhys < 1.0f) RePhys = 1e4f;
    float L_LB = (float)lbmNx; // характерный размер в решеточных единицах
    if (L_LB < 1.0f) L_LB = 32.0f;
    float nuLB_fromRe = (U0tmp * L_LB) / RePhys;
    // Смешиваем с пользовательским tau, но ограничиваем физически
    float tauFromPhys = nuLB_fromRe / cs2 + 0.5f;
    // Если tau из физики в разумных пределах, используем его, иначе пользовательский
    float tauUser = lbmParams.tau;
    if (!std::isfinite(tauUser) || tauUser < 0.51f) tauUser = 0.55f;
    if (tauUser > 1.5f) tauUser = 1.5f;
    // Выбираем более стабильный: max физического и пользовательского, но в пределах
    float tauFinal = tauFromPhys;
    if (tauFinal < 0.51f) tauFinal = 0.51f;
    if (tauFinal > 1.5f) tauFinal = 1.5f;
    // Если физический дает слишком маленькую вязкость (высокий Re), используем пользовательский для стабильности
    if (tauFromPhys < 0.52f) tauFinal = tauUser;

    lbmParams.tau = tauFinal;
    lbmParams.viscosity = (lbmParams.tau - 0.5f) * cs2;
    lbmParams.U0 = U0tmp;

    lbmCurrentStep = 0;
    lbmConverged = false;
    lbmInitialized = true;

    lbmReynolds = RePhys;
    aeroReNumber = lbmReynolds;

    auto t1 = std::chrono::high_resolution_clock::now();
    lbmTimeMs = std::chrono::duration<float, std::milli>(t1-t0).count();
    std::cout << "[LBM] Initialized Physics Ultra v1.14.0: " << lbmNx << "x" << lbmNy << "x" << lbmNz << " = " << total
              << " mu=" << muSuth << " nuPhys=" << nuPhys
              << " cells, tau=" << lbmParams.tau << " nu=" << lbmParams.viscosity
              << " Re_phys=" << lbmReynolds << " in " << lbmTimeMs << " ms"
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

void stepLBMCPU(int steps) {
    if (!lbmInitialized) return;
    if (lbmNx <= 0 || lbmNy <= 0 || lbmNz <= 0) return;
    size_t totalSz = (size_t)lbmNx * (size_t)lbmNy * (size_t)lbmNz;
    if (totalSz == 0 || totalSz > 20*1024*1024) return;
    int total = (int)totalSz;
    if (total != (int)lbmRho.size()) return;
    if (f.size() != totalSz * (size_t)Q) return;

    auto t0 = std::chrono::high_resolution_clock::now();

    float tau0 = lbmParams.tau;
    if (!std::isfinite(tau0) || tau0 < 0.51f) tau0 = 0.55f;
    if (tau0 > 1.5f) tau0 = 1.5f;
    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    float U0mag = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    if (!std::isfinite(U0mag) || U0mag < 1e-6f) U0mag = 0.08f;

    int flowAxis, flowSign;
    glm::vec3 flowDir;
    computeFlowAxis(flowAxis, flowSign, flowDir);

    float* fPtr = f.data();
    float* fNextPtr = fNext.data();
    float* rhoPtr = lbmRho.data();
    float* uxPtr = lbmUx.data();
    float* uyPtr = lbmUy.data();
    float* uzPtr = lbmUz.data();
    float* tkePtr = lbmTKEField.data();
    float* strainPtr = lbmStrainMag.data();
    char* solidPtr = lbmIsSolid.data();
    char* groundPtr = lbmIsGround.data();

    const int Nx = lbmNx, Ny = lbmNy, Nz = lbmNz;
    const int strideY = Nx;
    const int strideZ = Nx*Ny;

    static float prevKinetic = 0.0f;
    bool hasDiverged = false;

    for (int s = 0; s < steps; ++s) {
        float maxVelGlobal = 0.0f;
        double avgRhoAcc = 0.0;
        double kineticAcc = 0.0;
        double tkeAcc = 0.0;
        int nanCount = 0;

        #ifdef _OPENMP
        #pragma omp parallel reduction(+:avgRhoAcc,kineticAcc,tkeAcc,nanCount) reduction(max:maxVelGlobal)
        {
            #pragma omp for nowait
        #endif
        for (int cell = 0; cell < total; ++cell) {
            if (solidPtr[cell] || groundPtr[cell]) {
                rhoPtr[cell] = 1.0f;
                uxPtr[cell] = 0; uyPtr[cell] = 0; uzPtr[cell] = 0;
                tkePtr[cell] = 0; strainPtr[cell] = 0;
                continue;
            }

            int base = cell*Q;
            float rho = 0.0f, ux = 0.0f, uy = 0.0f, uz = 0.0f;
            for (int qi = 0; qi < Q; ++qi) {
                float fi = fPtr[base+qi];
                if (!std::isfinite(fi)) { fi = w[qi]; nanCount++; }
                // Убран жесткий clamp -0.5/2.0, заменен на мягкий
                if (fi < -0.2f) fi = w[qi]*0.5f;
                if (fi > 1.0f) fi = w[qi]*1.5f;
                rho += fi;
                ux += fi * (float)c[qi][0];
                uy += fi * (float)c[qi][1];
                uz += fi * (float)c[qi][2];
            }
            if (rho < 0.5f || rho > 1.5f || !std::isfinite(rho)) { rho = 1.0f; nanCount++; }
            float invRho = 1.0f / rho;
            ux *= invRho; uy *= invRho; uz *= invRho;
            if (!std::isfinite(ux)) { ux = 0; uy = 0; uz = 0; nanCount++; }

            float usqrTmp = ux*ux + uy*uy + uz*uz;
            if (usqrTmp > 0.09f) {
                float scale = sqrtf(0.09f / usqrTmp);
                ux *= scale; uy *= scale; uz *= scale;
            }

            int x = cell % Nx;
            int y = (cell / Nx) % Ny;
            int z = cell / strideZ;
            bool isInlet = false;
            bool isOutlet = false;
            if (flowAxis == 0) {
                isInlet = (flowSign > 0) ? (x == 0) : (x == Nx-1);
                isOutlet = (flowSign > 0) ? (x == Nx-1) : (x == 0);
            } else if (flowAxis == 1) {
                isInlet = (flowSign > 0) ? (y == 0) : (y == Ny-1);
                isOutlet = (flowSign > 0) ? (y == Ny-1) : (y == 0);
            } else {
                isInlet = (flowSign > 0) ? (z == 0) : (z == Nz-1);
                isOutlet = (flowSign > 0) ? (z == Nz-1) : (z == 0);
            }

            if (isInlet) {
                rho = 1.0f;
                ux = inUx; uy = inUy; uz = inUz;
            }

            // Smagorinsky LES — v1.11.0 Physics Fix: правильная формула с (Cs*dx)^2
            float tauEff = tau0;
            float strainMag = 0.0f;
            if (lbmParams.useTurbulence && !isInlet && !isOutlet) {
                if (x > 0 && x < Nx-1 && y > 0 && y < Ny-1 && z > 0 && z < Nz-1) {
                    int xm = cell - 1, xp = cell + 1;
                    int ym = cell - strideY, yp = cell + strideY;
                    int zm = cell - strideZ, zp = cell + strideZ;
                    if (xm>=0 && xp<total && ym>=0 && yp<total && zm>=0 && zp<total) {
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

                            float Sxx = dux_dx, Syy = duy_dy, Szz = duz_dz;
                            float Sxy = 0.5f*(dux_dy + duy_dx);
                            float Sxz = 0.5f*(dux_dz + duz_dx);
                            float Syz = 0.5f*(duy_dz + duz_dy);
                            float S2 = Sxx*Sxx + Syy*Syy + Szz*Szz + 2.0f*(Sxy*Sxy + Sxz*Sxz + Syz*Syz);
                            float S = std::sqrt(2.0f*S2);
                            strainMag = S;
                            if (std::isfinite(S) && S < 10.0f && S > 1e-6f) {
                                float Cs = lbmParams.smagorinskyC;
                                if (!std::isfinite(Cs) || Cs < 0) Cs = 0.12f;
                                if (Cs > 0.3f) Cs = 0.3f;

                                // v1.11.0: правильная формула Smagorinsky с delta = cellSize
                                float dx = lbmCellSizeX; // характерный размер ячейки
                                // Van Driest damping у стенок: Cs_eff = Cs * (1 - exp(-y+/26))
                                // y+ оцениваем как расстояние до ближайшей твердой ячейки
                                float yPlus = 1.0f;
                                // Поиск ближайшей твердой ячейки в радиусе 5
                                int wallDist = 5;
                                bool nearWall = false;
                                for (int dz2=-2; dz2<=2 && !nearWall; ++dz2)
                                    for (int dy2=-2; dy2<=2 && !nearWall; ++dy2)
                                        for (int dx2=-2; dx2<=2 && !nearWall; ++dx2) {
                                            int nx2 = x+dx2, ny2 = y+dy2, nz2 = z+dz2;
                                            if (nx2<0||nx2>=Nx||ny2<0||ny2>=Ny||nz2<0||nz2>=Nz) continue;
                                            int c2 = (nz2*Ny+ny2)*Nx+nx2;
                                            if (solidPtr[c2] || groundPtr[c2]) { wallDist = abs(dx2)+abs(dy2)+abs(dz2); nearWall = true; }
                                        }
                                if (nearWall) {
                                    float yPlusEst = (float)wallDist;
                                    float damping = 1.0f - expf(-yPlusEst/2.0f);
                                    Cs *= damping;
                                }

                                float nu_t = Cs*Cs * dx*dx * S; // правильная формула
                                tauEff = tau0 + nu_t / cs2;
                                if (tauEff > 1.5f) tauEff = 1.5f;
                                if (tauEff < 0.51f) tauEff = 0.51f;
                            }
                        }
                    }
                }
            }

            float usqr = ux*ux + uy*uy + uz*uz;
            if (usqr > 0.09f) {
                float scale = sqrtf(0.09f / usqr);
                ux *= scale; uy *= scale; uz *= scale;
                usqr = ux*ux + uy*uy + uz*uz;
            }
            float invTau = 1.0f / tauEff;

            for (int qi = 0; qi < Q; ++qi) {
                float feq = computeEquilibriumFast(qi, rho, ux, uy, uz, usqr);
                float fi = fPtr[base+qi];
                if (!std::isfinite(fi)) fi = feq;
                float fNew = fi - (fi - feq) * invTau;
                if (!std::isfinite(fNew)) fNew = feq;
                if (fNew < 0.0f) fNew = 0.0f; // только положительность
                fPtr[base+qi] = fNew;
            }

            rhoPtr[cell] = rho;
            uxPtr[cell] = ux; uyPtr[cell] = uy; uzPtr[cell] = uz;
            strainPtr[cell] = strainMag;
            // v1.11.0: корректный TKE = (Cs*dx*S)^2
            float dx = lbmCellSizeX;
            float Cs = lbmParams.smagorinskyC;
            float tke = Cs*Cs * dx*dx * strainMag*strainMag;
            if (!std::isfinite(tke) || tke < 0) tke = 0;
            if (tke > 1.0f) tke = 1.0f;
            tkePtr[cell] = tke;

            float velMag = std::sqrt(usqr);
            if (velMag > maxVelGlobal) maxVelGlobal = velMag;
            avgRhoAcc += rho;
            kineticAcc += usqr;
            tkeAcc += tke;
        }
        #ifdef _OPENMP
        }
        #endif

        if (nanCount > total/10) {
            std::cout << "[LBM] Warning: many NaN/invalid (" << nanCount << ") — possible instability, resetting" << std::endl;
            hasDiverged = true;
            break;
        }

        // Streaming с bounce-back
        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int z = 0; z < Nz; ++z) {
            for (int y = 0; y < Ny; ++y) {
                int rowBase = (z*Ny + y)*Nx;
                for (int x = 0; x < Nx; ++x) {
                    int dest = rowBase + x;
                    if (solidPtr[dest] || groundPtr[dest]) continue;
                    int destBase = dest*Q;
                    for (int qi = 0; qi < Q; ++qi) {
                        int sx = x - c[qi][0];
                        int sy = y - c[qi][1];
                        int sz = z - c[qi][2];
                        if (sx < 0 || sx >= Nx || sy < 0 || sy >= Ny || sz < 0 || sz >= Nz) {
                            // Граница — будет обработано отдельно как inlet/outlet
                            fNextPtr[destBase+qi] = fPtr[destBase+qi];
                        } else {
                            int src = (sz*Ny + sy)*Nx + sx;
                            if (src < 0 || src >= total) {
                                fNextPtr[destBase+qi] = fPtr[destBase+qi];
                            } else if (solidPtr[src] || groundPtr[src]) {
                                // Bounce-back: отражение
                                fNextPtr[destBase+qi] = fPtr[destBase+opp[qi]];
                            } else {
                                fNextPtr[destBase+qi] = fPtr[src*Q+qi];
                            }
                        }
                    }
                }
            }
        }

        // Inlet BC — Zou/He упрощенный: все f = equilibrium(inlet)
        if (flowAxis == 0) {
            int ix = (flowSign > 0) ? 0 : Nx-1;
            float usqr = inUx*inUx + inUy*inUy + inUz*inUz;
            #ifdef _OPENMP
            #pragma omp parallel for
            #endif
            for (int z = 0; z < Nz; ++z) {
                for (int y = 0; y < Ny; ++y) {
                    int cell = (z*Ny + y)*Nx + ix;
                    if (cell < 0 || cell >= total) continue;
                    if (solidPtr[cell] || groundPtr[cell]) continue;
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
            #pragma omp parallel for
            #endif
            for (int z = 0; z < Nz; ++z) {
                for (int x = 0; x < Nx; ++x) {
                    int cell = (z*Ny + iy)*Nx + x;
                    if (cell < 0 || cell >= total) continue;
                    if (solidPtr[cell] || groundPtr[cell]) continue;
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
            #pragma omp parallel for
            #endif
            for (int y = 0; y < Ny; ++y) {
                for (int x = 0; x < Nx; ++x) {
                    int cell = (iz*Ny + y)*Nx + x;
                    if (cell < 0 || cell >= total) continue;
                    if (solidPtr[cell] || groundPtr[cell]) continue;
                    int base = cell*Q;
                    for (int qi = 0; qi < Q; ++qi) {
                        fNextPtr[base+qi] = computeEquilibriumFast(qi, 1.0f, inUx, inUy, inUz, usqr);
                    }
                }
            }
        }

        // Outlet — конвективный zero-gradient: копируем из соседней внутренней ячейки
        if (lbmParams.useConvectiveOutlet) {
            if (flowAxis == 0) {
                int ox = (flowSign > 0) ? Nx-1 : 0;
                int ix = (flowSign > 0) ? Nx-2 : 1;
                #ifdef _OPENMP
                #pragma omp parallel for
                #endif
                for (int z = 0; z < Nz; ++z) {
                    for (int y = 0; y < Ny; ++y) {
                        int cellOut = (z*Ny + y)*Nx + ox;
                        int cellIn = (z*Ny + y)*Nx + ix;
                        if (cellOut < 0 || cellOut >= total || cellIn < 0 || cellIn >= total) continue;
                        if (solidPtr[cellOut] || groundPtr[cellOut]) continue;
                        int baseOut = cellOut*Q;
                        int baseIn = cellIn*Q;
                        for (int qi = 0; qi < Q; ++qi) {
                            fNextPtr[baseOut+qi] = fPtr[baseIn+qi];
                        }
                    }
                }
            } else if (flowAxis == 1) {
                int oy = (flowSign > 0) ? Ny-1 : 0;
                int iy = (flowSign > 0) ? Ny-2 : 1;
                #ifdef _OPENMP
                #pragma omp parallel for
                #endif
                for (int z = 0; z < Nz; ++z) {
                    for (int x = 0; x < Nx; ++x) {
                        int cellOut = (z*Ny + oy)*Nx + x;
                        int cellIn = (z*Ny + iy)*Nx + x;
                        if (cellOut < 0 || cellOut >= total || cellIn < 0 || cellIn >= total) continue;
                        if (solidPtr[cellOut] || groundPtr[cellOut]) continue;
                        int baseOut = cellOut*Q;
                        int baseIn = cellIn*Q;
                        for (int qi = 0; qi < Q; ++qi) {
                            fNextPtr[baseOut+qi] = fPtr[baseIn+qi];
                        }
                    }
                }
            } else {
                int oz = (flowSign > 0) ? Nz-1 : 0;
                int iz = (flowSign > 0) ? Nz-2 : 1;
                #ifdef _OPENMP
                #pragma omp parallel for
                #endif
                for (int y = 0; y < Ny; ++y) {
                    for (int x = 0; x < Nx; ++x) {
                        int cellOut = (oz*Ny + y)*Nx + x;
                        int cellIn = (iz*Ny + y)*Nx + x;
                        if (cellOut < 0 || cellOut >= total || cellIn < 0 || cellIn >= total) continue;
                        if (solidPtr[cellOut] || groundPtr[cellOut]) continue;
                        int baseOut = cellOut*Q;
                        int baseIn = cellIn*Q;
                        for (int qi = 0; qi < Q; ++qi) {
                            fNextPtr[baseOut+qi] = fPtr[baseIn+qi];
                        }
                    }
                }
            }
        }

        f.swap(fNext);

        lbmCurrentStep++;
        lbmAvgRho = (float)(avgRhoAcc / total);
        lbmAvgKineticEnergy = (float)(kineticAcc / total);
        lbmMaxVelocityLB = maxVelGlobal;
        lbmTKE = (float)(tkeAcc / total);

        float scale = flowSpeed / U0mag;
        if (!std::isfinite(scale) || scale > 1000.0f) scale = 20.0f;
        if (scale < 0.01f) scale = 0.01f;
        lbmMaxVelocityWorld = maxVelGlobal * scale;

        lbmConvergence = std::fabs(lbmAvgKineticEnergy - prevKinetic);
        prevKinetic = lbmAvgKineticEnergy;
        lbmConverged = (lbmConvergence < lbmParams.convergenceThreshold);

        if (!std::isfinite(lbmAvgRho) || lbmAvgRho > 1.2f || lbmAvgRho < 0.8f) {
            std::cout << "[LBM] Divergence detected rho=" << lbmAvgRho << " — resetting" << std::endl;
            hasDiverged = true;
            break;
        }
    }

    if (hasDiverged) {
        float inUx2, inUy2, inUz2;
        computeInletVelocityLB(inUx2, inUy2, inUz2);
        float usqr = inUx2*inUx2 + inUy2*inUy2 + inUz2*inUz2;
        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int cell = 0; cell < total; ++cell) {
            if (lbmIsSolid[cell] || lbmIsGround[cell]) continue;
            int base = cell*Q;
            for (int qi = 0; qi < Q; ++qi) {
                f[base+qi] = computeEquilibriumFast(qi, 1.0f, inUx2, inUy2, inUz2, usqr);
            }
            lbmRho[cell] = 1.0f;
            lbmUx[cell] = inUx2; lbmUy[cell] = inUy2; lbmUz[cell] = inUz2;
        }
        lbmAvgRho = 1.0f;
        lbmAvgKineticEnergy = usqr;
    }

    {
        float inUx2, inUy2, inUz2;
        computeInletVelocityLB(inUx2, inUy2, inUz2);
        float U0mag2 = std::sqrt(inUx2*inUx2 + inUy2*inUy2 + inUz2*inUz2);
        if (!std::isfinite(U0mag2) || U0mag2 < 1e-6f) U0mag2 = 0.08f;
        float scale = flowSpeed / U0mag2;
        if (!std::isfinite(scale) || scale > 1000.0f) scale = 20.0f;
        float* uxW = lbmUxWorld.data();
        float* uyW = lbmUyWorld.data();
        float* uzW = lbmUzWorld.data();
        float* pPtr = lbmPressure.data();
        char* solidPtr2 = lbmIsSolid.data();
        char* groundPtr2 = lbmIsGround.data();
        float rho0 = airDensity; // физическая плотность для масштабирования давления
        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int cell = 0; cell < total; ++cell) {
            if (solidPtr2[cell] || groundPtr2[cell]) {
                uxW[cell] = 0; uyW[cell] = 0; uzW[cell] = 0;
                pPtr[cell] = 0;
            } else {
                float ux = lbmUx[cell], uy = lbmUy[cell], uz = lbmUz[cell];
                if (!std::isfinite(ux)) ux = 0;
                uxW[cell] = ux * scale;
                uyW[cell] = uy * scale;
                uzW[cell] = uz * scale;
                float rho = lbmRho[cell];
                if (!std::isfinite(rho)) rho = 1.0f;
                // v1.11.0: корректное давление p = rho0 * cs2*(rho-1) * scale^2
                float pLat = cs2 * (rho - 1.0f);
                float pPhys = pLat * scale * scale * rho0;
                if (!std::isfinite(pPhys)) pPhys = 0;
                // Ограничиваем физически разумными значениями: |p| < 0.5*q
                float q = 0.5f * rho0 * flowSpeed*flowSpeed;
                if (fabsf(pPhys) > q) pPhys = glm::clamp(pPhys, -q, q);
                pPtr[cell] = pPhys;
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
        if (abs(lbmNx - g_voxNx) > 2 || abs(lbmNy - g_voxNy) > 2 || abs(lbmNz - g_voxNz) > 2) {
            std::cout << "[LBM] Voxel grid changed, reinit" << std::endl;
            initLBM();
        }
        return;
    }
    int steps = lbmParams.stepsPerFrame;
    if (steps <= 0) steps = 1;
    if (steps > 50) steps = 50;
    if (deltaTime > 0.02f) steps = std::min(steps*2, 50);
    stepLBMCPU(steps);
    if (lbmCurrentStep % 30 == 0) computeLBMVorticityAndQ();
}

// v1.11.0: улучшенная трилинейная интерполяция — учитывает только жидкие ячейки
glm::vec3 getLBMVelocityLB(const glm::vec3& worldPos) {
    if (!lbmInitialized) return glm::vec3(0);
    if (!std::isfinite(worldPos.x)) return glm::vec3(0);
    float fx = (worldPos.x - lbmMinX) / lbmCellSizeX;
    float fy = (worldPos.y - lbmMinY) / lbmCellSizeY;
    float fz = (worldPos.z - lbmMinZ) / lbmCellSizeZ;
    int ix = (int)std::floor(fx);
    int iy = (int)std::floor(fy);
    int iz = (int)std::floor(fz);
    float tx = fx - (float)ix, ty = fy - (float)iy, tz = fz - (float)iz;
    tx = glm::clamp(tx, 0.0f, 1.0f);
    ty = glm::clamp(ty, 0.0f, 1.0f);
    tz = glm::clamp(tz, 0.0f, 1.0f);
    if (ix < 0 || ix >= lbmNx-1 || iy < 0 || iy >= lbmNy-1 || iz < 0 || iz >= lbmNz-1) return glm::vec3(0);
    const float* uxPtr = lbmUx.data();
    const float* uyPtr = lbmUy.data();
    const float* uzPtr = lbmUz.data();
    const char* solidPtr = lbmIsSolid.data();
    const char* groundPtr = lbmIsGround.data();

    // Собираем только жидкие ячейки, игнорируем твердые
    struct Sample { glm::vec3 v; float w; };
    Sample samples[8];
    int validCount = 0;
    float totalW = 0.0f;

    auto tryFetch = [&](int x,int y,int z, float weight) {
        if (x<0||x>=lbmNx||y<0||y>=lbmNy||z<0||z>=lbmNz) return;
        int cell = (z*lbmNy + y)*lbmNx + x;
        if (cell < 0 || cell >= (int)lbmIsSolid.size()) return;
        if (solidPtr[cell] || groundPtr[cell]) return; // пропускаем твердые
        glm::vec3 vv(uxPtr[cell], uyPtr[cell], uzPtr[cell]);
        if (!std::isfinite(vv.x)) return;
        samples[validCount].v = vv;
        samples[validCount].w = weight;
        totalW += weight;
        validCount++;
    };

    float w000 = (1-tx)*(1-ty)*(1-tz);
    float w100 = tx*(1-ty)*(1-tz);
    float w010 = (1-tx)*ty*(1-tz);
    float w110 = tx*ty*(1-tz);
    float w001 = (1-tx)*(1-ty)*tz;
    float w101 = tx*(1-ty)*tz;
    float w011 = (1-tx)*ty*tz;
    float w111 = tx*ty*tz;

    tryFetch(ix,iy,iz,w000);
    tryFetch(ix+1,iy,iz,w100);
    tryFetch(ix,iy+1,iz,w010);
    tryFetch(ix+1,iy+1,iz,w110);
    tryFetch(ix,iy,iz+1,w001);
    tryFetch(ix+1,iy,iz+1,w101);
    tryFetch(ix,iy+1,iz+1,w011);
    tryFetch(ix+1,iy+1,iz+1,w111);

    if (validCount == 0) return glm::vec3(0); // все твердые — no-slip
    if (totalW < 1e-6f) return samples[0].v;

    glm::vec3 result(0);
    for (int i=0;i<validCount;++i) result += samples[i].v * (samples[i].w / totalW);
    return std::isfinite(result.x) ? result : glm::vec3(0);
}

glm::vec3 getLBMVelocityWorld(const glm::vec3& worldPos) {
    if (!lbmInitialized) return glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
    if (worldPos.x < lbmMinX || worldPos.x > lbmMaxX || worldPos.y < lbmMinY || worldPos.y > lbmMaxY || worldPos.z < lbmMinZ || worldPos.z > lbmMaxZ)
        return glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
    // Проверка на твердое тело — возвращаем 0, а не freestream*0.1
    if (isLBMSolidWorld(worldPos)) return glm::vec3(0.0f);

    glm::vec3 vLB = getLBMVelocityLB(worldPos);
    float mag2 = vLB.x*vLB.x + vLB.y*vLB.y + vLB.z*vLB.z;
    if (mag2 < 1e-12f) return glm::vec3(0.0f); // в погранслое — 0, физично

    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    float U0mag = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    if (!std::isfinite(U0mag) || U0mag < 1e-6f) U0mag = 0.08f;
    float scale = flowSpeed / U0mag;
    if (!std::isfinite(scale) || scale > 1000.0f) scale = 20.0f;
    glm::vec3 vWorld = vLB * scale;
    if (!std::isfinite(vWorld.x)) return glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
    float maxV = flowSpeed * 2.0f; // ограничим 2*Vinf
    float mag = glm::length(vWorld);
    if (mag > maxV) vWorld *= maxV / mag;
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
    return std::isfinite(rho) ? glm::clamp(rho, 0.8f, 1.2f) : 1.0f;
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
    return lbmIsSolid[cell] != 0 || lbmIsGround[cell] != 0;
}

float getLBMVorticityWorld(const glm::vec3& worldPos) {
    if (!lbmInitialized || lbmVorticityMag.empty()) return 0.0f;
    float fx = (worldPos.x - lbmMinX) / lbmCellSizeX;
    float fy = (worldPos.y - lbmMinY) / lbmCellSizeY;
    float fz = (worldPos.z - lbmMinZ) / lbmCellSizeZ;
    int ix = (int)fx, iy = (int)fy, iz = (int)fz;
    if (ix < 0 || ix >= lbmNx || iy < 0 || iy >= lbmNy || iz < 0 || iz >= lbmNz) return 0.0f;
    int cell = (iz*lbmNy + iy)*lbmNx + ix;
    if (cell < 0 || cell >= (int)lbmVorticityMag.size()) return 0.0f;
    float v = lbmVorticityMag[cell];
    return std::isfinite(v) ? v : 0.0f;
}

float getLBMQWorld(const glm::vec3& worldPos) {
    if (!lbmInitialized || lbmQCriterion.empty()) return 0.0f;
    float fx = (worldPos.x - lbmMinX) / lbmCellSizeX;
    float fy = (worldPos.y - lbmMinY) / lbmCellSizeY;
    float fz = (worldPos.z - lbmMinZ) / lbmCellSizeZ;
    int ix = (int)fx, iy = (int)fy, iz = (int)fz;
    if (ix < 0 || ix >= lbmNx || iy < 0 || iy >= lbmNy || iz < 0 || iz >= lbmNz) return 0.0f;
    int cell = (iz*lbmNy + iy)*lbmNx + ix;
    if (cell < 0 || cell >= (int)lbmQCriterion.size()) return 0.0f;
    float q = lbmQCriterion[cell];
    return std::isfinite(q) ? q : 0.0f;
}

float getLBMTKEWorld(const glm::vec3& worldPos) {
    if (!lbmInitialized || lbmTKEField.empty()) return 0.0f;
    float fx = (worldPos.x - lbmMinX) / lbmCellSizeX;
    float fy = (worldPos.y - lbmMinY) / lbmCellSizeY;
    float fz = (worldPos.z - lbmMinZ) / lbmCellSizeZ;
    int ix = (int)fx, iy = (int)fy, iz = (int)fz;
    if (ix < 0 || ix >= lbmNx || iy < 0 || iy >= lbmNy || iz < 0 || iz >= lbmNz) return 0.0f;
    int cell = (iz*lbmNy + iy)*lbmNx + ix;
    if (cell < 0 || cell >= (int)lbmTKEField.size()) return 0.0f;
    float tke = lbmTKEField[cell];
    return std::isfinite(tke) ? tke : 0.0f;
}

float getLBMVelocityMagWorld(const glm::vec3& worldPos) {
    glm::vec3 v = getLBMVelocityWorld(worldPos);
    float m = glm::length(v);
    return std::isfinite(m) ? m : 0.0f;
}

float getLBMStrainWorld(const glm::vec3& worldPos) {
    if (!lbmInitialized || lbmStrainMag.empty()) return 0.0f;
    float fx = (worldPos.x - lbmMinX) / lbmCellSizeX;
    float fy = (worldPos.y - lbmMinY) / lbmCellSizeY;
    float fz = (worldPos.z - lbmMinZ) / lbmCellSizeZ;
    int ix = (int)fx, iy = (int)fy, iz = (int)fz;
    if (ix < 0 || ix >= lbmNx || iy < 0 || iy >= lbmNy || iz < 0 || iz >= lbmNz) return 0.0f;
    int cell = (iz*lbmNy + iy)*lbmNx + ix;
    if (cell < 0 || cell >= (int)lbmStrainMag.size()) return 0.0f;
    float s = lbmStrainMag[cell];
    return std::isfinite(s) ? s : 0.0f;
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
    float* tkePtr = lbmTKEField.data();
    float* strainPtr = lbmStrainMag.data();
    const char* solidPtr = lbmIsSolid.data();
    const char* groundPtr = lbmIsGround.data();
    float csx = lbmCellSizeX, csy = lbmCellSizeY, csz = lbmCellSizeZ;
    if (csx < 1e-6f) csx = 0.1f;
    if (csy < 1e-6f) csy = 0.1f;
    if (csz < 1e-6f) csz = 0.1f;
    float invCsx = 1.0f / csx, invCsy = 1.0f / csy, invCsz = 1.0f / csz;

    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (int z = 1; z < Nz-1; ++z) {
        for (int y = 1; y < Ny-1; ++y) {
            for (int x = 1; x < Nx-1; ++x) {
                int cell = (z*Ny + y)*Nx + x;
                if (solidPtr[cell] || groundPtr[cell]) { vortPtr[cell]=0; qPtr[cell]=0; continue; }

                int xm = cell-1, xp = cell+1;
                int ym = cell-Nx, yp = cell+Nx;
                int zm = cell-Nx*Ny, zp = cell+Nx*Ny;

                if (xm <0 || xp>=total || ym<0 || yp>=total || zm<0 || zp>=total) {
                    vortPtr[cell]=0; qPtr[cell]=0; continue;
                }

                float dux_dx = (uxW[xp] - uxW[xm]) * 0.5f * invCsx;
                float dux_dy = (uxW[yp] - uxW[ym]) * 0.5f * invCsy;
                float dux_dz = (uxW[zp] - uxW[zm]) * 0.5f * invCsz;
                float duy_dx = (uyW[xp] - uyW[xm]) * 0.5f * invCsx;
                float duy_dy = (uyW[yp] - uyW[ym]) * 0.5f * invCsy;
                float duy_dz = (uyW[zp] - uyW[zm]) * 0.5f * invCsz;
                float duz_dx = (uzW[xp] - uzW[xm]) * 0.5f * invCsx;
                float duz_dy = (uzW[yp] - uzW[ym]) * 0.5f * invCsy;
                float duz_dz = (uzW[zp] - uzW[zm]) * 0.5f * invCsz;

                if (!std::isfinite(dux_dx)) { vortPtr[cell]=0; qPtr[cell]=0; continue; }

                float wx = duz_dy - duy_dz;
                float wy = dux_dz - duz_dx;
                float wz = duy_dx - dux_dy;
                float vortMag = std::sqrt(wx*wx + wy*wy + wz*wz);
                if (!std::isfinite(vortMag)) vortMag = 0;
                if (vortMag > 1000.0f) vortMag = 1000.0f;
                vortPtr[cell] = vortMag;

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
                if (Q > 1e6f) Q = 1e6f;
                if (Q < -1e6f) Q = -1e6f;
                qPtr[cell] = Q;
                float strain = std::sqrt(2.0f*S2);
                if (!std::isfinite(strain)) strain = 0;
                strainPtr[cell] = strain;
                float tke = 0.5f * (vortMag*vortMag) * 0.01f + S2*0.005f;
                if (!std::isfinite(tke)) tke = 0;
                if (tke > 10.0f) tke = 10.0f;
                tkePtr[cell] = tke;
            }
        }
    }
}

float computeLBMRe() {
    // v1.11.0: физический Re, не решеточный
    const float mu = 1.81e-5f;
    float L = maxDim;
    if (L < 1e-6f) L = 1.0f;
    float Re = airDensity * flowSpeed * L / mu;
    if (!std::isfinite(Re)) Re = aeroReNumber;
    if (Re > 1e9f) Re = 1e9f;
    return Re;
}

float computeLBMRefArea() {
    if (aeroAutoRefArea) {
        // v1.11.0: проекционная площадь на плоскость перпендикулярную потоку
        float az = glm::radians(flowAzimuth);
        float el = glm::radians(flowElevation);
        float ce = cosf(el), se = sinf(el);
        float ca = cosf(az), sa = sinf(az);
        glm::vec3 flowDir(ce*ca, se, ce*sa);
        float len = glm::length(flowDir);
        if (len > 1e-6f) flowDir /= len;
        else flowDir = glm::vec3(1,0,0);

        // Если есть треугольники — считаем проекцию
        if (g_vertices.size() >= 9) {
            float projArea = 0.0f;
            int triCount = (int)(g_vertices.size() / 9);
            float* vertPtr = g_vertices.data();
            for (int ti=0; ti<triCount; ++ti) {
                int i = ti*9;
                if (i+8 >= (int)g_vertices.size()) continue;
                glm::vec3 v0(vertPtr[i], vertPtr[i+1], vertPtr[i+2]);
                glm::vec3 v1(vertPtr[i+3], vertPtr[i+4], vertPtr[i+5]);
                glm::vec3 v2(vertPtr[i+6], vertPtr[i+7], vertPtr[i+8]);
                glm::vec3 cr = glm::cross(v1-v0, v2-v0);
                float area = 0.5f * glm::length(cr);
                if (!std::isfinite(area) || area < 1e-12f) continue;
                glm::vec3 n = glm::normalize(cr);
                // Проекция на направление потока — только передняя часть
                float dot = fabsf(glm::dot(n, flowDir));
                // Для замкнутого тела фронтальная площадь = 0.5 * sum(area*|dot|)
                projArea += area * dot;
            }
            float frontal = projArea * 0.5f;
            if (std::isfinite(frontal) && frontal > 1e-6f && frontal < 1e6f) {
                return frontal;
            }
        }

        // Fallback — bbox проекция
        float sizeY = maxBB.y - minBB.y;
        float sizeZ = maxBB.z - minBB.z;
        float sizeX = maxBB.x - minBB.x;
        if (!std::isfinite(sizeY) || sizeY < 0.01f) sizeY = maxDim;
        if (!std::isfinite(sizeZ) || sizeZ < 0.01f) sizeZ = maxDim;
        if (!std::isfinite(sizeX) || sizeX < 0.01f) sizeX = maxDim;
        // Проекция bbox на плоскость перпендикулярную потоку — упрощенно
        float area = (fabsf(flowDir.x)*sizeY*sizeZ + fabsf(flowDir.y)*sizeX*sizeZ + fabsf(flowDir.z)*sizeX*sizeY);
        if (!std::isfinite(area) || area < 1e-6f) area = maxDim*maxDim*0.5f;
        return area;
    }
    if (!std::isfinite(aeroRefArea) || aeroRefArea < 1e-6f) return 1.0f;
    return aeroRefArea;
}

void computeLBMForcesFromLBM() {
    lbmReynolds = computeLBMRe();
    aeroReNumber = lbmReynolds;
}

bool lbmValidateInitialization() {
    if (!lbmInitialized) return false;
    if (lbmNx <= 0 || lbmNy <= 0 || lbmNz <= 0) return false;
    int total = lbmNx*lbmNy*lbmNz;
    if ((int)lbmRho.size() != total) return false;
    if ((int)f.size() != total*Q) return false;
    for (float rho : lbmRho) if (!std::isfinite(rho) || rho < 0.5f || rho > 1.5f) return false;
    return true;
}
bool lbmValidateConservation() {
    if (!lbmInitialized) return true;
    double avg = 0;
    int count = 0;
    for (float r : lbmRho) if (std::isfinite(r)) { avg += r; count++; }
    if (count == 0) return false;
    avg /= count;
    return std::fabs(avg - 1.0f) <= 0.1f; // строже — 10%
}
bool lbmValidateBoundaryConditions() {
    if (!lbmInitialized) return true;
    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    float mag = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    return std::isfinite(mag) && mag >= 1e-6f && mag <= 0.2f;
}
bool lbmValidateSolidHandling() {
    if (!lbmInitialized) return true;
    for (size_t i = 0; i < lbmIsSolid.size(); ++i) {
        if (lbmIsSolid[i]) {
            if (std::fabs(lbmUx[i]) > 1e-4f || std::fabs(lbmUy[i]) > 1e-4f || std::fabs(lbmUz[i]) > 1e-4f) return false;
        }
    }
    return true;
}
bool lbmValidateRealisticAero() {
    if (!lbmInitialized) return true;
    float maxP = -1e9f, minP = 1e9f;
    for (float p : lbmPressure) {
        if (!std::isfinite(p)) return false;
        if (p > maxP) maxP = p;
        if (p < minP) minP = p;
    }
    float q = 0.5f * airDensity * flowSpeed*flowSpeed;
    if (fabsf(maxP) > q*1.5f || fabsf(minP) > q*1.5f) return false;
    for (float tke : lbmTKEField) if (tke < -1e-3f || !std::isfinite(tke) || tke > 10.0f) return false;
    for (float v : lbmVorticityMag) if (v < -1e-3f || !std::isfinite(v) || v > 1e4f) return false;
    return true;
}
bool lbmValidateStability() {
    if (!lbmInitialized) return true;
    if (!std::isfinite(lbmAvgRho) || lbmAvgRho < 0.8f || lbmAvgRho > 1.2f) return false;
    if (!std::isfinite(lbmMaxVelocityLB) || lbmMaxVelocityLB > 0.35f) return false;
    if (!std::isfinite(lbmAvgKineticEnergy) || lbmAvgKineticEnergy > 0.1f) return false;
    return true;
}

float getLBMMachWorld(const glm::vec3& worldPos) {
    glm::vec3 v = getLBMVelocityWorld(worldPos);
    float mag = glm::length(v);
    if (!std::isfinite(mag)) return 0.0f;
    float a = speedOfSound;
    if (a < 1.0f) a = 340.0f;
    return mag / a;
}

float getLBMHelicityWorld(const glm::vec3& worldPos) {
    if (!lbmInitialized) return 0.0f;
    float fx = (worldPos.x - lbmMinX) / lbmCellSizeX;
    float fy = (worldPos.y - lbmMinY) / lbmCellSizeY;
    float fz = (worldPos.z - lbmMinZ) / lbmCellSizeZ;
    int ix = (int)fx, iy = (int)fy, iz = (int)fz;
    if (ix <= 0 || ix >= lbmNx-1 || iy <= 0 || iy >= lbmNy-1 || iz <= 0 || iz >= lbmNz-1) return 0.0f;
    int cell = (iz*lbmNy + iy)*lbmNx + ix;
    if (cell < 0 || cell >= (int)lbmVorticityMag.size()) return 0.0f;
    glm::vec3 vel = getLBMVelocityWorld(worldPos);
    int xm = cell-1, xp = cell+1;
    int ym = cell-lbmNx, yp = cell+lbmNx;
    int zm = cell-lbmNx*lbmNy, zp = cell+lbmNx*lbmNy;
    int total = lbmNx*lbmNy*lbmNz;
    if (xm<0||xp>=total||ym<0||yp>=total||zm<0||zp>=total) return 0.0f;
    float duz_dy = (lbmUzWorld[yp] - lbmUzWorld[ym]) * 0.5f;
    float duy_dz = (lbmUyWorld[zp] - lbmUyWorld[zm]) * 0.5f;
    float dux_dz = (lbmUxWorld[zp] - lbmUxWorld[zm]) * 0.5f;
    float duz_dx = (lbmUzWorld[xp] - lbmUzWorld[xm]) * 0.5f;
    float duy_dx = (lbmUyWorld[xp] - lbmUyWorld[xm]) * 0.5f;
    float dux_dy = (lbmUxWorld[yp] - lbmUxWorld[ym]) * 0.5f;
    glm::vec3 omega(duz_dy - duy_dz, dux_dz - duz_dx, duy_dx - dux_dy);
    float helicity = glm::dot(vel, omega);
    if (!std::isfinite(helicity)) return 0.0f;
    float vMag = glm::length(vel);
    float oMag = glm::length(omega);
    if (vMag < 1e-6f || oMag < 1e-6f) return 0.0f;
    return helicity / (vMag * oMag);
}

float getLBMTotalPressureWorld(const glm::vec3& worldPos) {
    glm::vec3 v = getLBMVelocityWorld(worldPos);
    float mag2 = glm::dot(v,v);
    if (!std::isfinite(mag2)) return airPressure;
    float rho = airDensity;
    float pStatic = airPressure;
    // v1.11.0 fix: скобки для тернарного оператора
    float lbmP = 0.0f;
    float fx = (worldPos.x - lbmMinX) / lbmCellSizeX;
    float fy = (worldPos.y - lbmMinY) / lbmCellSizeY;
    float fz = (worldPos.z - lbmMinZ) / lbmCellSizeZ;
    int ix = (int)fx, iy = (int)fy, iz = (int)fz;
    if (ix>=0 && ix<lbmNx && iy>=0 && iy<lbmNy && iz>=0 && iz<lbmNz) {
        int cell = (iz*lbmNy + iy)*lbmNx + ix;
        if (cell>=0 && cell < (int)lbmPressure.size()) {
            lbmP = lbmPressure[cell];
        }
    }
    pStatic += lbmP;
    float pt = pStatic + 0.5f * rho * mag2;
    return std::isfinite(pt) ? pt : airPressure;
}

void drawLBMUI() {}
