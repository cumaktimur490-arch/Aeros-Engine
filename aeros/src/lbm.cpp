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
#include <random>

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

inline float computeEquilibriumFast(int qi, float rho, float ux, float uy, float uz, float usqr) {
    if (!std::isfinite(rho) || rho < 0.1f) rho = 1.0f;
    if (!std::isfinite(usqr) || usqr > 1.0f) {
        // Clamp velocity for stability
        float mag = sqrtf(usqr);
        if (mag > 0.3f) {
            float scale = 0.3f / mag;
            ux *= scale; uy *= scale; uz *= scale;
            usqr = ux*ux + uy*uy + uz*uz;
        }
    }
    float cu = (float)c[qi][0]*ux + (float)c[qi][1]*uy + (float)c[qi][2]*uz;
    // Clamp cu for stability
    if (cu > 0.5f) cu = 0.5f;
    if (cu < -0.5f) cu = -0.5f;
    float feq = w[qi]*rho*(1.0f + cu*invCs2 + cu*cu*invCs2SqHalf - usqr*1.5f);
    if (!std::isfinite(feq) || feq < -0.1f) feq = w[qi]*rho;
    if (feq > 2.0f) feq = w[qi]*rho*1.5f;
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
    if (!std::isfinite(U0) || U0 < 1e-6f) U0 = 0.1f;
    if (U0 > 0.25f) U0 = 0.25f;
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
// Цветовые карты v1.8.0 — улучшенные как на фото
// =====================================================
glm::vec3 getRealisticPressureColor(float cp) {
    if (!std::isfinite(cp)) cp = 0;
    float t = (cp + 3.0f) / 4.0f;
    t = glm::clamp(t, 0.0f, 1.0f);
    
    // AeroColorMap support
    if (aeroColorMap == 1) { // viridis for pressure
        // Map Cp to viridis: low (blue) = low pressure, high (yellow) = high
        if (t < 0.25f) { float k=t/0.25f; return glm::vec3(0.267f+k*0.1f, 0.004f+k*0.2f, 0.329f+k*0.3f); }
        else if (t < 0.5f) { float k=(t-0.25f)/0.25f; return glm::vec3(0.22f+k*0.05f, 0.3f+k*0.2f, 0.5f); }
        else if (t < 0.75f) { float k=(t-0.5f)/0.25f; return glm::vec3(0.12f+k*0.4f, 0.5f+k*0.2f, 0.5f-k*0.2f); }
        else { float k=(t-0.75f)/0.25f; return glm::vec3(0.5f+k*0.49f, 0.7f+k*0.2f, 0.3f-k*0.1f); }
    } else if (aeroColorMap == 3) { // coolwarm
        if (t < 0.5f) { float k=t*2.0f; return glm::vec3(0.2f+k*0.3f, 0.3f+k*0.3f, 0.9f-k*0.2f); }
        else { float k=(t-0.5f)*2.0f; return glm::vec3(0.5f+k*0.5f, 0.6f-k*0.4f, 0.7f-k*0.5f); }
    }
    
    // Default rainbow NASCAR
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

    if (aeroColorMap == 1) { // viridis
        if (t < 0.25f) { float k=t/0.25f; return glm::vec3(0.267f, 0.004f+k*0.3f, 0.329f+k*0.2f); }
        else if (t < 0.5f) { float k=(t-0.25f)/0.25f; return glm::vec3(0.229f+k*0.0f, 0.322f+k*0.2f, 0.545f); }
        else if (t < 0.75f) { float k=(t-0.5f)/0.25f; return glm::vec3(0.127f+k*0.4f, 0.566f+k*0.2f, 0.550f-k*0.2f); }
        else { float k=(t-0.75f)/0.25f; return glm::vec3(0.5f+k*0.49f, 0.79f+k*0.1f, 0.3f); }
    } else if (aeroColorMap == 2) { // parula
        if (t < 0.2f) return glm::vec3(t*5.0f*0.1f, t*2.0f, 0.8f);
        else if (t < 0.4f) { float k=(t-0.2f)/0.2f; return glm::vec3(0.1f+k*0.1f, 0.4f+k*0.4f, 0.8f-k*0.2f); }
        else if (t < 0.6f) { float k=(t-0.4f)/0.2f; return glm::vec3(0.2f+k*0.5f, 0.8f, 0.6f-k*0.4f); }
        else if (t < 0.8f) { float k=(t-0.6f)/0.2f; return glm::vec3(0.7f+k*0.3f, 0.8f-k*0.4f, 0.2f); }
        else { float k=(t-0.8f)/0.2f; return glm::vec3(1.0f, 0.4f-k*0.3f, k*0.1f); }
    }

    // Default rainbow blue->red
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

void initLBM() {
    std::cout << "[LBM] Initializing v1.8.0 realistic..." << std::endl;
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

    // Clamp grid size
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
        // Voxel grid size mismatch — map with scaling
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

    // Более реалистичная инициализация с небольшим шумом
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-0.01f, 0.01f);

    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (int cell = 0; cell < total; ++cell) {
        float rho = 1.0f;
        float ux = 0, uy = 0, uz = 0;
        if (!solidPtr[cell] && !groundPtr[cell]) { ux = inUx; uy = inUy; uz = inUz; }
        if (lbmParams.inletTurbulence > 0 && !solidPtr[cell] && !groundPtr[cell]) {
            float turb = lbmParams.inletTurbulence;
            // Perlin-like turbulence using sin with different frequencies
            float rx = sinf(cell*0.1f) * turb * 0.1f + sinf(cell*0.023f)*turb*0.05f;
            float ry = cosf(cell*0.13f) * turb * 0.1f + cosf(cell*0.031f)*turb*0.05f;
            float rz = sinf(cell*0.07f) * turb * 0.1f + sinf(cell*0.017f)*turb*0.05f;
            ux += rx; uy += ry; uz += rz;
        }
        float usqr = ux*ux + uy*uy + uz*uz;
        if (usqr > 0.09f) {
            float scale = 0.3f / sqrtf(usqr);
            ux *= scale; uy *= scale; uz *= scale;
            usqr = ux*ux + uy*uy + uz*uz;
        }
        int base = cell*Q;
        for (int qi = 0; qi < Q; ++qi) {
            fPtr[base+qi] = computeEquilibriumFast(qi, rho, ux, uy, uz, usqr);
        }
    }

    if (!std::isfinite(lbmParams.tau) || lbmParams.tau < 0.51f) lbmParams.tau = 0.51f;
    if (lbmParams.tau > 2.0f) lbmParams.tau = 2.0f;
    lbmParams.viscosity = (lbmParams.tau - 0.5f) * cs2;
    lbmParams.U0 = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    if (!std::isfinite(lbmParams.U0) || lbmParams.U0 < 1e-6f) lbmParams.U0 = 0.1f;

    lbmCurrentStep = 0;
    lbmConverged = false;
    lbmInitialized = true;

    float L = (float)std::max({lbmNx, lbmNy, lbmNz});
    if (maxDim > 1e-6f) L = maxDim / lbmCellSizeX;
    lbmReynolds = lbmParams.U0 * L / (lbmParams.viscosity + 1e-6f);
    aeroReNumber = lbmReynolds;

    auto t1 = std::chrono::high_resolution_clock::now();
    lbmTimeMs = std::chrono::duration<float, std::milli>(t1-t0).count();
    std::cout << "[LBM] Initialized REALISTIC v1.8.0: " << lbmNx << "x" << lbmNy << "x" << lbmNz << " = " << total
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
    // Keep memory allocated for fast reinit, but clear flags
}

void stepLBMCPU(int steps) {
    if (!lbmInitialized) return;
    if (lbmNx <= 0 || lbmNy <= 0 || lbmNz <= 0) return;
    int total = lbmNx * lbmNy * lbmNz;
    if (total <= 0) return;
    if (total != (int)lbmRho.size()) return;
    if (f.size() != (size_t)total*Q) return;

    auto t0 = std::chrono::high_resolution_clock::now();

    float tau0 = lbmParams.tau;
    if (!std::isfinite(tau0) || tau0 < 0.51f) tau0 = 0.6f;
    if (tau0 > 2.0f) tau0 = 2.0f;
    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    float U0mag = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    if (!std::isfinite(U0mag) || U0mag < 1e-6f) U0mag = 0.1f;

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
        #pragma omp parallel reduction(+:avgRhoAcc,kineticAcc,tkeAcc,nanCount)
        {
            float maxVelLocal = 0.0f;
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
                if (fi < -0.5f) fi = w[qi];
                if (fi > 2.0f) fi = w[qi];
                rho += fi;
                ux += fi * (float)c[qi][0];
                uy += fi * (float)c[qi][1];
                uz += fi * (float)c[qi][2];
            }
            if (rho < 0.1f || rho > 3.0f || !std::isfinite(rho)) { rho = 1.0f; nanCount++; }
            float invRho = 1.0f / rho;
            ux *= invRho; uy *= invRho; uz *= invRho;
            if (!std::isfinite(ux)) { ux = 0; uy = 0; uz = 0; nanCount++; }

            // Clamp velocity for stability
            float usqrTmp = ux*ux + uy*uy + uz*uz;
            if (usqrTmp > 0.25f) {
                float scale = 0.5f / sqrtf(usqrTmp);
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
                            if (std::isfinite(S) && S < 10.0f) {
                                float Cs = lbmParams.smagorinskyC;
                                if (!std::isfinite(Cs) || Cs < 0) Cs = 0.12f;
                                if (Cs > 0.5f) Cs = 0.5f;
                                float nu_t = Cs*Cs * S;
                                tauEff = tau0 + nu_t / cs2;
                                if (tauEff > 2.0f) tauEff = 2.0f;
                                if (tauEff < 0.51f) tauEff = 0.51f;
                            }
                        }
                    }
                }
            }

            float usqr = ux*ux + uy*uy + uz*uz;
            if (usqr > 0.25f) {
                float scale = 0.5f / sqrtf(usqr);
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
                if (fNew < -0.1f) fNew = feq;
                if (fNew > 2.0f) fNew = feq;
                fPtr[base+qi] = fNew;
            }

            rhoPtr[cell] = rho;
            uxPtr[cell] = ux; uyPtr[cell] = uy; uzPtr[cell] = uz;
            strainPtr[cell] = strainMag;
            float tke = 0.5f * usqr * (tauEff - tau0) / cs2;
            if (!std::isfinite(tke) || tke < 0) tke = 0;
            if (tke > 1.0f) tke = 1.0f;
            tkePtr[cell] = tke;

            float velMag = std::sqrt(usqr);
            #ifdef _OPENMP
            if (velMag > maxVelLocal) maxVelLocal = velMag;
            #else
            if (velMag > maxVelGlobal) maxVelGlobal = velMag;
            #endif
            avgRhoAcc += rho;
            kineticAcc += usqr;
            tkeAcc += tke;
        }
        #ifdef _OPENMP
            #pragma omp critical
            { if (maxVelLocal > maxVelGlobal) maxVelGlobal = maxVelLocal; }
        }
        #endif

        if (nanCount > total/10) {
            std::cout << "[LBM] Warning: many NaN/invalid (" << nanCount << ") — possible instability, resetting" << std::endl;
            hasDiverged = true;
            break;
        }

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
                            fNextPtr[destBase+qi] = fPtr[destBase+qi];
                        } else {
                            int src = (sz*Ny + sy)*Nx + sx;
                            if (src < 0 || src >= total) {
                                fNextPtr[destBase+qi] = fPtr[destBase+qi];
                            } else if (solidPtr[src] || groundPtr[src]) {
                                fNextPtr[destBase+qi] = fPtr[destBase+opp[qi]];
                            } else {
                                fNextPtr[destBase+qi] = fPtr[src*Q+qi];
                            }
                        }
                    }
                }
            }
        }

        // Inlet BC
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

        if (!std::isfinite(lbmAvgRho) || lbmAvgRho > 2.0f || lbmAvgRho < 0.5f) {
            std::cout << "[LBM] Divergence detected rho=" << lbmAvgRho << " — resetting" << std::endl;
            hasDiverged = true;
            break;
        }
    }

    if (hasDiverged) {
        // Soft reset to inlet
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
        if (!std::isfinite(U0mag2) || U0mag2 < 1e-6f) U0mag2 = 0.1f;
        float scale = flowSpeed / U0mag2;
        if (!std::isfinite(scale) || scale > 1000.0f) scale = 20.0f;
        float* uxW = lbmUxWorld.data();
        float* uyW = lbmUyWorld.data();
        float* uzW = lbmUzWorld.data();
        float* pPtr = lbmPressure.data();
        char* solidPtr2 = lbmIsSolid.data();
        char* groundPtr2 = lbmIsGround.data();
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
                pPtr[cell] = cs2 * (rho - 1.0f) * scale * scale;
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
        // Only reinit if difference is large
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
    auto fetch = [&](int x,int y,int z, glm::vec3& out)->bool {
        if (x<0||x>=lbmNx||y<0||y>=lbmNy||z<0||z>=lbmNz) { out=glm::vec3(0); return false; }
        int cell = (z*lbmNy + y)*lbmNx + x;
        if (cell < 0 || cell >= (int)lbmIsSolid.size()) { out=glm::vec3(0); return false; }
        if (solidPtr[cell] || groundPtr[cell]) { out=glm::vec3(0); return false; }
        out.x = uxPtr[cell]; out.y = uyPtr[cell]; out.z = uzPtr[cell];
        if (!std::isfinite(out.x)) out = glm::vec3(0);
        return true;
    };
    glm::vec3 c000,c100,c010,c110,c001,c101,c011,c111;
    fetch(ix,iy,iz,c000); fetch(ix+1,iy,iz,c100); fetch(ix,iy+1,iz,c010); fetch(ix+1,iy+1,iz,c110);
    fetch(ix,iy,iz+1,c001); fetch(ix+1,iy,iz+1,c101); fetch(ix,iy+1,iz+1,c011); fetch(ix+1,iy+1,iz+1,c111);
    glm::vec3 c00 = c000*(1-tx) + c100*tx;
    glm::vec3 c01 = c001*(1-tx) + c101*tx;
    glm::vec3 c10 = c010*(1-tx) + c110*tx;
    glm::vec3 c11 = c011*(1-tx) + c111*tx;
    glm::vec3 c0 = c00*(1-ty) + c10*ty;
    glm::vec3 c1 = c01*(1-ty) + c11*ty;
    glm::vec3 c = c0*(1-tz) + c1*tz;
    return std::isfinite(c.x) ? c : glm::vec3(0);
}

glm::vec3 getLBMVelocityWorld(const glm::vec3& worldPos) {
    if (!lbmInitialized) return glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
    if (worldPos.x < lbmMinX || worldPos.x > lbmMaxX || worldPos.y < lbmMinY || worldPos.y > lbmMaxY || worldPos.z < lbmMinZ || worldPos.z > lbmMaxZ)
        return glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
    glm::vec3 vLB = getLBMVelocityLB(worldPos);
    float mag2 = vLB.x*vLB.x + vLB.y*vLB.y + vLB.z*vLB.z;
    if (mag2 < 1e-12f) return glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz) * 0.1f;
    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    float U0mag = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    if (!std::isfinite(U0mag) || U0mag < 1e-6f) U0mag = 0.1f;
    float scale = flowSpeed / U0mag;
    if (!std::isfinite(scale) || scale > 1000.0f) scale = 20.0f;
    glm::vec3 vWorld = vLB * scale;
    if (!std::isfinite(vWorld.x)) return glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
    // Clamp world velocity
    float maxV = flowSpeed * 3.0f;
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
    return std::isfinite(rho) ? glm::clamp(rho, 0.5f, 1.5f) : 1.0f;
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
    if (!lbmInitialized) return aeroReNumber;
    float L = maxDim;
    if (L < 1e-6f) L = (float)std::max({lbmNx, lbmNy, lbmNz}) * lbmCellSizeX;
    float nu = lbmParams.viscosity;
    if (nu < 1e-6f) nu = 0.033f;
    // Convert lattice viscosity to physical: nu_phys = nu_lat * (U_phys/U_lat) * (L_lat/L_phys) ... simplified
    float Re = lbmParams.U0 * (L / lbmCellSizeX) / (nu + 1e-6f);
    if (!std::isfinite(Re)) Re = aeroReNumber;
    return Re;
}

float computeLBMRefArea() {
    if (aeroAutoRefArea) {
        float sizeY = maxBB.y - minBB.y;
        float sizeZ = maxBB.z - minBB.z;
        if (!std::isfinite(sizeY) || sizeY < 0.01f) sizeY = maxDim;
        if (!std::isfinite(sizeZ) || sizeZ < 0.01f) sizeZ = maxDim;
        if (sizeY < 1e-6f) sizeY = 1.0f;
        if (sizeZ < 1e-6f) sizeZ = 1.0f;
        float area = sizeY * sizeZ * 0.6f;
        if (!std::isfinite(area) || area < 1e-6f) area = maxDim*maxDim*0.5f;
        if (area < 1e-6f) area = 1.0f;
        return area;
    }
    if (!std::isfinite(aeroRefArea) || aeroRefArea < 1e-6f) return 1.0f;
    return aeroRefArea;
}

void computeLBMForcesFromLBM() {
    lbmReynolds = computeLBMRe();
    aeroReNumber = lbmReynolds;
    // aeroRefArea updated elsewhere
}

bool lbmValidateInitialization() {
    if (!lbmInitialized) return false;
    if (lbmNx <= 0 || lbmNy <= 0 || lbmNz <= 0) return false;
    int total = lbmNx*lbmNy*lbmNz;
    if ((int)lbmRho.size() != total) return false;
    if ((int)f.size() != total*Q) return false;
    for (float rho : lbmRho) if (!std::isfinite(rho) || rho < 0.3f || rho > 3.0f) return false;
    return true;
}
bool lbmValidateConservation() {
    if (!lbmInitialized) return true;
    double avg = 0;
    int count = 0;
    for (float r : lbmRho) if (std::isfinite(r)) { avg += r; count++; }
    if (count == 0) return false;
    avg /= count;
    return std::fabs(avg - 1.0f) <= 0.15f;
}
bool lbmValidateBoundaryConditions() {
    if (!lbmInitialized) return true;
    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    float mag = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    return std::isfinite(mag) && mag >= 1e-6f && mag <= 0.35f;
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
    if (fabsf(maxP) > 5000.0f || fabsf(minP) > 5000.0f) return false;
    for (float tke : lbmTKEField) if (tke < -1e-3f || !std::isfinite(tke) || tke > 100.0f) return false;
    for (float v : lbmVorticityMag) if (v < -1e-3f || !std::isfinite(v) || v > 1e4f) return false;
    return true;
}
bool lbmValidateStability() {
    if (!lbmInitialized) return true;
    if (!std::isfinite(lbmAvgRho) || lbmAvgRho < 0.5f || lbmAvgRho > 1.5f) return false;
    if (!std::isfinite(lbmMaxVelocityLB) || lbmMaxVelocityLB > 1.0f) return false;
    if (!std::isfinite(lbmAvgKineticEnergy) || lbmAvgKineticEnergy > 1.0f) return false;
    return true;
}

void drawLBMUI() {}
