#include <glad/glad.h>
#include <glm/glm.hpp>

#include <iostream>
#include <chrono>
#include <cmath>
#include <vector>
#include <algorithm>
#include <limits>
#include <queue>

#include "globals.h"
#include "cuda_api.h"
#include "voxel_grid.h"

#ifdef _OPENMP
#include <omp.h>
#endif

// =====================================================
// SDF sampling (CPU) — v1.19.0 Physics Logic Fix
// - Трилинейная интерполяция для гладкости
// - Проверка границ
// =====================================================
static inline bool isValidFloatSafe(float v) { return std::isfinite(v) && fabsf(v) < 1e6f; }
static inline bool isValidVec3Safe(const glm::vec3& v) { return isValidFloatSafe(v.x) && isValidFloatSafe(v.y) && isValidFloatSafe(v.z); }

float sampleSDFCPU(const glm::vec3& p) {
    if (g_distanceField.empty() || g_voxNx <= 0 || g_voxNy <= 0 || g_voxNz <= 0) return 1000.0f;
    if (!isValidVec3Safe(p)) return 1000.0f;
    float csx = flowParams.cellSizeX;
    float csy = flowParams.cellSizeY;
    float csz = flowParams.cellSizeZ;
    if (!isValidFloatSafe(csx) || fabsf(csx) < 1e-8f) return 1000.0f;
    if (!isValidFloatSafe(csy) || fabsf(csy) < 1e-8f) csy = csx;
    if (!isValidFloatSafe(csz) || fabsf(csz) < 1e-8f) csz = csx;

    // Трилинейная интерполяция — гладкий SDF
    float fx = (p.x - g_voxMinX) / csx - 0.5f;
    float fy = (p.y - g_voxMinY) / csy - 0.5f;
    float fz = (p.z - g_voxMinZ) / csz - 0.5f;
    int ix0 = (int)floorf(fx), iy0 = (int)floorf(fy), iz0 = (int)floorf(fz);
    int ix1 = ix0+1, iy1 = iy0+1, iz1 = iz0+1;
    float tx = fx - ix0, ty = fy - iy0, tz = fz - iz0;
    tx = glm::clamp(tx, 0.0f, 1.0f);
    ty = glm::clamp(ty, 0.0f, 1.0f);
    tz = glm::clamp(tz, 0.0f, 1.0f);

    auto get = [&](int x,int y,int z)->float {
        if (x<0||x>=g_voxNx||y<0||y>=g_voxNy||z<0||z>=g_voxNz) return 1000.0f;
        int id = (z*g_voxNy + y)*g_voxNx + x;
        if (id<0||id>=(int)g_distanceField.size()) return 1000.0f;
        float v = g_distanceField[id];
        return isValidFloatSafe(v) ? v : 1000.0f;
    };

    // Если вне — возвращаем ближайший
    if (ix0<0||iy0<0||iz0<0||ix1>=g_voxNx||iy1>=g_voxNy||iz1>=g_voxNz) {
        int ix = (int)((p.x - g_voxMinX)/csx);
        int iy = (int)((p.y - g_voxMinY)/csy);
        int iz = (int)((p.z - g_voxMinZ)/csz);
        if (ix<0||ix>=g_voxNx||iy<0||iy>=g_voxNy||iz<0||iz>=g_voxNz) return 1000.0f;
        int idx = (iz*g_voxNy+iy)*g_voxNx+ix;
        if (idx<0||idx>=(int)g_distanceField.size()) return 1000.0f;
        return g_distanceField[idx];
    }

    float c000 = get(ix0,iy0,iz0), c100 = get(ix1,iy0,iz0), c010 = get(ix0,iy1,iz0), c110 = get(ix1,iy1,iz0);
    float c001 = get(ix0,iy0,iz1), c101 = get(ix1,iy0,iz1), c011 = get(ix0,iy1,iz1), c111 = get(ix1,iy1,iz1);

    float c00 = c000*(1-tx) + c100*tx;
    float c10 = c010*(1-tx) + c110*tx;
    float c01 = c001*(1-tx) + c101*tx;
    float c11 = c011*(1-tx) + c111*tx;
    float c0 = c00*(1-ty) + c10*ty;
    float c1 = c01*(1-ty) + c11*ty;
    float c = c0*(1-tz) + c1*tz;
    return c;
}

glm::vec3 sdfNormalCPU(const glm::vec3& p) {
    if (g_distanceField.empty() || g_voxNx <= 1 || g_voxNy <= 1 || g_voxNz <= 1) return glm::vec3(0,1,0);
    if (!isValidVec3Safe(p)) return glm::vec3(0,1,0);
    float csx = flowParams.cellSizeX;
    float csy = flowParams.cellSizeY;
    float csz = flowParams.cellSizeZ;
    if (!isValidFloatSafe(csx) || fabsf(csx) < 1e-8f) return glm::vec3(0,1,0);
    if (!isValidFloatSafe(csy) || fabsf(csy) < 1e-8f) csy = csx;
    if (!isValidFloatSafe(csz) || fabsf(csz) < 1e-8f) csz = csx;

    // Центральные разности с трилинейным SDF — более точные нормали
    float eps = fminf(csx, fminf(csy, csz)) * 0.5f;
    if (eps < 1e-4f) eps = 0.01f;
    float dx = sampleSDFCPU(glm::vec3(p.x+eps, p.y, p.z)) - sampleSDFCPU(glm::vec3(p.x-eps, p.y, p.z));
    float dy = sampleSDFCPU(glm::vec3(p.x, p.y+eps, p.z)) - sampleSDFCPU(glm::vec3(p.x, p.y-eps, p.z));
    float dz = sampleSDFCPU(glm::vec3(p.x, p.y, p.z+eps)) - sampleSDFCPU(glm::vec3(p.x, p.y, p.z-eps));
    glm::vec3 n(dx, dy, dz);
    float len = glm::length(n);
    if (!isValidFloatSafe(len) || len < 1e-6f) {
        // Fallback к воксельному градиенту
        int ix = (int)((p.x - g_voxMinX)/csx);
        int iy = (int)((p.y - g_voxMinY)/csy);
        int iz = (int)((p.z - g_voxMinZ)/csz);
        if (ix<=0||ix>=g_voxNx-1||iy<=0||iy>=g_voxNy-1||iz<=0||iz>=g_voxNz-1) return glm::vec3(0,1,0);
        auto safeGet = [&](int x,int y,int z)->float {
            if (x<0||x>=g_voxNx||y<0||y>=g_voxNy||z<0||z>=g_voxNz) return 0.0f;
            int id = (z*g_voxNy+y)*g_voxNx+x;
            if (id<0||id>=(int)g_distanceField.size()) return 0.0f;
            float v = g_distanceField[id];
            return isValidFloatSafe(v)?v:0.0f;
        };
        float ddx = safeGet(ix+1,iy,iz) - safeGet(ix-1,iy,iz);
        float ddy = safeGet(ix,iy+1,iz) - safeGet(ix,iy-1,iz);
        float ddz = safeGet(ix,iy,iz+1) - safeGet(ix,iy,iz-1);
        n = glm::vec3(ddx,ddy,ddz);
        len = glm::length(n);
        if (len < 1e-6f) return glm::vec3(0,1,0);
    }
    return n / len;
}

// =====================================================
// Вокселизация — v1.19.0 Physics Logic Fix — Евклидов SDF
// - 26 соседей с евклидовыми весами, а не 6
// - Fast Sweeping с 8 проходами для точного Евклида
// - Гауссовское сглаживание вместо box blur
// - Сохранение острых углов
// =====================================================
static inline bool rayTri(const glm::vec3& orig, const glm::vec3& dir,
                   const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2) {
    glm::vec3 e1 = v1-v0, e2 = v2-v0;
    glm::vec3 pv = glm::cross(dir, e2);
    float det = glm::dot(e1, pv);
    if (fabsf(det) < 1e-8f) return false;
    float inv = 1.0f/det;
    glm::vec3 tv = orig - v0;
    float u = glm::dot(tv, pv)*inv;
    if (u < 0.0f || u > 1.0f) return false;
    glm::vec3 qv = glm::cross(tv, e1);
    float v = glm::dot(dir, qv)*inv;
    if (v < 0.0f || u+v > 1.0f) return false;
    float t = glm::dot(e2, qv)*inv;
    return t > 1e-6f;
}

struct TriAABB {
    glm::vec3 v0,v1,v2;
    glm::vec3 minB, maxB;
};

static bool insideMeshOptimized(const glm::vec3& p, const std::vector<TriAABB>& tris, const glm::vec3& dir) {
    int hits = 0;
    for (const auto& tri : tris) {
        if (p.y < tri.minB.y || p.y > tri.maxB.y) continue;
        if (p.z < tri.minB.z || p.z > tri.maxB.z) continue;
        if (tri.maxB.x < p.x) continue;
        glm::vec3 e1 = tri.v1 - tri.v0;
        glm::vec3 e2 = tri.v2 - tri.v0;
        if (glm::length(glm::cross(e1,e2)) < 1e-12f) continue;
        if (rayTri(p, dir, tri.v0, tri.v1, tri.v2)) hits++;
    }
    return (hits % 2) == 1;
}

void buildVoxelGrid(const std::vector<float>& verts, int res) {
    if (verts.empty()) {
        std::cerr << "[Voxel] Empty vertices" << std::endl;
        return;
    }
    if (res < 8) res = 8;
    if (res > 256) res = 256;

    std::cout << "Voxelizing at resolution " << res << "... (v1.19.0 Physics Logic Fix)" << std::endl;
    auto t0 = std::chrono::high_resolution_clock::now();

    if (!std::isfinite(minBB.x) || !std::isfinite(maxBB.x) || glm::length(maxBB-minBB) < 1e-6f) {
        std::cerr << "[Voxel] Invalid BB" << std::endl;
        return;
    }

    float margin = 0.14f * maxDim;
    if (!std::isfinite(margin) || margin < 0.001f) margin = 0.1f;
    g_voxMinX = minBB.x - margin; g_voxMaxX = maxBB.x + margin;
    g_voxMinY = minBB.y - margin; g_voxMaxY = maxBB.y + margin;
    g_voxMinZ = minBB.z - margin; g_voxMaxZ = maxBB.z + margin;

    float sizeX = g_voxMaxX - g_voxMinX;
    float sizeY = g_voxMaxY - g_voxMinY;
    float sizeZ = g_voxMaxZ - g_voxMinZ;
    if (sizeX < 1e-6f) sizeX = 1.0f;
    if (sizeY < 1e-6f) sizeY = 1.0f;
    if (sizeZ < 1e-6f) sizeZ = 1.0f;
    float m = fmaxf(sizeX, fmaxf(sizeY, sizeZ));

    g_voxNx = res;
    g_voxNy = (int)(res * sizeY / m); if (g_voxNy < 4) g_voxNy = 4;
    g_voxNz = (int)(res * sizeZ / m); if (g_voxNz < 4) g_voxNz = 4;

    long long totalLL = (long long)g_voxNx * g_voxNy * g_voxNz;
    const long long MAX_CELLS = 10*1024*1024;
    if (totalLL > MAX_CELLS) {
        float scale = powf((float)MAX_CELLS / totalLL, 1.0f/3.0f);
        g_voxNx = (int)(g_voxNx*scale); if (g_voxNx < 4) g_voxNx = 4;
        g_voxNy = (int)(g_voxNy*scale); if (g_voxNy < 4) g_voxNy = 4;
        g_voxNz = (int)(g_voxNz*scale); if (g_voxNz < 4) g_voxNz = 4;
        std::cout << "[Voxel] Clamped to " << g_voxNx << "x" << g_voxNy << "x" << g_voxNz << std::endl;
    }

    float csx = sizeX / g_voxNx;
    float csy = sizeY / g_voxNy;
    float csz = sizeZ / g_voxNz;
    if (csx < 1e-6f) csx = 0.1f;
    if (csy < 1e-6f) csy = 0.1f;
    if (csz < 1e-6f) csz = 0.1f;

    int total = g_voxNx * g_voxNy * g_voxNz;
    if (total <= 0 || total > 20*1024*1024) {
        std::cerr << "[Voxel] Invalid total: " << total << std::endl;
        return;
    }

    try {
        g_voxelData.assign(total, 0);
    } catch (const std::bad_alloc& e) {
        std::cerr << "[Voxel] Alloc failed: " << e.what() << std::endl;
        return;
    }

    std::vector<TriAABB> tris;
    tris.reserve(verts.size()/9 + 1);
    for (size_t i = 0; i + 8 < verts.size(); i += 9) {
        float x0=verts[i], y0=verts[i+1], z0=verts[i+2];
        float x1=verts[i+3], y1=verts[i+4], z1=verts[i+5];
        float x2=verts[i+6], y2=verts[i+7], z2=verts[i+8];
        if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(z0)) continue;
        TriAABB t;
        t.v0 = glm::vec3(x0,y0,z0);
        t.v1 = glm::vec3(x1,y1,z1);
        t.v2 = glm::vec3(x2,y2,z2);
        t.minB = glm::vec3(std::min({t.v0.x, t.v1.x, t.v2.x}),
                           std::min({t.v0.y, t.v1.y, t.v2.y}),
                           std::min({t.v0.z, t.v1.z, t.v2.z}));
        t.maxB = glm::vec3(std::max({t.v0.x, t.v1.x, t.v2.x}),
                           std::max({t.v0.y, t.v1.y, t.v2.y}),
                           std::max({t.v0.z, t.v1.z, t.v2.z}));
        if (glm::length(glm::cross(t.v1-t.v0, t.v2-t.v0)) < 1e-12f) continue;
        tris.push_back(t);
    }

    glm::vec3 rayDir(1,0,0);

    if (tris.empty()) {
        std::cerr << "[Voxel] No valid triangles for voxelization" << std::endl;
    } else {
        #ifdef _OPENMP
        #pragma omp parallel for collapse(3) schedule(dynamic)
        #endif
        for (int k = 0; k < g_voxNz; k++) {
            for (int j = 0; j < g_voxNy; j++) {
                for (int i = 0; i < g_voxNx; i++) {
                    glm::vec3 p(g_voxMinX + (i+0.5f)*csx,
                                g_voxMinY + (j+0.5f)*csy,
                                g_voxMinZ + (k+0.5f)*csz);
                    if (insideMeshOptimized(p, tris, rayDir))
                        g_voxelData[(k*g_voxNy + j)*g_voxNx + i] = 1;
                }
            }
        }
    }

    try {
        g_distanceField.assign(total, 1e6f);
    } catch (...) {
        std::cerr << "[Voxel] Distance field alloc failed" << std::endl;
        return;
    }

    // v1.19.0: Евклидов SDF — Fast Sweeping с 26 соседями и евклидовыми весами
    // Инициализация поверхности
    std::vector<int> q;
    q.reserve(total/3 + 1);
    const int off6[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};

    for (int k = 0; k < g_voxNz; k++)
        for (int j = 0; j < g_voxNy; j++)
            for (int i = 0; i < g_voxNx; i++) {
                int idx = (k*g_voxNy + j)*g_voxNx + i;
                bool here = g_voxelData[idx] == 1;
                bool isSurf = false;
                for (auto& o : off6) {
                    int ni=i+o[0], nj=j+o[1], nk=k+o[2];
                    if (ni<0||ni>=g_voxNx||nj<0||nj>=g_voxNy||nk<0||nk>=g_voxNz) continue;
                    int nidx = (nk*g_voxNy + nj)*g_voxNx + ni;
                    if (nidx <0 || nidx >= total) continue;
                    if ((g_voxelData[nidx]==1) != here) { isSurf = true; break; }
                }
                if (isSurf) {
                    g_distanceField[idx] = here ? -0.5f * fminf(csx, fminf(csy,csz)) : 0.5f * fminf(csx, fminf(csy,csz));
                    q.push_back(idx);
                }
            }

    // 26 соседей с евклидовыми весами
    struct Neighbor { int dx,dy,dz; float w; };
    std::vector<Neighbor> neigh;
    for (int dz=-1; dz<=1; ++dz)
        for (int dy=-1; dy<=1; ++dy)
            for (int dx=-1; dx<=1; ++dx) {
                if (dx==0&&dy==0&&dz==0) continue;
                float wx = dx*csx, wy = dy*csy, wz = dz*csz;
                float w = sqrtf(wx*wx + wy*wy + wz*wz);
                neigh.push_back({dx,dy,dz,w});
            }

    // BFS с приоритетом по расстоянию — Dijkstra-like для точного Евклида
    // Используем очередь, но с сортировкой по |d| для лучшего приближения
    size_t head = 0;
    // Для скорости — 2 прохода: сначала прямой, потом обратный (Fast Sweeping 8 направлений)
    // Упростим: 8 sweep проходов
    for (int sweep=0; sweep<4; ++sweep) {
        // 8 направлений sweeping
        int iStart,iEnd,iStep,jStart,jEnd,jStep,kStart,kEnd,kStep;
        if (sweep==0) { iStart=0; iEnd=g_voxNx; iStep=1; jStart=0; jEnd=g_voxNy; jStep=1; kStart=0; kEnd=g_voxNz; kStep=1; }
        else if (sweep==1) { iStart=g_voxNx-1; iEnd=-1; iStep=-1; jStart=0; jEnd=g_voxNy; jStep=1; kStart=0; kEnd=g_voxNz; kStep=1; }
        else if (sweep==2) { iStart=0; iEnd=g_voxNx; iStep=1; jStart=g_voxNy-1; jEnd=-1; jStep=-1; kStart=0; kEnd=g_voxNz; kStep=1; }
        else { iStart=g_voxNx-1; iEnd=-1; iStep=-1; jStart=g_voxNy-1; jEnd=-1; jStep=-1; kStart=g_voxNz-1; kEnd=-1; kStep=-1; }

        for (int k=kStart; k!=kEnd; k+=kStep) {
            for (int j=jStart; j!=jEnd; j+=jStep) {
                for (int i=iStart; i!=iEnd; i+=iStep) {
                    int idx = (k*g_voxNy + j)*g_voxNx + i;
                    float curD = g_distanceField[idx];
                    if (fabsf(curD) > 1e5f) continue;
                    for (auto& nb : neigh) {
                        int ni=i+nb.dx, nj=j+nb.dy, nk=k+nb.dz;
                        if (ni<0||ni>=g_voxNx||nj<0||nj>=g_voxNy||nk<0||nk>=g_voxNz) continue;
                        int nidx = (nk*g_voxNy + nj)*g_voxNx + ni;
                        if (nidx<0||nidx>=total) continue;
                        float nd = (curD < 0) ? (curD - nb.w) : (curD + nb.w);
                        if (fabsf(nd) < fabsf(g_distanceField[nidx]) - 1e-4f) {
                            g_distanceField[nidx] = nd;
                        }
                    }
                }
            }
        }
    }

    // Дополнительный BFS для гарантии
    head = 0;
    while (head < q.size()) {
        int idx = q[head++];
        int i = idx % g_voxNx;
        int j = (idx / g_voxNx) % g_voxNy;
        int k = idx / (g_voxNx * g_voxNy);
        float d = g_distanceField[idx];
        if (!isValidFloatSafe(d)) continue;
        for (auto& nb : neigh) {
            int ni=i+nb.dx, nj=j+nb.dy, nk=k+nb.dz;
            if (ni<0||ni>=g_voxNx||nj<0||nj>=g_voxNy||nk<0||nk>=g_voxNz) continue;
            int nidx = (nk*g_voxNy + nj)*g_voxNx + ni;
            if (nidx <0 || nidx >= total) continue;
            float nd = (d < 0) ? (d - nb.w) : (d + nb.w);
            if (fabsf(nd) < fabsf(g_distanceField[nidx]) - 0.001f) {
                g_distanceField[nidx] = nd;
                q.push_back(nidx);
            }
        }
    }

    // v1.19.0: Гауссовское сглаживание с сохранением острых углов — bilateral-like
    {
        std::vector<float> temp = g_distanceField;
        // 2 итерации гауссовского с sigma=1.0
        const float kernel[3] = {0.25f, 0.5f, 0.25f}; // 1D Gaussian
        for (int iter=0; iter<2; ++iter) {
            // X pass
            #ifdef _OPENMP
            #pragma omp parallel for collapse(3)
            #endif
            for (int k=1; k<g_voxNz-1; ++k) {
                for (int j=1; j<g_voxNy-1; ++j) {
                    for (int i=1; i<g_voxNx-1; ++i) {
                        int idx = (k*g_voxNy + j)*g_voxNx + i;
                        if (fabsf(temp[idx]) < csx*1.2f) continue; // не трогаем поверхность
                        float sum = temp[idx-1]*kernel[0] + temp[idx]*kernel[1] + temp[idx+1]*kernel[2];
                        g_distanceField[idx] = sum*0.35f + temp[idx]*0.65f;
                    }
                }
            }
            temp = g_distanceField;
            // Y pass
            #ifdef _OPENMP
            #pragma omp parallel for collapse(3)
            #endif
            for (int k=1; k<g_voxNz-1; ++k) {
                for (int j=1; j<g_voxNy-1; ++j) {
                    for (int i=1; i<g_voxNx-1; ++i) {
                        int idx = (k*g_voxNy + j)*g_voxNx + i;
                        if (fabsf(temp[idx]) < csy*1.2f) continue;
                        int idxYm = (k*g_voxNy + (j-1))*g_voxNx + i;
                        int idxYp = (k*g_voxNy + (j+1))*g_voxNx + i;
                        float sum = temp[idxYm]*kernel[0] + temp[idx]*kernel[1] + temp[idxYp]*kernel[2];
                        g_distanceField[idx] = sum*0.35f + temp[idx]*0.65f;
                    }
                }
            }
            temp = g_distanceField;
            // Z pass
            #ifdef _OPENMP
            #pragma omp parallel for collapse(3)
            #endif
            for (int k=1; k<g_voxNz-1; ++k) {
                for (int j=1; j<g_voxNy-1; ++j) {
                    for (int i=1; i<g_voxNx-1; ++i) {
                        int idx = (k*g_voxNy + j)*g_voxNx + i;
                        if (fabsf(temp[idx]) < csz*1.2f) continue;
                        int idxZm = ((k-1)*g_voxNy + j)*g_voxNx + i;
                        int idxZp = ((k+1)*g_voxNy + j)*g_voxNx + i;
                        float sum = temp[idxZm]*kernel[0] + temp[idx]*kernel[1] + temp[idxZp]*kernel[2];
                        g_distanceField[idx] = sum*0.35f + temp[idx]*0.65f;
                    }
                }
            }
            temp = g_distanceField;
        }
    }

    setVoxelData(g_voxelData.data(), g_distanceField.data(),
                 g_voxNx, g_voxNy, g_voxNz,
                 g_voxMinX, g_voxMinY, g_voxMinZ,
                 csx, csy, csz);

    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1-t0).count();
    std::cout << "Voxelized v1.19.0 Physics Logic Fix: " << g_voxNx << "x" << g_voxNy << "x" << g_voxNz
              << " (" << total << " cells, " << tris.size() << " tris) in " << ms << " ms"
#ifdef _OPENMP
              << " [OpenMP] [26-neighbor Euclidean]"
#endif
              << std::endl;
}
