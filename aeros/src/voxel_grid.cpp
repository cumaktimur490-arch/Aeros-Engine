#include <glad/glad.h>
#include <glm/glm.hpp>

#include <iostream>
#include <chrono>
#include <cmath>
#include <vector>

#include "globals.h"
#include "cuda_api.h"
#include "voxel_grid.h"

#ifdef _OPENMP
#include <omp.h>
#endif

// =====================================================
// SDF sampling (CPU) — оптимизировано с кэшированием
// =====================================================
static inline bool isValidFloatSafe(float v) { return std::isfinite(v); }
static inline bool isValidVec3Safe(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

float sampleSDFCPU(const glm::vec3& p) {
    if (g_distanceField.empty() || g_voxNx <= 0 || g_voxNy <= 0 || g_voxNz <= 0) return 1000.0f;
    if (!isValidVec3Safe(p)) return 1000.0f;
    float csx = flowParams.cellSizeX;
    if (!isValidFloatSafe(csx) || fabsf(csx) < 1e-8f) return 1000.0f;
    // Используем одинаковый cellSize для всех осей — быстрее
    int ix = (int)((p.x - g_voxMinX) / csx);
    int iy = (int)((p.y - g_voxMinY) / flowParams.cellSizeY);
    int iz = (int)((p.z - g_voxMinZ) / flowParams.cellSizeZ);
    if (ix < 0 || ix >= g_voxNx || iy < 0 || iy >= g_voxNy || iz < 0 || iz >= g_voxNz)
        return 1000.0f;
    int idx = (iz * g_voxNy + iy) * g_voxNx + ix;
    if (idx < 0 || idx >= (int)g_distanceField.size()) return 1000.0f;
    float raw = g_distanceField[idx];
    if (!isValidFloatSafe(raw)) return 1000.0f;
    return raw * csx;
}

glm::vec3 sdfNormalCPU(const glm::vec3& p) {
    if (g_distanceField.empty() || g_voxNx <= 1 || g_voxNy <= 1 || g_voxNz <= 1) return glm::vec3(0,1,0);
    if (!isValidVec3Safe(p)) return glm::vec3(0,1,0);
    float csx = flowParams.cellSizeX;
    float csy = flowParams.cellSizeY;
    float csz = flowParams.cellSizeZ;
    if (!isValidFloatSafe(csx) || fabsf(csx) < 1e-8f) return glm::vec3(0,1,0);
    if (!isValidFloatSafe(csy) || fabsf(csy) < 1e-8f) return glm::vec3(0,1,0);
    if (!isValidFloatSafe(csz) || fabsf(csz) < 1e-8f) return glm::vec3(0,1,0);
    int ix = (int)((p.x - g_voxMinX) / csx);
    int iy = (int)((p.y - g_voxMinY) / csy);
    int iz = (int)((p.z - g_voxMinZ) / csz);
    if (ix <= 0 || ix >= g_voxNx-1 || iy <= 0 || iy >= g_voxNy-1 || iz <= 0 || iz >= g_voxNz-1)
        return glm::vec3(0,1,0);
    auto safeGet = [&](int x, int y, int z) -> float {
        if (x < 0 || x >= g_voxNx || y < 0 || y >= g_voxNy || z < 0 || z >= g_voxNz) return 0.0f;
        int id = (z*g_voxNy + y)*g_voxNx + x;
        if (id < 0 || id >= (int)g_distanceField.size()) return 0.0f;
        float v = g_distanceField[id];
        return isValidFloatSafe(v) ? v : 0.0f;
    };
    float dx = safeGet(ix+1,iy,iz) - safeGet(ix-1,iy,iz);
    float dy = safeGet(ix,iy+1,iz) - safeGet(ix,iy-1,iz);
    float dz = safeGet(ix,iy,iz+1) - safeGet(ix,iy,iz-1);
    glm::vec3 n(dx, dy, dz);
    if (!isValidVec3Safe(n)) return glm::vec3(0,1,0);
    float len = glm::length(n);
    if (!isValidFloatSafe(len) || len < 1e-6f) return glm::vec3(0,1,0);
    return n / len;
}

// =====================================================
// Вокселизация — оптимизировано с OpenMP и AABB
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

// Быстрая проверка AABB для треугольника
struct TriAABB {
    glm::vec3 v0,v1,v2;
    glm::vec3 minB, maxB;
};

static bool insideMeshOptimized(const glm::vec3& p, const std::vector<TriAABB>& tris, const glm::vec3& dir) {
    int hits = 0;
    for (const auto& tri : tris) {
        // AABB ранний отсев по YZ — луч идет вдоль X
        if (p.y < tri.minB.y || p.y > tri.maxB.y) continue;
        if (p.z < tri.minB.z || p.z > tri.maxB.z) continue;
        if (tri.maxB.x < p.x) continue; // треугольник позади точки
        if (rayTri(p, dir, tri.v0, tri.v1, tri.v2)) hits++;
    }
    return (hits % 2) == 1;
}

void buildVoxelGrid(const std::vector<float>& verts, int res) {
    std::cout << "Voxelizing at resolution " << res << "... (optimized)" << std::endl;
    auto t0 = std::chrono::high_resolution_clock::now();

    float margin = 0.1f * maxDim;
    g_voxMinX = minBB.x - margin; g_voxMaxX = maxBB.x + margin;
    g_voxMinY = minBB.y - margin; g_voxMaxY = maxBB.y + margin;
    g_voxMinZ = minBB.z - margin; g_voxMaxZ = maxBB.z + margin;

    float sizeX = g_voxMaxX - g_voxMinX;
    float sizeY = g_voxMaxY - g_voxMinY;
    float sizeZ = g_voxMaxZ - g_voxMinZ;
    float m = fmaxf(sizeX, fmaxf(sizeY, sizeZ));

    g_voxNx = res;
    g_voxNy = (int)(res * sizeY / m); if (g_voxNy < 4) g_voxNy = 4;
    g_voxNz = (int)(res * sizeZ / m); if (g_voxNz < 4) g_voxNz = 4;

    float csx = sizeX / g_voxNx;
    float csy = sizeY / g_voxNy;
    float csz = sizeZ / g_voxNz;

    int total = g_voxNx * g_voxNy * g_voxNz;
    g_voxelData.assign(total, 0);

    // Предвычисляем треугольники с AABB
    std::vector<TriAABB> tris;
    tris.reserve(verts.size()/9);
    for (size_t i = 0; i + 8 < verts.size(); i += 9) {
        TriAABB t;
        t.v0 = glm::vec3(verts[i], verts[i+1], verts[i+2]);
        t.v1 = glm::vec3(verts[i+3], verts[i+4], verts[i+5]);
        t.v2 = glm::vec3(verts[i+6], verts[i+7], verts[i+8]);
        t.minB = glm::vec3(std::min({t.v0.x, t.v1.x, t.v2.x}),
                           std::min({t.v0.y, t.v1.y, t.v2.y}),
                           std::min({t.v0.z, t.v1.z, t.v2.z}));
        t.maxB = glm::vec3(std::max({t.v0.x, t.v1.x, t.v2.x}),
                           std::max({t.v0.y, t.v1.y, t.v2.y}),
                           std::max({t.v0.z, t.v1.z, t.v2.z}));
        tris.push_back(t);
    }

    glm::vec3 rayDir(1,0,0);

    // Параллельная вокселизация по Z
    #ifdef _OPENMP
    #pragma omp parallel for collapse(2)
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

    g_distanceField.assign(total, 1000.0f);
    std::vector<int> q;
    q.reserve(total/4);
    const int off[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};

    for (int k = 0; k < g_voxNz; k++)
        for (int j = 0; j < g_voxNy; j++)
            for (int i = 0; i < g_voxNx; i++) {
                int idx = (k*g_voxNy + j)*g_voxNx + i;
                bool here = g_voxelData[idx] == 1;
                bool isSurf = false;
                for (auto& o : off) {
                    int ni=i+o[0], nj=j+o[1], nk=k+o[2];
                    if (ni<0||ni>=g_voxNx||nj<0||nj>=g_voxNy||nk<0||nk>=g_voxNz) continue;
                    int nidx = (nk*g_voxNy + nj)*g_voxNx + ni;
                    if ((g_voxelData[nidx]==1) != here) { isSurf = true; break; }
                }
                if (isSurf) {
                    g_distanceField[idx] = here ? -0.5f : 0.5f;
                    q.push_back(idx);
                }
            }

    size_t head = 0;
    while (head < q.size()) {
        int idx = q[head++];
        int i = idx % g_voxNx;
        int j = (idx / g_voxNx) % g_voxNy;
        int k = idx / (g_voxNx * g_voxNy);
        float d = g_distanceField[idx];
        for (auto& o : off) {
            int ni=i+o[0], nj=j+o[1], nk=k+o[2];
            if (ni<0||ni>=g_voxNx||nj<0||nj>=g_voxNy||nk<0||nk>=g_voxNz) continue;
            int nidx = (nk*g_voxNy + nj)*g_voxNx + ni;
            float nd = (d<0) ? (d-1.0f) : (d+1.0f);
            if (fabsf(nd) < fabsf(g_distanceField[nidx]) - 0.01f) {
                g_distanceField[nidx] = nd;
                q.push_back(nidx);
            }
        }
    }

    setVoxelData(g_voxelData.data(), g_distanceField.data(),
                 g_voxNx, g_voxNy, g_voxNz,
                 g_voxMinX, g_voxMinY, g_voxMinZ,
                 csx, csy, csz);

    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1-t0).count();
    std::cout << "Voxelized OPT: " << g_voxNx << "x" << g_voxNy << "x" << g_voxNz
              << " (" << total << " cells, " << tris.size() << " tris) in " << ms << " ms"
#ifdef _OPENMP
              << " [OpenMP]"
#endif
              << std::endl;
}
